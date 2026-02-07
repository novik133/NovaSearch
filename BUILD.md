# NovaSearch Build System

## Overview

NovaSearch uses a hybrid build system:
- **Rust daemon**: Built with Cargo (Rust's native build tool)
- **C panel plugin**: Built with Meson
- **Top-level coordination**: Meson orchestrates both builds

## Quick Start

### Build Everything
```bash
./build.sh
```

Or manually:
```bash
meson setup builddir
meson compile -C builddir
```

### Build Options

```bash
# Build only the daemon
meson setup builddir -Dpanel=false

# Build only the panel
meson setup builddir -Ddaemon=false

# Disable tests
meson setup builddir -Dtests=false

# Debug build
meson setup builddir --buildtype=debug
```

## Build Commands

### Setup
```bash
meson setup builddir [OPTIONS]
```

### Compile
```bash
meson compile -C builddir
```

### Test
```bash
meson test -C builddir
```

### Install
```bash
sudo meson install -C builddir
```

### Clean
```bash
rm -rf builddir
```

## Build Outputs

After building, you'll find:

- **Daemon binary**: `builddir/novasearch-daemon` (or `daemon/target/release/novasearch-daemon`)
- **Panel plugin**: `builddir/panel/novasearch-panel.so`

## Development Workflow

### Rust Daemon Development

```bash
cd daemon

# Build
cargo build

# Run tests
cargo test

# Run with logging
RUST_LOG=debug cargo run

# Build release
cargo build --release
```

### Panel Development

```bash
# Reconfigure if needed
meson configure builddir

# Build
meson compile -C builddir

# Run tests
meson test -C builddir
```

## Meson Options

Available options (see `meson_options.txt`):

- `tests` (boolean, default: true) - Build and run tests
- `daemon` (boolean, default: true) - Build the Rust daemon
- `panel` (boolean, default: true) - Build the panel plugin

Set options during setup:
```bash
meson setup builddir -Doption=value
```

Or reconfigure existing build:
```bash
meson configure builddir -Doption=value
```

## Dependencies

### Required for Daemon
- Rust toolchain (rustc, cargo)
- See `daemon/Cargo.toml` for Rust dependencies

### Required for Panel
- Meson >= 0.55
- Ninja build system
- pkg-config
- GTK+ 3.0 >= 3.22
- libxfce4panel-2.0 >= 4.12
- SQLite3 >= 3.0

### Optional for Testing
- theft (for property-based testing in C)

## Troubleshooting

### "cargo not found"
Install Rust: https://rustup.rs/

### "meson not found"
Install Meson: https://mesonbuild.com/Getting-meson.html

### "libxfce4panel-2.0 not found"
Install XFCE4 panel development files:
- Debian/Ubuntu: `sudo apt-get install libxfce4panel-2.0-dev`
- Fedora: `sudo dnf install xfce4-panel-devel`
- Arch: `sudo pacman -S xfce4-panel`

Or build without panel: `meson setup builddir -Dpanel=false`

### Build fails with "ninja not found"
Install ninja build system:
- Debian/Ubuntu: `sudo apt-get install ninja-build`
- Fedora: `sudo dnf install ninja-build`
- Arch: `sudo pacman -S ninja`

### Reconfigure after dependency changes
```bash
rm -rf builddir
meson setup builddir
```

## CI/CD Integration

### GitHub Actions Example
```yaml
- name: Install dependencies
  run: |
    sudo apt-get update
    sudo apt-get install -y meson ninja-build pkg-config libgtk-3-dev libxfce4panel-2.0-dev libsqlite3-dev
    curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y

- name: Build
  run: |
    source $HOME/.cargo/env
    meson setup builddir
    meson compile -C builddir

- name: Test
  run: meson test -C builddir
```

## Advanced Usage

### Cross-compilation (Rust)
```bash
cd daemon
rustup target add x86_64-unknown-linux-musl
cargo build --target x86_64-unknown-linux-musl --release
```

### Custom install prefix
```bash
meson setup builddir --prefix=/opt/novasearch
sudo meson install -C builddir
```

### Verbose build
```bash
meson compile -C builddir -v
```

### Parallel builds
```bash
meson compile -C builddir -j 8
```
