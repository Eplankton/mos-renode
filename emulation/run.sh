#!/usr/bin/env bash
# ==============================================================================
# MOS Renode launcher
#
# Interactive:
#   ./emulation/run.sh
#
# Non-interactive:
#   ./emulation/run.sh stm32f4-disco demo
#   ./emulation/run.sh stm32h7-disco debug
#   ./emulation/run.sh riscv32-virt multi
#   ./emulation/run.sh stm32f4-disco multi-debug
#   ./emulation/run.sh <board> <mode> --no-build
#
# Modes:
#   demo        build, run, attach a UART console (single machine)
#   debug       build, start Renode + GDB server (single machine)
#   multi       build, run M1/M2 and multiplex both UART consoles
#   multi-debug build, start M1/M2 with GDB servers (1234/1235)
#
# Boards are the directories under examples/boards/; each provides renode/debug.resc
# and renode/multi_mach.resc.
# ==============================================================================

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
source "$ROOT/emulation/renode.sh"

BOARD=""
MODE=""
NO_BUILD=0
MODES=(demo debug multi multi-debug)
declare -A MODE_DESC=(
    [demo]="Demo: single machine + UART console"
    [debug]="Debug: single machine + GDB server (1234)"
    [multi]="Demo: dual-machine M2M + UART multiplexer"
    [multi-debug]="Debug: dual-machine M2M (GDB 1234/1235)"
)
mapfile -t BOARDS < <(mos_boards)

usage() {
    cat <<'EOF'
Usage:
  ./emulation/run.sh                              interactive board + mode selection
  ./emulation/run.sh stm32f4-disco demo           single machine + UART console
  ./emulation/run.sh stm32h7-disco debug          single machine + GDB server
  ./emulation/run.sh riscv32-virt multi           dual-machine M2M + UART multiplexer
  ./emulation/run.sh stm32f4-disco multi-debug    dual-machine M2M + GDB 1234/1235
  ./emulation/run.sh <board> <mode> --no-build    skip the build step

Boards:
EOF
    local b m
    for b in "${BOARDS[@]}"; do
        mos_load_board "$b"
        printf '  %-16s %s\n' "$b" "$BOARD_TITLE"
    done
    echo
    echo "Modes:"
    for m in "${MODES[@]}"; do
        printf '  %-16s %s\n' "$m" "${MODE_DESC[$m]}"
    done
}

while (($#)); do
    case "$1" in
        demo|debug|multi|multi-debug) MODE="$1" ;;
        --no-build) NO_BUILD=1 ;;
        -h|--help) usage; exit 0 ;;
        *)
            if mos_is_board "$1"; then
                BOARD="$1"
            else
                echo "Unknown argument: $1" >&2
                usage >&2
                exit 1
            fi
            ;;
    esac
    shift
done

# pick <prompt> <value>... : numbered menu showing each value's label
# (from LABELS[value]), prints the chosen value
pick() {
    local prompt="$1"; shift
    local opts=("$@") i choice
    echo "$prompt" >&2
    while true; do
        for i in "${!opts[@]}"; do
            printf '  %d) %s\n' $((i + 1)) "${LABELS[${opts[$i]}]}" >&2
        done
        read -r -p "> " choice || exit 1
        if [[ "$choice" =~ ^[0-9]+$ ]] && ((choice >= 1 && choice <= ${#opts[@]})); then
            echo "${opts[$((choice - 1))]}"
            return
        fi
        echo "Invalid selection" >&2
    done
}

declare -A LABELS=()

if [[ -z "$BOARD" ]]; then
    if [[ ! -t 0 ]]; then
        echo "[error] no board selected ($(mos_boards | tr '\n' ' '))" >&2
        exit 1
    fi
    for b in "${BOARDS[@]}"; do
        mos_load_board "$b"
        LABELS[$b]="$BOARD_TITLE"
    done
    BOARD="$(pick "Select board:" "${BOARDS[@]}")"
fi

if [[ -z "$MODE" ]]; then
    if [[ ! -t 0 ]]; then
        echo "[error] no mode selected (use ${MODES[*]})" >&2
        exit 1
    fi
    for m in "${MODES[@]}"; do
        LABELS[$m]="${MODE_DESC[$m]}"
    done
    MODE="$(pick "Select mode:" "${MODES[@]}")"
fi

mos_load_board "$BOARD"
BOARD_NAME="$BOARD_TITLE"
ELF="build/$BOARD/$BOARD.elf"

if [[ "$MODE" == multi* ]]; then
    RESC="$BOARD_RESC_DIR/multi_mach.resc"
else
    RESC="$BOARD_RESC_DIR/debug.resc"
fi

# GDB matching the board's architecture (PATH or ~/.local/xpacks)
GDB="$(mos_find_tool "$BOARD_GDB" || echo "$BOARD_GDB")"

if [[ "$NO_BUILD" -eq 0 ]]; then
    echo "[build] $BOARD_NAME"
    "$ROOT/build.sh" "$BOARD"
else
    echo "[build] skipped (--no-build)"
fi

if [[ ! -f "$ELF" ]]; then
    echo "[error] not found: $ELF" >&2
    exit 1
fi

if ! command -v renode >/dev/null 2>&1; then
    echo "[error] renode not found in PATH" >&2
    exit 1
fi

SETSID=""
if command -v setsid >/dev/null 2>&1; then
    SETSID=setsid
fi

LOG="$(mktemp "${TMPDIR:-/tmp}/mos-renode-log.XXXXXX")"
FIFO="$(mktemp -u "${TMPDIR:-/tmp}/mos-renode-fifo.XXXXXX")"
mkfifo "$FIFO"

# Keep a read/write descriptor on the FIFO. Renode reads monitor commands from
# it, while this script can inject "quit" during cleanup.
exec 3<>"$FIFO"

RENODE_PID=""
GDB_PIDS=()
CONSOLE_PID=""

kill_tree() {
    local pid="$1"
    [[ -z "$pid" ]] && return 0
    # Each child is started with setsid when available, so the negative PID
    # targets its process group and also reaps helpers such as mono/GDB.
    kill -TERM -- "-$pid" 2>/dev/null || kill -TERM "$pid" 2>/dev/null || true
}

cleanup() {
    trap - EXIT INT TERM

    if [[ -n "$CONSOLE_PID" ]]; then
        kill -TERM "$CONSOLE_PID" 2>/dev/null || true
    fi

    for pid in "${GDB_PIDS[@]:-}"; do
        kill_tree "$pid"
    done

    if [[ -n "$RENODE_PID" ]] && kill -0 "$RENODE_PID" 2>/dev/null; then
        echo "quit" >&3 2>/dev/null || true
        exec 3>&-
        for _ in {1..20}; do
            kill -0 "$RENODE_PID" 2>/dev/null || break
            sleep 0.1
        done
        kill_tree "$RENODE_PID"
        wait "$RENODE_PID" 2>/dev/null || true
    else
        exec 3>&- 2>/dev/null || true
    fi

    rm -f "$FIFO"
    echo "[renode] stopped (log: $LOG)"
}
trap cleanup EXIT INT TERM

wait_for_log() {
    local pattern="$1"
    local timeout="${2:-15}"
    local deadline=$((SECONDS + timeout))

    while ((SECONDS < deadline)); do
        if grep -q "$pattern" "$LOG" 2>/dev/null; then
            return 0
        fi
        if ! kill -0 "$RENODE_PID" 2>/dev/null; then
            return 1
        fi
        sleep 0.1
    done
    return 1
}

echo "[renode] $BOARD_NAME / $MODE"
$SETSID renode --console "$RESC" <"$FIFO" >"$LOG" 2>&1 &
RENODE_PID=$!

if ! wait_for_log "MOS_RENODE_READY" 25; then
    echo "[error] Renode did not finish its startup script" >&2
    tail -30 "$LOG" >&2 || true
    exit 1
fi

start_gdb() {
    local port="$1"
    $SETSID "$GDB" -q -batch "$ELF" \
        -ex "set confirm off" \
        -ex "target remote 127.0.0.1:$port" \
        -ex "load" \
        -ex "continue" >"${LOG}.gdb-$port" 2>&1 &
    GDB_PIDS+=("$!")
}

run_console() {
    # Keep the console in the background while waiting so SIGINT/SIGTERM can
    # run cleanup immediately instead of being deferred until the child exits.
    python3 "$ROOT/emulation/uart_console.py" "$@" <&0 &
    CONSOLE_PID=$!
    wait "$CONSOLE_PID" || true
    CONSOLE_PID=""
}

case "$MODE" in
    debug)
        echo "Renode is running."
        echo "  GDB : localhost:1234"
        echo "  UART: localhost:3333 (telnet)"
        echo "Use VS Code F5, or: $(basename "$GDB") $ELF"
        echo "Press Ctrl-C to stop."
        wait "$RENODE_PID"
        ;;
    multi-debug)
        echo "Renode is running (M1/M2)."
        echo "  GDB : localhost:1234 (M1), localhost:1235 (M2)"
        echo "  UART: localhost:3333 (M1), localhost:3334 (M2)"
        echo "  M1: $(basename "$GDB") $ELF -ex 'target remote :1234'"
        echo "  M2: $(basename "$GDB") $ELF -ex 'target remote :1235'"
        echo "Press Ctrl-C to stop."
        wait "$RENODE_PID"
        ;;
    demo)
        if ! command -v "$GDB" >/dev/null 2>&1; then
            echo "[error] $GDB not found" >&2
            exit 1
        fi
        start_gdb 1234
        echo "[uart] connecting to localhost:3333"
        run_console --port 3333
        ;;
    multi)
        if ! command -v "$GDB" >/dev/null 2>&1; then
            echo "[error] $GDB not found" >&2
            exit 1
        fi
        start_gdb 1234
        start_gdb 1235
        echo "[uart] multiplexing M1@3333 and M2@3334"
        run_console --port 3333:M1 --port 3334:M2 --send-port 3333
        ;;
esac
