#!/bin/bash
set -e

echo "========================================"
echo "NovaSearch Debian Package Builder"
echo "========================================"
echo ""

# Package information
PACKAGE_NAME="novasearch"
VERSION="0.1.0"
ARCH=$(dpkg --print-architecture)
PACKAGE_DIR="${PACKAGE_NAME}_${VERSION}-1_${ARCH}"

echo "Building package: ${PACKAGE_DIR}.deb"
echo ""

# Clean previous builds
echo "Cleaning previous builds..."
rm -rf "$PACKAGE_DIR" "${PACKAGE_DIR}.deb"
rm -rf builddir

# Create package directory structure
echo "Creating package structure..."
mkdir -p "$PACKAGE_DIR/DEBIAN"
mkdir -p "$PACKAGE_DIR/usr/bin"
mkdir -p "$PACKAGE_DIR/usr/lib/x86_64-linux-gnu/xfce4/panel/plugins"
mkdir -p "$PACKAGE_DIR/usr/share/xfce4/panel/plugins"
mkdir -p "$PACKAGE_DIR/usr/lib/systemd/user"
mkdir -p "$PACKAGE_DIR/etc/xdg/novasearch"
mkdir -p "$PACKAGE_DIR/usr/share/doc/novasearch"

# Build the project
echo "Building NovaSearch..."
meson setup builddir --prefix=/usr --buildtype=release
meson compile -C builddir

# Copy binaries
echo "Installing files..."
cp builddir/novasearch-daemon "$PACKAGE_DIR/usr/bin/"
chmod 755 "$PACKAGE_DIR/usr/bin/novasearch-daemon"

# Copy panel plugin
if [ -f "builddir/panel/libnovasearch-panel.so" ]; then
    cp builddir/panel/libnovasearch-panel.so "$PACKAGE_DIR/usr/lib/x86_64-linux-gnu/xfce4/panel/plugins/"
    chmod 644 "$PACKAGE_DIR/usr/lib/x86_64-linux-gnu/xfce4/panel/plugins/libnovasearch-panel.so"
    echo "Panel plugin installed"
else
    echo "Warning: Panel plugin not found"
fi

# Copy desktop file
if [ -f "panel/novasearch-panel.desktop" ]; then
    cp panel/novasearch-panel.desktop "$PACKAGE_DIR/usr/share/xfce4/panel/plugins/"
    chmod 644 "$PACKAGE_DIR/usr/share/xfce4/panel/plugins/novasearch-panel.desktop"
fi

# Copy systemd service
cp novasearch-daemon.service "$PACKAGE_DIR/usr/lib/systemd/user/"
chmod 644 "$PACKAGE_DIR/usr/lib/systemd/user/novasearch-daemon.service"

# Copy default configuration
cp debian/novasearch.toml "$PACKAGE_DIR/etc/xdg/novasearch/config.toml"
chmod 644 "$PACKAGE_DIR/etc/xdg/novasearch/config.toml"

# Copy documentation
cp README.md "$PACKAGE_DIR/usr/share/doc/novasearch/"
cp BUILD.md "$PACKAGE_DIR/usr/share/doc/novasearch/"
cp INSTALL.md "$PACKAGE_DIR/usr/share/doc/novasearch/"
cp debian/copyright "$PACKAGE_DIR/usr/share/doc/novasearch/"
chmod 644 "$PACKAGE_DIR/usr/share/doc/novasearch/"*

# Get installed size
INSTALLED_SIZE=$(du -sk "$PACKAGE_DIR" | cut -f1)

# Create control file
echo "Creating control file..."
cat > "$PACKAGE_DIR/DEBIAN/control" <<EOF
Package: novasearch
Version: ${VERSION}-1
Section: utils
Priority: optional
Architecture: ${ARCH}
Installed-Size: ${INSTALLED_SIZE}
Depends: libgtk-3-0 (>= 3.22), libxfce4panel-2.0-4 (>= 4.12), libsqlite3-0, xfce4-panel
Maintainer: NovaSearch Team <novasearch@example.com>
Description: Fast system-wide file search for Linux with XFCE4 integration
 NovaSearch provides fast, system-wide file search functionality similar to
 macOS Spotlight. It consists of two main components:
 .
  * An indexing daemon written in Rust that continuously monitors and indexes
    the filesystem using inotify
  * A GTK3-based search panel plugin for XFCE4 that provides a native user
    interface for searching indexed content
 .
 Features:
  * Real-time indexing with filesystem monitoring
  * Fast SQLite-backed search index
  * Native XFCE4 panel integration
  * Configurable include/exclude patterns
  * Minimal resource usage
  * Global keyboard shortcut (default: Super+Space)
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
        echo "NovaSearch installed successfully!"
        echo ""
        echo "To get started:"
        echo "  1. Enable the daemon: systemctl --user enable --now novasearch-daemon"
        echo "  2. Add the panel plugin: Right-click XFCE panel → Panel → Add New Items → NovaSearch"
        echo "  3. Press Super+Space to open the search window"
        echo ""
        echo "Configuration: ~/.config/novasearch/config.toml"
        echo "Database: ~/.local/share/novasearch/index.db"
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
dpkg-deb -I "${PACKAGE_DIR}.deb" | grep -E "Package:|Version:|Architecture:|Installed-Size:|Description:" | head -5
echo ""
echo "Package contents:"
dpkg-deb -c "${PACKAGE_DIR}.deb" | head -20
echo ""
echo "To install:"
echo "  sudo dpkg -i ${PACKAGE_DIR}.deb"
echo "  sudo apt-get install -f  # Install any missing dependencies"
echo ""
