#!/usr/bin/env bash
# ==============================================================================
# Thread-Metric benchmark runner (Renode)
#
#   ./benchmarks/thread_metric/run.sh [board] [options]
#
#   board: any board under examples/boards/ with benchmark support (default stm32f4-disco)
#   options:
#     --duration N   reporting interval in seconds (default 5)
#     --cycles N     reports before exit, 0 = keep running (default 1)
#     --tests a,b    subset of tests, default: all
#     --no-build     reuse existing ELFs
#
# Results are written next to the ELFs under build/<board>/thread_metric/.
# ==============================================================================

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
source "$ROOT/emulation/renode.sh"

BOARD="stm32f4-disco"
DURATION=5
CYCLES=1
DO_BUILD=1

ALL_TESTS=(
    basic_processing
    cooperative_scheduling
    preemptive_scheduling
    interrupt_processing
    interrupt_preemption_processing
    message_processing
    synchronization_processing
    memory_allocation
)
TESTS=("${ALL_TESTS[@]}")

usage() {
    cat <<'EOF'
Usage:
  ./benchmarks/thread_metric/run.sh [board] [options]

Options:
  --duration N    Thread-Metric reporting interval in seconds (default 5)
  --cycles N      Reports before exit, 0 = run until stopped (default 1)
  --tests a,b     Comma-separated subset of:
                  basic_processing, cooperative_scheduling,
                  preemptive_scheduling, interrupt_processing,
                  interrupt_preemption_processing, message_processing,
                  synchronization_processing, memory_allocation
  --no-build      Reuse existing build/<board>/tm_*.elf files
  -h, --help      Show this help
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
        --duration) DURATION="${2:?missing duration}"; shift ;;
        --cycles) CYCLES="${2:?missing cycles}"; shift ;;
        --tests)
            IFS=',' read -r -a TESTS <<< "${2:?missing test list}"
            shift
            ;;
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


for test in "${TESTS[@]}"; do
    found=0
    for known in "${ALL_TESTS[@]}"; do
        [[ "$test" == "$known" ]] && found=1
    done
    if ((!found)); then
        echo "Unknown test: $test" >&2
        exit 1
    fi
done

if ! [[ "$DURATION" =~ ^[1-9][0-9]*$ ]]; then
    echo "--duration must be a positive integer" >&2
    exit 1
fi
if ! [[ "$CYCLES" =~ ^[0-9]+$ ]]; then
    echo "--cycles must be zero or a positive integer" >&2
    exit 1
fi

if ! command -v renode >/dev/null 2>&1; then
    echo "[error] renode not found in PATH" >&2
    exit 1
fi

BUILD_DIR="$ROOT/build/$BOARD"
RUN_DIR="$BUILD_DIR/thread_metric"
mkdir -p "$RUN_DIR"

if ((DO_BUILD)); then
    echo "[build] $BOARD (Thread-Metric)"
    cmake -S "$ROOT" -B "$BUILD_DIR" \
        -DMOS_BOARD="$BOARD" \
        -DMOS_THREAD_METRIC=ON \
        -DMOS_TM_DURATION="$DURATION" \
        -DMOS_TM_CYCLES="$CYCLES" \
        -DMOS_SCHED_POLICY="${MOS_SCHED_POLICY:-}" \
        -DMOS_SCHED_DEMO=OFF \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON >/dev/null

    targets=()
    for test in "${TESTS[@]}"; do
        targets+=("tm_${test}.elf")
    done
    cmake --build "$BUILD_DIR" --target "${targets[@]}" -j"$(nproc)"
fi

declare -a RESULTS=()
FAILED=0

for test in "${TESTS[@]}"; do
    elf="$BUILD_DIR/tm_${test}.elf"
    log="$RUN_DIR/${test}.uart.log"
    resc="$RUN_DIR/${test}.resc"
    renode_log="$RUN_DIR/${test}.renode.log"

    if [[ ! -f "$elf" ]]; then
        echo "[error] missing $elf (run without --no-build)" >&2
        RESULTS+=("$test|0|0|0|MISSING")
        FAILED=1
        continue
    fi

    { mos_machine_setup "$elf" "$log"; echo start; } >"$resc"
    rm -f "$log"

    timeout_s=$((DURATION * 4 + 30))
    if ((CYCLES > 1)); then
        timeout_s=$((DURATION * CYCLES * 4 + 30))
    fi

    # One report and exit, or (--cycles 0) the first complete report
    marker="THREAD-METRIC DONE"
    ((CYCLES == 0)) && marker="Time Period Total:"

    if ! mos_renode_headless "$resc" "$log" "$marker" "$timeout_s" "$renode_log"; then
        echo "[warn] $test timed out after ${timeout_s}s" >&2
        tail -20 "$renode_log" >&2 || true
    fi

    total=$(awk -F: '/Time Period Total:/ {v=$2} END {gsub(/[ \r]/, "", v); print v}' "$log" 2>/dev/null || true)
    relative=$(awk -F: '/Relative Time:/ {v=$2} END {gsub(/[ \r]/, "", v); print v}' "$log" 2>/dev/null || true)

    status=PASS
    if grep -q "ERROR:" "$log" 2>/dev/null; then
        status=FAIL
    fi
    if grep -q "THREAD-METRIC DONE (code=1)" "$log" 2>/dev/null; then
        status=FAIL
    fi
    if [[ -z "$total" || -z "$relative" ]]; then
        status=FAIL
    fi

    if [[ "$status" == "PASS" ]]; then
        ops=$(awk -v n="$total" -v d="$relative" 'BEGIN {
            if (d > 0) printf "%.0f", n / d; else printf "0";
        }')
    else
        ops=0
    fi

    RESULTS+=("$test|${total:-0}|${relative:-0}|$ops|$status")

    echo "================ $test ================"
    cat "$log" 2>/dev/null || true
    echo
done

echo "============================================================"
printf '%-36s %14s %14s %8s\n' "Test" "Operations" "Ops/second" "Status"
echo "------------------------------------------------------------"
for row in "${RESULTS[@]}"; do
    IFS='|' read -r test total relative ops status <<< "$row"
    printf '%-36s %14s %14s %8s\n' "$test" "$total" "$ops" "$status"
done
echo "============================================================"
echo "Logs: $RUN_DIR"

{
    echo "test,operations,seconds,ops_per_second,status"
    for row in "${RESULTS[@]}"; do
        echo "${row//|/,}"
    done
} > "$RUN_DIR/results.csv"
echo "CSV : $RUN_DIR/results.csv"

if ((FAILED)); then
    exit 1
fi

for row in "${RESULTS[@]}"; do
    IFS='|' read -r _ _ _ _ status <<< "$row"
    if [[ "$status" != "PASS" ]]; then
        exit 1
    fi
done
