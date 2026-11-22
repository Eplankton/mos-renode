#!/bin/bash

# ==============================================================================
# Build Script for mos-renode
#
#   ./build.sh                    interactive board menu
#   ./build.sh <board>            build examples/boards/<board>/main.cpp
#   ./build.sh all                build every board
#   ./build.sh clean              remove build/
#
# Boards are the directories under examples/boards/ (see examples/README.md).
# Artifacts: build/<board>/<board>.{elf,hex,bin}
# ==============================================================================

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"
source "$ROOT/emulation/renode.sh"

THREADS=$(nproc 2>/dev/null || echo 4)

GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

log_info()    { echo -e "${BLUE}[INFO]  $1${NC}"; }
log_success() { echo -e "${GREEN}[DONE]  $1${NC}"; }
log_warn()    { echo -e "${YELLOW}[WARN]  ${1}${NC}"; }
log_error()   { echo -e "${RED}[ERROR] ${1}${NC}"; }

mapfile -t BOARDS < <(mos_boards)

usage() {
    cat <<'EOF'
Usage:
  ./build.sh                    interactive board menu
  ./build.sh <board>            build one board
  ./build.sh all                build every board
  ./build.sh clean              remove build/

Environment (optional):
  MOS_SCHED_POLICY=RR|PP|RM|DM|EDF|COOP
  MOS_SCHED_DEMO=ON             build the periodic scheduling demo
  MOS_THREAD_METRIC=ON          build the Thread-Metric benchmark firmware
  MOS_TM_DURATION=5             Thread-Metric reporting interval (seconds)
  MOS_TM_CYCLES=1               reports before exit (0 = run forever)
  MOS_RTOS_BENCHMARK=ON         build the RTOS-Benchmark firmware
  MOS_RB_ITERATIONS=1000        RTOS-Benchmark iterations per test
  MOS_RB_TIMER_HZ=0             timer clock for ns conversion (0=SystemCoreClock)

Boards:
EOF
    local b
    for b in "${BOARDS[@]}"; do
        mos_load_board "$b"
        printf '  %-16s %s\n' "$b" "$BOARD_TITLE"
    done
}

show_artifacts() {
    local board="$1" file
    echo -e "=========================================${NC}"
    for file in "build/${board}/${board}".{elf,bin,hex}; do
        if [ -f "$file" ]; then
            ls -lh "$file" | awk '{print $9 " \t(" $5 ")"}'
        else
            log_warn "Missing: $file"
        fi
    done
    echo -e "=========================================${NC}"
}

build_board() {
    local board="$1"
    local build_dir="build/${board}"

    log_info "Configuring ${board}..."
    # Always pass the scheduler options explicitly so a stale CMake cache
    # from an experiment cannot silently change the default firmware.
    #   MOS_SCHED_POLICY=EDF MOS_SCHED_DEMO=ON ./build.sh ...
    cmake -S . -B "$build_dir" \
        -DMOS_BOARD="$board" \
        -DMOS_SCHED_POLICY="${MOS_SCHED_POLICY:-}" \
        -DMOS_SCHED_DEMO="${MOS_SCHED_DEMO:-OFF}" \
        -DMOS_THREAD_METRIC="${MOS_THREAD_METRIC:-OFF}" \
        -DMOS_TM_DURATION="${MOS_TM_DURATION:-5}" \
        -DMOS_TM_CYCLES="${MOS_TM_CYCLES:-1}" \
        -DMOS_RTOS_BENCHMARK="${MOS_RTOS_BENCHMARK:-OFF}" \
        -DMOS_RB_ITERATIONS="${MOS_RB_ITERATIONS:-1000}" \
        -DMOS_RB_TIMER_HZ="${MOS_RB_TIMER_HZ:-0}" \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

    log_info "Building ${board} with ${THREADS} threads..."
    cmake --build "$build_dir" --parallel "$THREADS"

    echo -e "\n=========================================${NC}"
    log_success "Build Successful: ${board}"
    show_artifacts "$board"
}

build_all() {
    local b
    for b in "${BOARDS[@]}"; do
        build_board "$b"
    done
    log_success "All boards built."
}

clean_build() {
    log_info "Cleaning build directory..."
    rm -rf build
    log_success "Cleaned."
}

menu() {
    echo -e "${BLUE}=========================================${NC}"
    echo -e "${BLUE}  MOS build targets${NC}"
    echo -e "${BLUE}=========================================${NC}"

    local n=${#BOARDS[@]} i choice
    while true; do
        for i in "${!BOARDS[@]}"; do
            mos_load_board "${BOARDS[$i]}"
            printf '  %d) %-28s (%s)\n' $((i + 1)) "$BOARD_TITLE" "${BOARDS[$i]}"
        done
        printf '  %d) Build every board\n  %d) Clean build directory\n  %d) Exit\n' \
            $((n + 1)) $((n + 2)) $((n + 3))
        read -r -p "Select: " choice || return 0

        if [[ "$choice" =~ ^[0-9]+$ ]] && ((choice >= 1 && choice <= n)); then
            build_board "${BOARDS[$((choice - 1))]}"; return 0
        fi
        case "$choice" in
            $((n + 1))) build_all; return 0 ;;
            $((n + 2))) clean_build; return 0 ;;
            $((n + 3))) return 0 ;;
            *) echo "Invalid selection." ;;
        esac
    done
}

if ! command -v cmake &> /dev/null; then
    log_error "cmake could not be found. Please install it."
    exit 1
fi

if [ "$#" -eq 0 ]; then
    if [ -t 0 ]; then
        menu
        exit 0
    fi
    usage
    exit 1
fi

case "$1" in
    clean) clean_build ;;
    all) build_all ;;
    -h|--help) usage ;;
    *)
        if mos_is_board "$1"; then
            build_board "$1"
        else
            log_error "Unknown target '$1'"
            usage
            exit 1
        fi
        ;;
esac
