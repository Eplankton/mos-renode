#!/usr/bin/env bash
# ==============================================================================
# RTOS-Benchmark runner (Renode)
#
#   ./benchmarks/rtos_benchmark/run.sh [board] [options]
#
#   board: any board under examples/boards/ with benchmark support (default stm32f4-disco)
#   options:
#     --iterations N   iterations per test (default 1000)
#     --no-build       reuse build/<board>/rtos_benchmark.elf
#
# Results are written to build/<board>/rtos_benchmark/.
# ==============================================================================

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
source "$ROOT/emulation/renode.sh"

BOARD="stm32f4-disco"
ITERATIONS=1000
DO_BUILD=1
TIMER_HZ=0

usage() {
    cat <<'EOF'
Usage:
  ./benchmarks/rtos_benchmark/run.sh [board] [options]

Options:
  --iterations N  Iterations per benchmark test (default 1000)
  --timer-hz N    Timer clock used for ns conversion (default: the Renode
                  model's rate for the board; use 0 for SystemCoreClock)
  --no-build      Reuse build/<board>/rtos_benchmark.elf
  -h, --help      Show this help

The suite reports average/min/max latency in nanoseconds for thread,
mutex, semaphore, queue, allocator and interrupt-latency operations.
EOF
    local b
    echo
    echo "Boards with benchmark support (default stm32f4-disco):"
    for b in $(mos_boards); do
        mos_load_board "$b"
        [[ -n "$BOARD_RB_TIMER_HZ" ]] && printf '  %-16s %s\n' "$b" "$BOARD_TITLE"
    done
    return 0
}

while (($#)); do
    case "$1" in
        --iterations) ITERATIONS="${2:?missing iteration count}"; shift ;;
        --timer-hz) TIMER_HZ="${2:?missing timer clock}"; shift ;;
        --no-build) DO_BUILD=0 ;;
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

mos_load_board "$BOARD"

if ! [[ "$ITERATIONS" =~ ^[1-9][0-9]*$ ]]; then
    echo "--iterations must be a positive integer" >&2
    exit 1
fi
if ! [[ "$TIMER_HZ" =~ ^[0-9]+$ ]]; then
    echo "--timer-hz must be zero or a positive integer" >&2
    exit 1
fi

# Renode models the benchmark timer at a fixed rate (examples/boards/<board>/board.env)
if ((TIMER_HZ == 0)); then
    if [[ -z "$BOARD_RB_TIMER_HZ" ]]; then
        echo "[error] $BOARD does not support the benchmarks" >&2
        exit 1
    fi
    TIMER_HZ=$BOARD_RB_TIMER_HZ
fi

if ! command -v renode >/dev/null 2>&1; then
    echo "[error] renode not found in PATH" >&2
    exit 1
fi

BUILD_DIR="$ROOT/build/$BOARD"
RUN_DIR="$BUILD_DIR/rtos_benchmark"
ELF="$BUILD_DIR/rtos_benchmark.elf"
LOG="$RUN_DIR/rtos_benchmark.uart.log"
RESC="$RUN_DIR/rtos_benchmark.resc"
RENODE_LOG="$RUN_DIR/rtos_benchmark.renode.log"

mkdir -p "$RUN_DIR"

if ((DO_BUILD)); then
    echo "[build] $BOARD (RTOS-Benchmark, ${ITERATIONS} iterations, ${TIMER_HZ} Hz timer)"
    cmake -S "$ROOT" -B "$BUILD_DIR" \
        -DMOS_BOARD="$BOARD" \
        -DMOS_RTOS_BENCHMARK=ON \
        -DMOS_RB_ITERATIONS="$ITERATIONS" \
        -DMOS_RB_TIMER_HZ="$TIMER_HZ" \
        -DMOS_SCHED_POLICY="${MOS_SCHED_POLICY:-}" \
        -DMOS_SCHED_DEMO=OFF \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON >/dev/null
    cmake --build "$BUILD_DIR" --target rtos_benchmark.elf -j"$(nproc)"
fi

if [[ ! -f "$ELF" ]]; then
    echo "[error] missing $ELF (run without --no-build)" >&2
    exit 1
fi

{ mos_machine_setup "$ELF" "$LOG"; echo start; } >"$RESC"
rm -f "$LOG"

timeout_s=$((120 + ITERATIONS / 50))
mos_renode_headless "$RESC" "$LOG" "RTOS-BENCHMARK DONE" "$timeout_s" "$RENODE_LOG" || true

echo "================ RTOS-Benchmark ($BOARD) ================"
cat "$LOG" 2>/dev/null || true
echo "==========================================================="
echo "Logs: $RUN_DIR"

if ! grep -q "RTOS-BENCHMARK DONE" "$LOG" 2>/dev/null; then
    echo "[error] benchmark did not finish within ${timeout_s}s" >&2
    tail -30 "$RENODE_LOG" >&2 || true
    exit 1
fi
