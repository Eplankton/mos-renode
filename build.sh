#!/bin/bash

# ==============================================================================
# Build Script for mos-renode
# ==============================================================================

# Stop on error, unset variables, and pipe failures
set -euo pipefail

# Project Configurations
BUILD_DIR="build"
ARTIFACT_NAME="mos-renode"
THREADS=$(nproc 2>/dev/null || echo 4)

# UI Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

# Helper functions for logging
log_info()  { echo -e "${BLUE}[INFO]  $1${NC}"; }
log_success() { echo -e "${GREEN}[DONE]  $1${NC}"; }
log_warn() { echo -e "${YELLOW}[WARN]  $1${NC}"; }
log_error() { echo -e "${RED}[ERROR] $1${NC}"; }

# Clean logic
if [ "${1:-}" == "clean" ]; then
    log_info "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
    log_success "Cleaned."
    exit 0
fi

# Environment Check
if ! command -v cmake &> /dev/null; then
    log_error "cmake could not be found. Please install it."
    exit 1
fi

# Create build directory
mkdir -p "$BUILD_DIR"

# CMake configuration
# Use -S and -B to avoid 'cd' (Cleaner path management)
log_info "Configuring CMake..."
cmake -S . -B "$BUILD_DIR" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Compilation
log_info "Building project with $THREADS threads..."
cmake --build "$BUILD_DIR" --parallel "$THREADS"

# Display results
echo -e "\n=========================================${NC}"
log_success "Build Successful!"
echo -e "=========================================${NC}"

# Check for artifacts and display size
FILES=("${BUILD_DIR}/${ARTIFACT_NAME}.elf" "${BUILD_DIR}/${ARTIFACT_NAME}.bin" "${BUILD_DIR}/${ARTIFACT_NAME}.hex")
for FILE in "${FILES[@]}"; do
    if [ -f "$FILE" ]; then
        ls -lh "$FILE" | awk '{print $9 " \t(" $5 ")"}'
    else
        log_warn "Missing: $(basename "$FILE")"
    fi
done
echo -e "=========================================${NC}"