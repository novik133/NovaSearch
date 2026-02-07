#!/bin/bash
set -e

# NovaSearch Installation Script
# This script installs the NovaSearch daemon and panel plugin

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

echo_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

echo_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if running as root
if [ "$EUID" -eq 0 ]; then
    echo_error "Do not run this script as root. It installs to user directories."
    exit 1
fi

# Determine installation directories
BIN_DIR="$HOME/.local/bin"
CONFIG_DIR="$HOME/.config/novasearch"
DATA_DIR="$HOME/.local/share/novasearch"
SYSTEMD_DIR="$HOME/.config/systemd/user"
PANEL_PLUGIN_DIR="$HOME/.local/share/xfce4/panel/plugins"

# Create directories
echo_info "Creating directories..."
mkdir -p "$BIN_DIR"
mkdir -p "$CONFIG_DIR"
mkdir -p "$DATA_DIR"
mkdir -p "$SYSTEMD_DIR"
mkdir -p "$PANEL_PLUGIN_DIR"

# Set correct permissions for data directory
chmod 700 "$DATA_DIR"

# Install daemon binary
echo_info "Installing daemon binary..."
if [ -f "daemon/target/release/novasearch-daemon" ]; then
    cp daemon/target/release/novasearch-daemon "$BIN_DIR/"
    chmod 755 "$BIN_DIR/novasearch-daemon"
    echo_info "Daemon installed to $BIN_DIR/novasearch-daemon"
else
    echo_error "Daemon binary not found. Please build the project first:"
    echo_error "  cd daemon && cargo build --release"
    exit 1
fi

# Install panel plugin
echo_info "Installing panel plugin..."
if [ -f "panel/build/novasearch-panel" ]; then
    cp panel/build/novasearch-panel "$BIN_DIR/"
    chmod 755 "$BIN_DIR/novasearch-panel"
    echo_info "Panel plugin installed to $BIN_DIR/novasearch-panel"
else
    echo_warn "Panel plugin binary not found. Skipping panel installation."
    echo_warn "To build the panel: cd panel && make"
fi

# Install desktop file for panel plugin
if [ -f "panel/novasearch-panel.desktop" ]; then
    cp panel/novasearch-panel.desktop "$PANEL_PLUGIN_DIR/"
    echo_info "Panel plugin desktop file installed"
fi

# Create default configuration if it doesn't exist
if [ ! -f "$CONFIG_DIR/config.toml" ]; then
    echo_info "Creating default configuration..."
    cat > "$CONFIG_DIR/config.toml" << 'EOF'
[indexing]
include_paths = ["~"]
exclude_patterns = [".*", "node_modules", ".git", "target", ".cache", ".cargo"]

[performance]
max_cpu_percent = 10
max_memory_mb = 100
batch_size = 100
flush_interval_ms = 1000

[ui]
keyboard_shortcut = "Super+Space"
max_results = 50
EOF
    echo_info "Default configuration created at $CONFIG_DIR/config.toml"
else
    echo_info "Configuration file already exists, skipping creation"
fi

# Install systemd service
echo_info "Installing systemd user service..."
if [ -f "novasearch-daemon.service" ]; then
    cp novasearch-daemon.service "$SYSTEMD_DIR/"
    systemctl --user daemon-reload
    echo_info "Systemd service installed"
    
    # Ask user if they want to enable the service
    read -p "Enable NovaSearch daemon to start on login? [Y/n] " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]] || [[ -z $REPLY ]]; then
        systemctl --user enable novasearch-daemon.service
        echo_info "Service enabled"
        
        read -p "Start NovaSearch daemon now? [Y/n] " -n 1 -r
        echo
        if [[ $REPLY =~ ^[Yy]$ ]] || [[ -z $REPLY ]]; then
            systemctl --user start novasearch-daemon.service
            echo_info "Service started"
        fi
    fi
else
    echo_warn "Systemd service file not found, skipping service installation"
fi

# Check if PATH includes ~/.local/bin
if [[ ":$PATH:" != *":$HOME/.local/bin:"* ]]; then
    echo_warn "~/.local/bin is not in your PATH"
    echo_warn "Add this line to your ~/.bashrc or ~/.zshrc:"
    echo_warn '  export PATH="$HOME/.local/bin:$PATH"'
fi

echo
echo_info "Installation complete!"
echo
echo "Next steps:"
echo "  1. Ensure ~/.local/bin is in your PATH"
echo "  2. Add the NovaSearch panel plugin to your XFCE4 panel"
echo "  3. Check daemon status: systemctl --user status novasearch-daemon"
echo "  4. View logs: journalctl --user -u novasearch-daemon -f"
echo
