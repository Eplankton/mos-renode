#!/bin/bash

# ==============================================================================
# Build Script for mos-renode
#
#   ./build.sh                    interactive board menu
#   ./build.sh stm32f4-disco      build examples/boards/stm32f4-disco/main.cpp
#   ./build.sh stm32h7-disco    build examples/boards/stm32h7-disco/main.cpp
#   ./build.sh all                build both boards
#   ./build.sh clean              remove build/
#
# Artifacts:
#   build/<board>/<board>.{elf,hex,bin}
# ==============================================================================

set -euo pipefail

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

usage() {
    cat <<'EOF'
Usage:
  ./build.sh                    interactive board menu
  ./build.sh stm32f4-disco      build STM32F4 Discovery
  ./build.sh stm32h7-disco    build STM32H747I-DISCO
  ./build.sh all                build both boards
  ./build.sh clean              remove build/

Boards: stm32f4-disco | stm32h7-disco

Environment (optional):
  MOS_SCHED_POLICY=RoundRobin|PreemptPri|RateMonotonic|
                   DeadlineMonotonic|EarliestDeadlineFirst|Cooperative
  MOS_SCHED_DEMO=ON             build the periodic scheduling demo
EOF
}

is_board() {
    case "$1" in
        stm32f4-disco|stm32h7-disco) return 0 ;;
        *) return 1 ;;
    esac
}

show_artifacts() {
    local board="$1"
    local files=(
        "build/${board}/${board}.elf"
        "build/${board}/${board}.bin"
        "build/${board}/${board}.hex"
    )

    echo -e "=========================================${NC}"
    for file in "${files[@]}"; do
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
    #   MOS_SCHED_POLICY=EarliestDeadlineFirst MOS_SCHED_DEMO=ON ./build.sh ...
    cmake -S . -B "$build_dir" \
        -DMOS_BOARD="$board" \
        -DMOS_SCHED_POLICY="${MOS_SCHED_POLICY:-}" \
        -DMOS_SCHED_DEMO="${MOS_SCHED_DEMO:-OFF}" \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

    log_info "Building ${board} with ${THREADS} threads..."
    cmake --build "$build_dir" --parallel "$THREADS"

    echo -e "\n=========================================${NC}"
    log_success "Build Successful: ${board}"
    show_artifacts "$board"
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

    while true; do
        printf '%s\n' \
            "  1) STM32F4 Discovery   (stm32f4-disco)" \
            "  2) STM32H747I-DISCO    (stm32h7-disco)" \
            "  3) Build both boards" \
            "  4) Clean build directory" \
            "  5) Exit"
        read -r -p "Select: " choice || return 0

        case "$choice" in
            1) build_board stm32f4-disco; return 0 ;;
            2) build_board stm32h7-disco; return 0 ;;
            3)
                build_board stm32f4-disco
                build_board stm32h7-disco
                log_success "Both boards built."
                return 0
                ;;
            4) clean_build; return 0 ;;
            5) return 0 ;;
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
    all)
        build_board stm32f4-disco
        build_board stm32h7-disco
        log_success "Both boards built."
        ;;
    *)
        if is_board "$1"; then
            build_board "$1"
        else
            log_error "Unknown target '$1' (use stm32f4-disco / stm32h7-disco / all / clean)"
            usage
            exit 1
        fi
        ;;
esac
