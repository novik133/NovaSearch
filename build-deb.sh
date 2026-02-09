#!/bin/bash
set -e

echo "========================================"
echo "NovaSearch Debian Package Builder"
echo "========================================"
echo ""

# Check if required tools are installed
echo "Checking dependencies..."
MISSING_DEPS=""

if ! command -v dpkg-buildpackage &> /dev/null; then
    MISSING_DEPS="$MISSING_DEPS dpkg-dev"
fi

if ! command -v debhelper &> /dev/null; then
    MISSING_DEPS="$MISSING_DEPS debhelper"
fi

if ! command -v meson &> /dev/null; then
    MISSING_DEPS="$MISSING_DEPS meson"
fi

if ! command -v cargo &> /dev/null; then
    MISSING_DEPS="$MISSING_DEPS cargo"
fi

if [ -n "$MISSING_DEPS" ]; then
    echo "Error: Missing required dependencies:$MISSING_DEPS"
    echo ""
    echo "Install them with:"
    echo "  sudo apt-get install$MISSING_DEPS"
    exit 1
fi

echo "All dependencies found!"
echo ""

# Clean previous builds
echo "Cleaning previous builds..."
rm -rf builddir debian/novasearch debian/.debhelper debian/files debian/*.substvars
rm -f ../novasearch_*.deb ../novasearch_*.buildinfo ../novasearch_*.changes
rm -f ../novasearch_*.dsc ../novasearch_*.tar.xz

# Build the package
echo "Building Debian package..."
echo ""

dpkg-buildpackage -us -uc -b

echo ""
echo "========================================"
echo "Build Complete!"
echo "========================================"
echo ""

# Find and display the package
DEB_FILE=$(ls -t ../novasearch_*.deb 2>/dev/null | head -1)

if [ -n "$DEB_FILE" ]; then
    echo "Package created: $DEB_FILE"
    echo ""
    echo "Package information:"
    dpkg-deb -I "$DEB_FILE" | grep -E "Package:|Version:|Architecture:|Description:" | head -4
    echo ""
    echo "To install:"
    echo "  sudo dpkg -i $DEB_FILE"
    echo "  sudo apt-get install -f  # Install any missing dependencies"
    echo ""
    echo "Or copy to another system and install there."
else
    echo "Error: Package file not found!"
    exit 1
fi
