#!/bin/bash

# Stop immediately on error
set -e

# Define paths
BUILD_DIR="build"
ARTIFACT_NAME="mos-renode"

# Color definitions
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# If argument is "clean", remove build directory and exit
if [ "$1" == "clean" ]; then
    echo -e "${BLUE}[INFO] Cleaning build directory...${NC}"
    rm -rf "$BUILD_DIR"
    echo -e "${GREEN}[DONE] Cleaned.${NC}"
    exit 0
fi

# 1. Create build directory
if [ ! -d "$BUILD_DIR" ]; then
    echo -e "${BLUE}[INFO] Creating build directory...${NC}"
    mkdir -p "$BUILD_DIR"
fi

cd "$BUILD_DIR"

# 2. CMake configuration
# -DCMAKE_EXPORT_COMPILE_COMMANDS=ON generates compile_commands.json in build directory
echo -e "${BLUE}[INFO] Configuring CMake...${NC}"
cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ..

# 3. Start compilation
echo -e "${BLUE}[INFO] Building project...${NC}"
cmake --build . -- -j$(nproc)

# 4. Display build results
echo -e "========================================="
echo -e "${GREEN}[DONE] Build Success!${NC}"
echo -e "========================================="
ls -lh ${ARTIFACT_NAME}.elf ${ARTIFACT_NAME}.bin ${ARTIFACT_NAME}.hex 2>/dev/null || true
echo -e "========================================="