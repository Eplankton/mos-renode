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
#   ./emulation/run.sh stm32f4-disco multi
#   ./emulation/run.sh stm32h7-disco multi-debug
#
# Modes:
#   demo        build, run, attach a UART console (single machine)
#   debug       build, start Renode + GDB server (single machine)
#   multi       build, run M1/M2 and multiplex both UART consoles
#   multi-debug build, start M1/M2 with GDB servers (1234/1235)
# ==============================================================================

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

BOARD=""
MODE=""
NO_BUILD=0

usage() {
    cat <<'EOF'
Usage:
  ./emulation/run.sh                 interactive board + mode selection
  ./emulation/run.sh stm32f4-disco demo        single machine + UART console
  ./emulation/run.sh stm32h7-disco debug     single machine + GDB server
  ./emulation/run.sh stm32f4-disco multi       dual-machine M2M + UART multiplexer
  ./emulation/run.sh stm32h7-disco multi-debug  dual-machine M2M + GDB 1234/1235
  ./emulation/run.sh <board> <mode> --no-build

Boards:
  stm32f4-disco    STM32F4 Discovery (Cortex-M4)
  stm32h7-disco  STM32H747I-DISCO (Cortex-M7)

Modes:
  demo         build, run, attach a UART console (single machine)
  debug        build, start Renode + GDB server (single machine)
  multi        build, run M1/M2 and multiplex both UART consoles
  multi-debug  build, start M1/M2 with GDB servers (1234/1235)
EOF
}

while (($#)); do
    case "$1" in
        stm32f4-disco) BOARD=stm32f4-disco ;;
        stm32h7-disco) BOARD=stm32h7-disco ;;
        demo|debug|multi|multi-debug) MODE="$1" ;;
        --no-build) NO_BUILD=1 ;;
        -h|--help) usage; exit 0 ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
    shift
done

if [[ -z "$BOARD" ]]; then
    if [[ ! -t 0 ]]; then
        echo "[error] no board selected (use stm32f4-disco / stm32h7-disco)" >&2
        exit 1
    fi
    echo "Select board:"
    while true; do
        printf '%s\n' \
            "  1) STM32F4 Discovery (Cortex-M4)" \
            "  2) STM32H747I-DISCO (Cortex-M7)"
        read -r -p "> " choice || exit 1
        case "$choice" in
            1) BOARD=stm32f4-disco; break ;;
            2) BOARD=stm32h7-disco; break ;;
            *) echo "Invalid selection" ;;
        esac
    done
fi

if [[ -z "$MODE" ]]; then
    if [[ ! -t 0 ]]; then
        echo "[error] no mode selected (use demo / debug / multi / multi-debug)" >&2
        exit 1
    fi
    echo "Select mode:"
    while true; do
        printf '%s\n' \
            "  1) Demo: single machine + UART console" \
            "  2) Debug: single machine + GDB server" \
            "  3) Demo: dual-machine M2M" \
            "  4) Debug: dual-machine M2M (GDB 1234/1235)"
        read -r -p "> " choice || exit 1
        case "$choice" in
            1) MODE=demo; break ;;
            2) MODE=debug; break ;;
            3) MODE=multi; break ;;
            4) MODE=multi-debug; break ;;
            *) echo "Invalid selection" ;;
        esac
    done
fi

case "$BOARD" in
    stm32f4-disco)
        BUILD_TARGET=stm32f4-disco
        BOARD_NAME="STM32F4-Discovery"
        ELF="build/stm32f4-disco/stm32f4-disco.elf"
        RESC_DIR="emulation/stm32f4-disco"
        ;;
    stm32h7-disco)
        BUILD_TARGET=stm32h7-disco
        BOARD_NAME="STM32H747I-DISCO"
        ELF="build/stm32h7-disco/stm32h7-disco.elf"
        RESC_DIR="emulation/stm32h7-disco"
        ;;
    *)
        echo "Unsupported board: $BOARD" >&2
        exit 1
        ;;
esac

if [[ "$MODE" == multi* ]]; then
    RESC="$RESC_DIR/multi_mach.resc"
else
    RESC="$RESC_DIR/debug.resc"
fi

if [[ "$NO_BUILD" -eq 0 ]]; then
    echo "[build] $BOARD_NAME"
    "$ROOT/build.sh" "$BUILD_TARGET"
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
    $SETSID arm-none-eabi-gdb -q -batch "$ELF" \
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
        echo "Use VS Code F5, or: arm-none-eabi-gdb $ELF"
        echo "Press Ctrl-C to stop."
        wait "$RENODE_PID"
        ;;
    multi-debug)
        echo "Renode is running (M1/M2)."
        echo "  GDB : localhost:1234 (M1), localhost:1235 (M2)"
        echo "  UART: localhost:3333 (M1), localhost:3334 (M2)"
        echo "  M1: arm-none-eabi-gdb $ELF -ex 'target remote :1234'"
        echo "  M2: arm-none-eabi-gdb $ELF -ex 'target remote :1235'"
        echo "Press Ctrl-C to stop."
        wait "$RENODE_PID"
        ;;
    demo)
        if ! command -v arm-none-eabi-gdb >/dev/null 2>&1; then
            echo "[error] arm-none-eabi-gdb not found" >&2
            exit 1
        fi
        start_gdb 1234
        echo "[uart] connecting to localhost:3333"
        run_console --port 3333
        ;;
    multi)
        if ! command -v arm-none-eabi-gdb >/dev/null 2>&1; then
            echo "[error] arm-none-eabi-gdb not found" >&2
            exit 1
        fi
        start_gdb 1234
        start_gdb 1235
        echo "[uart] multiplexing M1@3333 and M2@3334"
        run_console --port 3333:M1 --port 3334:M2 --send-port 3333
        ;;
esac
