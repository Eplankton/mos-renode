#!/usr/bin/env bash
# ==============================================================================
# Headless kernel self-test (Renode)
#
# Builds the board example for every scheduling policy, runs it in Renode and
# collects the `[TEST] ... PASS/FAIL` lines printed by examples/common/kernel_tests.hpp.
# Exit status is non-zero if any test fails or a run does not finish.
#
#   ./emulation/selftest.sh                           all boards, all policies
#   ./emulation/selftest.sh hifive1                   one board
#   ./emulation/selftest.sh --policies PreemptPri,EarliestDeadlineFirst
#   ./emulation/selftest.sh --no-build
#
# Builds go to build/selftest/<board>/<policy>/, logs next to them.
# ==============================================================================

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
source "$ROOT/emulation/renode.sh"

ALL_POLICIES=(RoundRobin PreemptPri RateMonotonic DeadlineMonotonic EarliestDeadlineFirst Cooperative)

BOARDS=()
POLICIES=("${ALL_POLICIES[@]}")
DO_BUILD=1
TIMEOUT=90
DONE_MARKER="Scheduling policy tests done"

usage() {
    sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'
    echo "Boards:"
    local b
    for b in $(mos_boards); do
        mos_load_board "$b"
        printf '  %-16s %s\n' "$b" "$BOARD_TITLE"
    done
}

while (($#)); do
    case "$1" in
        --policies) IFS=',' read -r -a POLICIES <<<"${2:?missing policy list}"; shift ;;
        --timeout) TIMEOUT="${2:?missing seconds}"; shift ;;
        --no-build) DO_BUILD=0 ;;
        -h|--help) usage; exit 0 ;;
        *)
            if mos_is_board "$1"; then
                BOARDS+=("$1")
            else
                echo "Unknown argument: $1" >&2; usage >&2; exit 1
            fi
            ;;
    esac
    shift
done
((${#BOARDS[@]})) || mapfile -t BOARDS < <(mos_boards)

command -v renode >/dev/null || { echo "[error] renode not found in PATH" >&2; exit 1; }

run_one() {
    local board="$1" policy="$2"
    local dir="$ROOT/build/selftest/$board/$policy"
    local elf="$dir/$board.elf" log="$dir/uart.log"

    mos_load_board "$board"
    mkdir -p "$dir"

    if ((DO_BUILD)); then
        cmake -S "$ROOT" -B "$dir" -DMOS_BOARD="$board" \
            -DMOS_SCHED_POLICY="$policy" -DMOS_SCHED_DEMO=OFF \
            -DMOS_THREAD_METRIC=OFF -DMOS_RTOS_BENCHMARK=OFF >/dev/null
        cmake --build "$dir" --target "$board.elf" -j"$(nproc)" >"$dir/build.log" 2>&1 || {
            echo "  build failed, see $dir/build.log"
            return 1
        }
    fi

    { mos_machine_setup "$elf" "$log"; echo start; } >"$dir/run.resc"
    rm -f "$log"

    local finished=0
    mos_renode_headless "$dir/run.resc" "$log" "$DONE_MARKER" "$TIMEOUT" "$dir/renode.log" && finished=1

    grep -a "\[TEST\]" "$log" 2>/dev/null | sed 's/^/  /' || true
    if ((!finished)); then
        echo "  TIMEOUT (log: $log)"
        return 1
    fi
    ! grep -aq "\[TEST\].*FAIL" "$log"
}

failed=()
for board in "${BOARDS[@]}"; do
    for policy in "${POLICIES[@]}"; do
        echo "== $board / $policy"
        run_one "$board" "$policy" || failed+=("$board/$policy")
    done
done

echo
if ((${#failed[@]})); then
    echo "FAILED: ${failed[*]}"
    exit 1
fi
echo "All self-tests passed."
