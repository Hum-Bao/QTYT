#!/usr/bin/env bash
# LightYT Automated Linux Build Script
# Mirrors build.ps1 lifecycle, options, and dependency handling.
# NEVER downloads, updates, or installs external binaries from the web.

set -euo pipefail

# Script directory is repository root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

CLEAN=0
RUN_TESTS=0
NO_RUN=0
BUILD_TYPE="Release"
QT_PREFIX=""

print_usage() {
    cat << 'EOF'
LightYT Linux Builder & Test Runner
Usage: ./build.sh [OPTIONS]

Options:
  -c, --clean         Clean and wipe the build directory before building
  -t, --test          Execute CTest unit tests after compilation
  -n, --no-run        Do not launch QTYT after compilation
  --prefix <path>     Explicit CMAKE_PREFIX_PATH to Qt 6 installation
  --build-type <type> Build type: Release (default), Debug, RelWithDebInfo
  -h, --help          Show this help message
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -c|--clean)
            CLEAN=1
            shift
            ;;
        -t|--test)
            RUN_TESTS=1
            shift
            ;;
        -n|--no-run)
            NO_RUN=1
            shift
            ;;
        --prefix)
            if [[ $# -lt 2 ]]; then
                echo -e "\033[1;31m[ERROR] --prefix requires a directory argument.\033[0m" >&2
                exit 1
            fi
            QT_PREFIX="$2"
            shift 2
            ;;
        --build-type)
            if [[ $# -lt 2 ]]; then
                echo -e "\033[1;31m[ERROR] --build-type requires an argument.\033[0m" >&2
                exit 1
            fi
            BUILD_TYPE="$2"
            shift 2
            ;;
        -h|--help)
            print_usage
            exit 0
            ;;
        *)
            echo -e "\033[1;31m[ERROR] Unknown option: $1\033[0m" >&2
            print_usage
            exit 1
            ;;
    esac
done

echo -e "\033[1;36m==========================================\033[0m"
echo -e "\033[1;36m          LightYT Linux Builder           \033[0m"
echo -e "\033[1;36m==========================================\033[0m"

# 1. Dependency and compiler checks
if ! command -v cmake &>/dev/null; then
    echo -e "\033[1;31m[ERROR] CMake is not installed or not in PATH.\033[0m" >&2
    exit 1
fi

COMPILER=""
if command -v g++ &>/dev/null; then
    COMPILER="$(command -v g++)"
elif command -v clang++ &>/dev/null; then
    COMPILER="$(command -v clang++)"
else
    echo -e "\033[1;31m[ERROR] No C++20 compiler (g++ or clang++) found in PATH.\033[0m" >&2
    exit 1
fi
echo -e "\033[1;32m[Compiler] Using: $COMPILER\033[0m"

# 2. Check for runtime streaming dependencies (lib/ or system)
mkdir -p lib

# yt-dlp check
if [[ -f "lib/yt-dlp" && -x "lib/yt-dlp" ]]; then
    echo -e "\033[1;32m[Dependencies] yt-dlp backend found in lib/.\033[0m"
elif command -v yt-dlp &>/dev/null; then
    echo -e "\033[1;32m[Dependencies] yt-dlp backend found on system PATH ($(command -v yt-dlp)).\033[0m"
else
    echo -e "\033[1;33m[Dependencies] Notice: yt-dlp not found in lib/ or system PATH.\033[0m"
    echo -e "\033[1;33m[Dependencies] Video streaming requires yt-dlp.\033[0m"
fi

# mpv / libmpv check
if [[ -f "lib/libmpv.so" || -f "lib/libmpv.so.2" || -f "lib/mpv" ]]; then
    echo -e "\033[1;32m[Dependencies] Media engine found in lib/.\033[0m"
elif ldconfig -p 2>/dev/null | grep -q "libmpv\.so" || command -v mpv &>/dev/null; then
    echo -e "\033[1;32m[Dependencies] Media engine found on system.\033[0m"
else
    echo -e "\033[1;33m[Dependencies] Notice: libmpv or mpv not found. Place libmpv.so or mpv in lib/ or install via package manager.\033[0m"
fi

# 3. Handle build directory
if [[ $CLEAN -eq 1 ]]; then
    if [[ -d "build" ]]; then
        echo -e "\033[1;33m[Clean] Removing build directory...\033[0m"
        rm -rf build
    fi
fi
mkdir -p build

# 4. CMake Configure & Build
CMAKE_ARGS=("-B" "build" "-S" "." "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
if [[ -n "$QT_PREFIX" ]]; then
    CMAKE_ARGS+=("-DCMAKE_PREFIX_PATH=${QT_PREFIX}")
fi

echo -e "\033[1;36m[CMake] Configuring project...\033[0m"
cmake "${CMAKE_ARGS[@]}"

echo -e "\033[1;36m[Build] Compiling QTYT in parallel...\033[0m"
cmake --build build -j"$(nproc)"

# 6. Run Tests if requested
if [[ $RUN_TESTS -eq 1 ]]; then
    echo -e "\033[1;36m[Test] Running unit tests via ctest...\033[0m"
    ctest --test-dir build --output-on-failure
fi

# 7. Launch application
if [[ $NO_RUN -eq 0 ]]; then
    if [[ -x "build/QTYT" ]]; then
        echo -e "\033[1;32m[Launch] Starting QTYT...\033[0m"
        exec ./build/QTYT
    else
        echo -e "\033[1;33m[Launch] build/QTYT executable not found (or GUI was disabled). CLI tool available at build/lightyt-cli\033[0m"
    fi
fi
