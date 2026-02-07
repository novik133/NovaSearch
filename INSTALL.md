# NovaSearch Installation Guide

## Prerequisites

### System Dependencies

**Debian/Ubuntu:**
```bash
sudo apt-get install build-essential meson ninja-build pkg-config libgtk-3-dev libxfce4panel-2.0-dev libsqlite3-dev
```

**Fedora:**
```bash
sudo dnf install gcc meson ninja-build pkg-config gtk3-devel xfce4-panel-devel sqlite-devel
```

**Arch Linux:**
```bash
sudo pacman -S base-devel meson ninja pkg-config gtk3 xfce4-panel sqlite
```

### Rust Toolchain

Install Rust using rustup:
```bash
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
```

## Building

### Quick Build (Everything)

```bash
meson setup builddir
meson compile -C builddir
```

### Build Options

You can customize the build with options:

```bash
# Build only the daemon
meson setup builddir -Dpanel=false

# Build only the panel
meson setup builddir -Ddaemon=false

# Disable tests
meson setup builddir -Dtests=false
```

### Build Individual Components

**Build the Indexing Daemon (Rust):**
```bash
cd daemon
cargo build --release
```

The binary will be at `daemon/target/release/novasearch-daemon`.

**Build the Search Panel Plugin (C):**
```bash
meson setup builddir
meson compile -C builddir
```

The plugin will be at `builddir/panel/novasearch-panel.so`.

## Installation

### Install Everything

```bash
meson setup builddir --prefix=/usr/local
meson compile -C builddir
sudo meson install -C builddir
```

### Install the Daemon Only

```bash
# Copy the daemon binary
sudo cp daemon/target/release/novasearch-daemon /usr/local/bin/

# Create systemd user service directory if it doesn't exist
mkdir -p ~/.config/systemd/user/

# Copy the systemd service file (to be created in task 12.1)
# cp daemon/novasearch-daemon.service ~/.config/systemd/user/

# Enable and start the service
# systemctl --user enable novasearch-daemon
# systemctl --user start novasearch-daemon
```

### Install the Panel Plugin Only

```bash
meson setup builddir --prefix=/usr
meson compile -C builddir
sudo meson install -C builddir
```

Then restart the XFCE4 panel or add the plugin through the panel preferences.

## Running Tests

### Run All Tests

```bash
meson test -C builddir
```

### Run Rust Tests

```bash
cd daemon
cargo test
```

### Run Panel Tests

```bash
meson test -C builddir --suite panel
```

## Configuration

### Create Default Configuration

The daemon will create default configuration on first run at:
`~/.config/novasearch/config.toml`

### Database Location

The index database will be stored at:
`~/.local/share/novasearch/index.db`

## Verification

Check that the daemon is running:
```bash
systemctl --user status novasearch-daemon
```

Check the database was created:
```bash
ls -lh ~/.local/share/novasearch/index.db
```

## Development

### Reconfigure Build

```bash
meson configure builddir -Doption=value
```

### Clean Build

```bash
rm -rf builddir
meson setup builddir
```

## Troubleshooting

See the main README.md for troubleshooting information.
