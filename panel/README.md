# NovaSearch Panel Plugin

GTK3-based search panel plugin for XFCE4.

## Dependencies

The following system packages are required:

- `libgtk-3-dev` - GTK3 development libraries
- `libxfce4panel-2.0-dev` - XFCE4 panel development libraries
- `libsqlite3-dev` - SQLite3 development libraries
- `libtheft` - Property-based testing library (for tests only)

### Installing Dependencies

**Debian/Ubuntu:**
```bash
sudo apt-get install libgtk-3-dev libxfce4panel-2.0-dev libsqlite3-dev
```

**Fedora:**
```bash
sudo dnf install gtk3-devel xfce4-panel-devel sqlite-devel
```

**Arch Linux:**
```bash
sudo pacman -S gtk3 xfce4-panel sqlite
```

## Building

```bash
make
```

## Testing

```bash
make test
```

Note: Tests require the `theft` library for property-based testing.

## Installation

```bash
sudo make install
```
