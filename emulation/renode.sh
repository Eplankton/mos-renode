# Shared helpers for the build/emulation/test scripts. Source it:
#   source "$ROOT/emulation/renode.sh"
#
# A board is a directory examples/boards/<board>/ with board.cmake (build), board.env
# (facts for scripts) and renode/board.resc (machine setup).

MOS_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# All boards, sorted
mos_boards() {
    local f
    for f in "$MOS_ROOT"/examples/boards/*/board.env; do
        [[ -f "$f" ]] && basename "$(dirname "$f")"
    done
}

mos_is_board() {
    [[ -n "$1" && -f "$MOS_ROOT/examples/boards/$1/board.env" ]]
}

# Load examples/boards/<board>/board.env and derived paths into BOARD_* variables
mos_load_board() {
    local board="$1"
    if ! mos_is_board "$board"; then
        echo "[error] unknown board '$board' (boards: $(mos_boards | tr '\n' ' '))" >&2
        return 1
    fi
    BOARD="$board"
    BOARD_DIR="$MOS_ROOT/examples/boards/$board"
    BOARD_RESC_DIR="$BOARD_DIR/renode"
    BOARD_ELF="$MOS_ROOT/build/$board/$board.elf"
    BOARD_TITLE="" BOARD_GDB="" BOARD_SHELL_UART="" BOARD_RB_TIMER_HZ="" BOARD_FREERTOS=""
    # shellcheck disable=SC1090
    source "$BOARD_DIR/board.env"
}

# Resolve a tool from PATH or the xPack install under ~/.local/xpacks
mos_find_tool() {
    local tool="$1" d
    if command -v "$tool" >/dev/null 2>&1; then
        command -v "$tool"
        return 0
    fi
    for d in "$HOME"/.local/xpacks/*/bin; do
        [[ -x "$d/$tool" ]] && { echo "$d/$tool"; return 0; }
    done
    return 1
}

# Renode commands that create the machine of the loaded board with `elf`
# and log its shell UART to `log`. Print to stdout.
mos_machine_setup() {
    local elf="$1" log="$2"
    cat <<RESC
\$bin_path=@$elf
mach create
include @$BOARD_RESC_DIR/board.resc
$BOARD_SHELL_UART CreateFileBackend @$log true
RESC
}

# Run a Renode script headless until `marker` appears in `watch` (a file
# the machine writes), Renode exits, or `timeout` seconds pass. Renode's own
# output goes to `renode_log`. Returns 0 if the marker was seen.
mos_renode_headless() {
    local resc="$1" watch="$2" marker="$3" timeout="$4" renode_log="$5"
    local fifo pid deadline

    fifo="$(mktemp -u "${TMPDIR:-/tmp}/mos-renode-fifo.XXXXXX")"
    mkfifo "$fifo"
    exec 3<>"$fifo" # Keep it open; Renode reads monitor commands from it

    renode --console --disable-gui "$resc" <"$fifo" >"$renode_log" 2>&1 &
    pid=$!

    deadline=$((SECONDS + timeout))
    while ((SECONDS < deadline)); do
        grep -aq "$marker" "$watch" 2>/dev/null && break
        kill -0 "$pid" 2>/dev/null || break
        sleep 0.2
    done

    echo quit >&3 2>/dev/null || true
    for _ in {1..30}; do kill -0 "$pid" 2>/dev/null || break; sleep 0.1; done
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
    exec 3>&-
    rm -f "$fifo"

    grep -aq "$marker" "$watch" 2>/dev/null
}
