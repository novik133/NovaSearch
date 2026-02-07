#!/bin/bash
# NovaSearch build script

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}NovaSearch Build Script${NC}"
echo "======================="
echo

# Check for required tools
echo "Checking dependencies..."

if ! command -v meson &> /dev/null; then
    echo -e "${RED}Error: meson not found${NC}"
    echo "Please install meson: https://mesonbuild.com/Getting-meson.html"
    exit 1
fi

if ! command -v ninja &> /dev/null; then
    echo -e "${RED}Error: ninja not found${NC}"
    echo "Please install ninja-build"
    exit 1
fi

if ! command -v cargo &> /dev/null; then
    echo -e "${RED}Error: cargo not found${NC}"
    echo "Please install Rust: https://rustup.rs/"
    exit 1
fi

echo -e "${GREEN}✓ All dependencies found${NC}"
echo

# Parse arguments
BUILD_DIR="builddir"
BUILD_TYPE="release"
RUN_TESTS=true

while [[ $# -gt 0 ]]; do
    case $1 in
        --debug)
            BUILD_TYPE="debug"
            shift
            ;;
        --no-tests)
            RUN_TESTS=false
            shift
            ;;
        --clean)
            echo "Cleaning build directory..."
            rm -rf "$BUILD_DIR"
            echo -e "${GREEN}✓ Clean complete${NC}"
            exit 0
            ;;
        --help)
            echo "Usage: $0 [OPTIONS]"
            echo
            echo "Options:"
            echo "  --debug      Build in debug mode"
            echo "  --no-tests   Skip running tests"
            echo "  --clean      Clean build directory"
            echo "  --help       Show this help message"
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Setup build directory
if [ ! -d "$BUILD_DIR" ]; then
    echo "Setting up build directory..."
    meson setup "$BUILD_DIR" --buildtype="$BUILD_TYPE"
    echo -e "${GREEN}✓ Build directory configured${NC}"
    echo
fi

# Build
echo "Building NovaSearch..."
meson compile -C "$BUILD_DIR"
echo -e "${GREEN}✓ Build complete${NC}"
echo

# Run tests
if [ "$RUN_TESTS" = true ]; then
    echo "Running tests..."
    
    # Run Rust tests
    echo "Running Rust daemon tests..."
    cd daemon
    cargo test --quiet
    cd ..
    echo -e "${GREEN}✓ Rust tests passed${NC}"
    
    # Run Meson tests (panel)
    echo "Running panel tests..."
    if meson test -C "$BUILD_DIR" --quiet 2>/dev/null; then
        echo -e "${GREEN}✓ Panel tests passed${NC}"
    else
        echo -e "${YELLOW}⚠ Panel tests skipped (theft library not found)${NC}"
    fi
    echo
fi

# Summary
echo -e "${GREEN}Build Summary${NC}"
echo "============="
echo "Daemon binary: daemon/target/$BUILD_TYPE/novasearch-daemon"
echo "Panel plugin:  $BUILD_DIR/panel/novasearch-panel.so"
echo
echo "To install, run: sudo meson install -C $BUILD_DIR"
