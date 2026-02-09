#!/bin/bash
set -e

echo "========================================"
echo "NovaSearch Daemon Package Builder"
echo "========================================"
echo ""

# Package information
PACKAGE_NAME="novasearch-daemon"
VERSION="0.1.0"
ARCH=$(dpkg --print-architecture)
PACKAGE_DIR="${PACKAGE_NAME}_${VERSION}-1_${ARCH}"

echo "Building package: ${PACKAGE_DIR}.deb"
echo "Note: This is a daemon-only package (panel plugin requires XFCE4 dev libraries)"
echo ""

# Clean previous builds
echo "Cleaning previous builds..."
rm -rf "$PACKAGE_DIR" "${PACKAGE_DIR}.deb"
rm -rf builddir

# Create package directory structure
echo "Creating package structure..."
mkdir -p "$PACKAGE_DIR/DEBIAN"
mkdir -p "$PACKAGE_DIR/usr/bin"
mkdir -p "$PACKAGE_DIR/usr/lib/systemd/user"
mkdir -p "$PACKAGE_DIR/etc/xdg/novasearch"
mkdir -p "$PACKAGE_DIR/usr/share/doc/novasearch-daemon"

# Build the daemon only
echo "Building NovaSearch daemon..."
meson setup builddir --prefix=/usr --buildtype=release -Dpanel=false
meson compile -C builddir

# Copy daemon binary
echo "Installing files..."
cp builddir/novasearch-daemon "$PACKAGE_DIR/usr/bin/"
chmod 755 "$PACKAGE_DIR/usr/bin/novasearch-daemon"

# Copy systemd service
cp novasearch-daemon.service "$PACKAGE_DIR/usr/lib/systemd/user/"
chmod 644 "$PACKAGE_DIR/usr/lib/systemd/user/novasearch-daemon.service"

# Copy default configuration
cp debian/novasearch.toml "$PACKAGE_DIR/etc/xdg/novasearch/config.toml"
chmod 644 "$PACKAGE_DIR/etc/xdg/novasearch/config.toml"

# Copy documentation
cp README.md "$PACKAGE_DIR/usr/share/doc/novasearch-daemon/"
cp BUILD.md "$PACKAGE_DIR/usr/share/doc/novasearch-daemon/"
cp INSTALL.md "$PACKAGE_DIR/usr/share/doc/novasearch-daemon/"
cp debian/copyright "$PACKAGE_DIR/usr/share/doc/novasearch-daemon/"
chmod 644 "$PACKAGE_DIR/usr/share/doc/novasearch-daemon/"*

# Get installed size
INSTALLED_SIZE=$(du -sk "$PACKAGE_DIR" | cut -f1)

# Create control file
echo "Creating control file..."
cat > "$PACKAGE_DIR/DEBIAN/control" <<EOF
Package: novasearch-daemon
Version: ${VERSION}-1
Section: utils
Priority: optional
Architecture: ${ARCH}
Installed-Size: ${INSTALLED_SIZE}
Depends: libsqlite3-0
Recommends: novasearch-panel
Maintainer: NovaSearch Team <novasearch@example.com>
Description: Fast file indexing daemon for NovaSearch
 NovaSearch daemon provides real-time filesystem indexing using inotify.
 It continuously monitors configured directories and maintains a SQLite
 database for fast file search.
 .
 This package contains only the indexing daemon. For the graphical search
 interface, install the novasearch-panel package (requires XFCE4).
 .
 Features:
  * Real-time indexing with inotify filesystem monitoring
  * Fast SQLite-backed search index
  * Configurable include/exclude patterns with glob support
  * Minimal resource usage (configurable CPU and memory limits)
  * Systemd user service integration
  * CLI for status queries and manual re-indexing
Homepage: https://github.com/novasearch/novasearch
EOF

# Create postinst script
echo "Creating postinst script..."
cat > "$PACKAGE_DIR/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e

case "$1" in
    configure)
        # Reload systemd user daemon for all logged-in users
        for user_id in $(loginctl list-users --no-legend 2>/dev/null | awk '{print $1}'); do
            user_name=$(loginctl show-user "$user_id" -p Name --value 2>/dev/null)
            if [ -n "$user_name" ]; then
                su - "$user_name" -c "systemctl --user daemon-reload" 2>/dev/null || true
            fi
        done
        
        echo ""
        echo "NovaSearch daemon installed successfully!"
        echo ""
        echo "To get started:"
        echo "  1. Enable the daemon: systemctl --user enable --now novasearch-daemon"
        echo "  2. Check status: novasearch-daemon status"
        echo "  3. View logs: journalctl --user -u novasearch-daemon -f"
        echo ""
        echo "Configuration: ~/.config/novasearch/config.toml"
        echo "Database: ~/.local/share/novasearch/index.db"
        echo ""
        echo "For the graphical search interface, install novasearch-panel (requires XFCE4)"
        echo ""
        ;;
esac

exit 0
EOF
chmod 755 "$PACKAGE_DIR/DEBIAN/postinst"

# Create prerm script
echo "Creating prerm script..."
cat > "$PACKAGE_DIR/DEBIAN/prerm" <<'EOF'
#!/bin/sh
set -e

case "$1" in
    remove|upgrade|deconfigure)
        # Stop the daemon for all logged-in users
        for user_id in $(loginctl list-users --no-legend 2>/dev/null | awk '{print $1}'); do
            user_name=$(loginctl show-user "$user_id" -p Name --value 2>/dev/null)
            if [ -n "$user_name" ]; then
                su - "$user_name" -c "systemctl --user stop novasearch-daemon" 2>/dev/null || true
            fi
        done
        ;;
esac

exit 0
EOF
chmod 755 "$PACKAGE_DIR/DEBIAN/prerm"

# Build the package
echo "Building .deb package..."
dpkg-deb --build --root-owner-group "$PACKAGE_DIR"

echo ""
echo "========================================"
echo "Build Complete!"
echo "========================================"
echo ""
echo "Package created: ${PACKAGE_DIR}.deb"
echo ""
echo "Package information:"
dpkg-deb -I "${PACKAGE_DIR}.deb"
echo ""
echo "Package contents:"
dpkg-deb -c "${PACKAGE_DIR}.deb"
echo ""
echo "To install:"
echo "  sudo dpkg -i ${PACKAGE_DIR}.deb"
echo "  sudo apt-get install -f  # Install any missing dependencies"
echo ""
echo "After installation:"
echo "  systemctl --user enable --now novasearch-daemon"
echo "  novasearch-daemon status"
echo ""
