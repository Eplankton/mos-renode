#!/usr/bin/env bash
# ==============================================================================
# MOS vs FreeRTOS on the same Renode machine
#
#   ./benchmarks/compare.sh [board] [options]
#
# Builds Thread-Metric and RTOS-Benchmark for both kernels from one CMake
# configuration (same board sources, compiler flags, tick, timing code and
# benchmark sources), runs every firmware headless in Renode and prints the
# results side by side as Markdown. See benchmarks/README.md.
#
#   board: a board with BOARD_FREERTOS in board.env (default stm32f4-disco)
#   options:
#     --duration N     Thread-Metric reporting interval in seconds (default 5)
#     --iterations N   RTOS-Benchmark samples per test (default 1000)
#     --assert         keep kernel assertions (default: off in both kernels)
#     --jobs N         Renode instances in parallel (default 4)
#     --no-build       reuse the existing ELFs
#
# Output: build/<board>-compare/compare.md, logs next to it.
# ==============================================================================

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
source "$ROOT/emulation/renode.sh"

BOARD=stm32f4-disco
DURATION=5
ITERATIONS=1000
ASSERT=OFF
JOBS=4
DO_BUILD=1

TM_TESTS=(
    basic_processing
    cooperative_scheduling
    preemptive_scheduling
    interrupt_processing
    interrupt_preemption_processing
    message_processing
    synchronization_processing
    memory_allocation
)

usage() {
    sed -n '4,20p' "$0" | sed 's/^# \{0,1\}//'
}

while (($#)); do
    case "$1" in
        --duration) DURATION="${2:?missing duration}"; shift ;;
        --iterations) ITERATIONS="${2:?missing iterations}"; shift ;;
        --assert) ASSERT=ON ;;
        --jobs) JOBS="${2:?missing job count}"; shift ;;
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
if [[ -z "$BOARD_FREERTOS" ]]; then
    echo "[error] $BOARD has no FreeRTOS reference build (BOARD_FREERTOS in board.env)" >&2
    exit 1
fi
command -v renode >/dev/null || { echo "[error] renode not found in PATH" >&2; exit 1; }

BUILD_DIR="$ROOT/build/$BOARD-compare"
RUN_DIR="$BUILD_DIR/results"
mkdir -p "$RUN_DIR"

if ((DO_BUILD)); then
    echo "[build] $BOARD: MOS + FreeRTOS, assertions $ASSERT"
    cmake -S "$ROOT" -B "$BUILD_DIR" \
        -DMOS_BOARD="$BOARD" \
        -DMOS_THREAD_METRIC=ON \
        -DMOS_RTOS_BENCHMARK=ON \
        -DMOS_BENCH_FREERTOS=ON \
        -DMOS_BENCH_ASSERT="$ASSERT" \
        -DMOS_TM_DURATION="$DURATION" \
        -DMOS_TM_CYCLES=1 \
        -DMOS_RB_ITERATIONS="$ITERATIONS" \
        -DMOS_RB_TIMER_HZ="$BOARD_RB_TIMER_HZ" \
        -DMOS_SCHED_POLICY=PreemptPri \
        -DMOS_SCHED_DEMO=OFF >/dev/null

    targets=(rtos_benchmark.elf rtos_benchmark_freertos.elf)
    for t in "${TM_TESTS[@]}"; do
        targets+=("tm_$t.elf" "tm_freertos_$t.elf")
    done
    cmake --build "$BUILD_DIR" --target "${targets[@]}" -j"$(nproc)" >"$RUN_DIR/build.log" 2>&1 || {
        tail -30 "$RUN_DIR/build.log" >&2
        exit 1
    }
fi

# run_one <elf name without .elf> <marker> <timeout>
run_one() {
    local name="$1" marker="$2" timeout="$3"
    local log="$RUN_DIR/$name.uart.log"
    rm -f "$log"
    { mos_machine_setup "$BUILD_DIR/$name.elf" "$log"; echo start; } >"$RUN_DIR/$name.resc"
    if mos_renode_headless "$RUN_DIR/$name.resc" "$log" "$marker" "$timeout" "$RUN_DIR/$name.renode.log"; then
        echo "  $name"
    else
        echo "  $name: TIMEOUT" >&2
    fi
}
export -f run_one mos_machine_setup mos_renode_headless
export BUILD_DIR RUN_DIR BOARD_RESC_DIR BOARD_SHELL_UART

echo "[run] $((${#TM_TESTS[@]} * 2 + 2)) firmwares, $JOBS at a time"
{
    echo "rtos_benchmark|RTOS-BENCHMARK DONE|600"
    echo "rtos_benchmark_freertos|RTOS-BENCHMARK DONE|600"
    for t in "${TM_TESTS[@]}"; do
        echo "tm_$t|THREAD-METRIC DONE|$((DURATION * 4 + 30))"
        echo "tm_freertos_$t|THREAD-METRIC DONE|$((DURATION * 4 + 30))"
    done
} | xargs -P "$JOBS" -I{} bash -c 'IFS="|" read -r n m t <<< "{}"; run_one "$n" "$m" "$t"'

# ------------------------------------------------------------------------------
# Report
# ------------------------------------------------------------------------------
tm_ops() { # Thread-Metric operations per second from a log
    awk -v d="$DURATION" -F: '/Time Period Total:/ {v=$2} END {gsub(/[ \r]/, "", v); if (v != "") printf "%d", v / d}' "$1" 2>/dev/null
}
rb_avg() { # RTOS-Benchmark average for row "$2" from a log
    awk -v row="$2" -F: '{name=$1; gsub(/^[ \t\r]+|[ \t\r]+$/, "", name)} name == row {split($2, a, ","); gsub(/ /, "", a[1]); print a[1]; exit}' "$1" 2>/dev/null
}
ratio() { awk -v a="$1" -v b="$2" 'BEGIN { if (a > 0 && b > 0) printf "%.2f", a / b; else printf "-" }'; }

REPORT="$BUILD_DIR/compare.md"
{
    echo "<!-- benchmarks/compare.sh $BOARD: Renode $(renode --version 2>/dev/null | grep -o '[0-9][0-9.]*' | head -1), assertions $ASSERT, Thread-Metric ${DURATION} s, RTOS-Benchmark $ITERATIONS iterations -->"
    echo
    echo "| Thread-Metric (ops/s, higher is better) | MOS | FreeRTOS | MOS / FreeRTOS |"
    echo "|---|---:|---:|---:|"
    for t in "${TM_TESTS[@]}"; do
        m=$(tm_ops "$RUN_DIR/tm_$t.uart.log")
        f=$(tm_ops "$RUN_DIR/tm_freertos_$t.uart.log")
        printf '| %s | %s | %s | %s |\n' "$t" "${m:--}" "${f:--}" "$(ratio "${m:-0}" "${f:-0}")"
    done
    echo
    echo "| RTOS-Benchmark (ns, lower is better) | MOS | FreeRTOS | MOS / FreeRTOS |"
    echo "|---|---:|---:|---:|"
    awk -F: '/ : / {name=$1; gsub(/^[ \t\r]+|[ \t\r]+$/, "", name); print name}' "$RUN_DIR/rtos_benchmark.uart.log" | awk '!seen[$0]++' |
    while IFS= read -r row; do
        m=$(rb_avg "$RUN_DIR/rtos_benchmark.uart.log" "$row")
        f=$(rb_avg "$RUN_DIR/rtos_benchmark_freertos.uart.log" "$row")
        printf '| %s | %s | %s | %s |\n' "$row" "${m:--}" "${f:--}" "$(ratio "${m:-0}" "${f:-0}")"
    done
} >"$REPORT"

echo
cat "$REPORT"
echo
echo "Report: $REPORT"
echo "Logs  : $RUN_DIR"
