# NovaSearch

Fast, system-wide file search for Linux with XFCE4 integration. NovaSearch provides real-time file indexing and intelligent search ranking similar to macOS Spotlight, with comprehensive application discovery and launching capabilities.

## Features

- **🚀 Real-time indexing**: Monitors filesystem changes using inotify
- **⚡ Fast search**: SQLite-backed index with optimized queries and usage tracking
- **🎯 Smart application discovery**: Automatically indexes all applications (apt, snap, flatpak, AppImage)
- **🖼️ Application icons**: Shows proper application icons and names in search results
- **🔧 Native integration**: GTK3 panel plugin for XFCE4 with system theme support
- **⌨️ Configurable shortcuts**: Interactive keyboard shortcut capture and customization
- **📁 Configuration GUI**: Built-in configuration editor with tabbed interface
- **💡 Usage tracking**: Prioritizes frequently used files and applications
- **🎨 Modern UI**: Spotlight-like search window without decorations
- **📊 CLI tools**: Comprehensive command-line interface for daemon management

## 📊 Performance Benchmarks (Actual Data)

NovaSearch is designed for maximum efficiency. Below are real-world statistics from a system with nearly 200,000 indexed files:
- **Metric	NovaSearch Daemon	Context / Comparison
- **Physical RAM (RSS)	~48.6 MB	Idle state (after initial index)
- **Peak Memory	~75.9 MB	During heavy indexing operations
- **Indexing Speed	~5,000 files/sec	25k files in ~5s
- **Database Size	93 MB	For ~188,360 indexed items
- **CPU Impact	< 0.1%	Negligible background monitoring

Compared to GNOME Tracker or macOS Spotlight, NovaSearch uses up to 70% less memory while maintaining real-time updates via inotify.

## Version

**Current Version**: 0.1.0

## Quick Start

### Installation (Debian Package)

1. **Download and install the package**:
   ```bash
   # Build the package
   ./build-deb-manual.sh
   
   # Install
   sudo dpkg -i novasearch_0.1.0-1_amd64.deb
   ```

2. **Enable the daemon**:
   ```bash
   systemctl --user enable --now novasearch-daemon
   ```

3. **Add panel plugin**:
   - Right-click XFCE4 panel → Panel → Add New Items → NovaSearch

4. **Start searching**:
   - Press `Super+Space` to open search window
   - Start typing to search files and applications

## Project Structure

```
novasearch/
├── daemon/                    # Rust indexing daemon
│   ├── src/
│   │   ├── main.rs           # Entry point and CLI commands
│   │   ├── config.rs         # Configuration management
│   │   ├── database.rs       # SQLite operations with usage tracking
│   │   ├── models.rs         # Data structures
│   │   ├── scanner.rs        # Filesystem scanning with app discovery
│   │   ├── watcher.rs        # Filesystem monitoring
│   │   └── paths.rs          # Path utilities
│   └── Cargo.toml
├── panel/                     # GTK3/C search panel plugin
│   ├── src/
│   │   ├── main.c            # Panel plugin with desktop file support
│   │   ├── database.c        # Database interface
│   │   └── database.h
│   ├── tests/                # Unit tests
│   └── meson.build
├── debian/                    # Debian packaging
├── build-deb-manual.sh       # Package builder
├── meson.build               # Top-level build configuration
└── README.md
```

## Dependencies

### Build Dependencies

**Required for building:**
- Rust 1.70+ and Cargo
- GCC or Clang
- Meson build system (≥ 0.59)
- Ninja build system
- pkg-config

**Development libraries:**
- GTK3 development libraries (≥ 3.22)
- SQLite3 development libraries (≥ 3.0)
- libxfce4panel development libraries (≥ 4.12)
- libxfce4ui development libraries
- keybinder-3.0 development libraries

### Runtime Dependencies

**Required for running:**
- GTK3 (≥ 3.22)
- SQLite3 (≥ 3.0)
- XFCE4 Panel (≥ 4.12)
- systemd (for daemon service)
- keybinder-3.0

### Installation Commands

**Ubuntu/Debian:**
```bash
sudo apt update
sudo apt install build-essential rustc cargo meson ninja-build pkg-config \
  libgtk-3-dev libsqlite3-dev libxfce4panel-2.0-dev libxfce4ui-2-dev \
  libkeybinder-3.0-dev
```

**Fedora:**
```bash
sudo dnf install rust cargo meson ninja-build gtk3-devel sqlite-devel \
  xfce4-panel-devel libxfce4ui-devel keybinder3-devel gcc pkg-config
```

**Arch Linux:**
```bash
sudo pacman -S rust meson ninja gtk3 sqlite xfce4-panel libxfce4ui \
  keybinder3 gcc pkg-config
```

## Building from Source

### Method 1: Debian Package (Recommended)

```bash
# Clone the repository
git clone https://github.com/novik133/NovaSearch.git
cd NovaSearch

# Install build dependencies (Ubuntu/Debian)
sudo apt install build-essential rustc cargo meson ninja-build pkg-config \
  libgtk-3-dev libsqlite3-dev libxfce4panel-2.0-dev libxfce4ui-2-dev \
  libkeybinder-3.0-dev

# Build the Debian package
./build-deb-manual.sh

# Install the package
sudo dpkg -i novasearch_0.1.0-1_amd64.deb

# Install any missing dependencies
sudo apt-get install -f
```

### Method 2: Manual Build

```bash
# Build using Meson
meson setup builddir --prefix=/usr
meson compile -C builddir

# Install
sudo meson install -C builddir

# Or install to user directory
meson setup builddir --prefix=$HOME/.local
meson compile -C builddir
meson install -C builddir
```

### Method 3: Component-wise Build

**Build daemon only:**
```bash
cd daemon
cargo build --release
# Binary will be at: target/release/novasearch-daemon
```

**Build panel plugin only:**
```bash
cd panel
make
# Library will be at: libnovasearch-panel.so
```

## Installation

### Automatic Installation (Debian Package)

The Debian package automatically handles:
- Binary installation to `/usr/bin/`
- Panel plugin installation to `/usr/lib/x86_64-linux-gnu/xfce4/panel/plugins/`
- Desktop file installation to `/usr/share/xfce4/panel/plugins/`
- Systemd service installation to `/usr/lib/systemd/user/`
- Default configuration to `/etc/xdg/novasearch/`

### Manual Installation

If building manually, you need to:

1. **Install binaries**:
   ```bash
   cp daemon/target/release/novasearch-daemon ~/.local/bin/
   cp panel/libnovasearch-panel.so ~/.local/lib/xfce4/panel/plugins/
   ```

2. **Install service file**:
   ```bash
   mkdir -p ~/.config/systemd/user
   cp novasearch-daemon.service ~/.config/systemd/user/
   systemctl --user daemon-reload
   ```

3. **Install panel plugin desktop file**:
   ```bash
   mkdir -p ~/.local/share/xfce4/panel/plugins
   cp panel/novasearch-panel.desktop ~/.local/share/xfce4/panel/plugins/
   ```

4. **Create configuration**:
   ```bash
   mkdir -p ~/.config/novasearch
   cp debian/novasearch.toml ~/.config/novasearch/config.toml
   ```

## Configuration

NovaSearch is configured via `~/.config/novasearch/config.toml` (user config) or `/etc/xdg/novasearch/config.toml` (system default).

### Configuration Options

#### [indexing]
Controls which directories and files are indexed.

```toml
[indexing]
include_paths = ["/home/username"]
exclude_patterns = [".*", "*.tmp", "*.log"]
```

**Note**: Application directories are always indexed regardless of this configuration:
- `/usr/share/applications` (system applications)
- `/usr/local/share/applications` (local applications)
- `~/.local/share/applications` (user applications)
- Snap, Flatpak, and AppImage locations

#### [performance]
Controls resource usage and indexing behavior.

```toml
[performance]
max_cpu_percent = 10
max_memory_mb = 100
batch_size = 100
flush_interval_ms = 1000
```

#### [ui]
Controls search panel behavior.

```toml
[ui]
keyboard_shortcut = "Super+Space"
max_results = 50
```

### GUI Configuration

NovaSearch includes a built-in configuration interface:

1. **Access settings**: Right-click panel plugin → Configure
2. **Hotkeys tab**: Interactive keyboard shortcut capture
3. **Configuration tab**: Direct config.toml editing with syntax highlighting
4. **About tab**: Version and author information

## Usage

### Daemon Management

**CLI Commands:**
```bash
# Show version
novasearch-daemon version

# Show about information
novasearch-daemon about

# Show author information
novasearch-daemon author

# Start daemon
novasearch-daemon start

# Check status
novasearch-daemon status

# Force re-index
novasearch-daemon reindex
```

**Systemd Service:**
```bash
# Enable and start
systemctl --user enable --now novasearch-daemon

# Check status
systemctl --user status novasearch-daemon

# View logs
journalctl --user -u novasearch-daemon -f

# Restart
systemctl --user restart novasearch-daemon
```

### Search Interface

1. **Open search**: Press `Super+Space` (or configured shortcut)
2. **Search**: Type to search files and applications
3. **Navigate**: Use arrow keys or mouse
4. **Launch**: Press Enter or click to open/launch
5. **Context menu**: Right-click for "Open containing folder"
6. **Close**: Press Escape or click outside

### Search Features

- **Application launching**: .desktop files launch applications instead of opening in text editor
- **Application icons**: Shows proper application icons (Firefox, Chrome, etc.)
- **Application names**: Displays "Firefox" instead of "firefox.desktop"
- **Usage tracking**: Frequently used items appear higher in results
- **Real-time results**: Updates as you type with 200ms debounce
- **Keyboard navigation**: Full keyboard support with arrow keys
- **Case-insensitive**: Search works regardless of case

## Application Discovery

NovaSearch automatically discovers and indexes applications from:

### System Applications
- `/usr/share/applications` - System-wide applications
- `/usr/local/share/applications` - Locally installed applications

### User Applications
- `~/.local/share/applications` - User-specific applications

### Snap Applications
- `/var/lib/snapd/desktop/applications` - System snap applications
- `~/snap` - User snap applications

### Flatpak Applications
- `/var/lib/flatpak/exports/share/applications` - System flatpak applications
- `~/.local/share/flatpak/exports/share/applications` - User flatpak applications

### AppImage Applications
- `~/Applications` - Common AppImage location
- `~/.local/bin` - Local binaries including AppImages
- `~/AppImages` - Dedicated AppImage folder
- `/opt` - System-wide optional applications

## Troubleshooting

### Common Issues

**Daemon won't start:**
```bash
# Check status
systemctl --user status novasearch-daemon

# View logs
journalctl --user -u novasearch-daemon -n 50

# Check binary exists
which novasearch-daemon
```

**Search window doesn't appear:**
```bash
# Test shortcut
grep keyboard_shortcut ~/.config/novasearch/config.toml

# Check panel plugin
xfce4-panel --restart
```

**No search results:**
```bash
# Check indexing status
novasearch-daemon status

# Force re-index
novasearch-daemon reindex

# Check database
sqlite3 ~/.local/share/novasearch/index.db "SELECT COUNT(*) FROM files;"
```

**Applications don't launch:**
- Ensure .desktop files are valid
- Check application is installed
- Try launching from terminal first

### Performance Issues

**High CPU/Memory usage:**
```toml
[performance]
max_cpu_percent = 5      # Reduce from 10
max_memory_mb = 50       # Reduce from 100
batch_size = 50          # Reduce from 100
```

**Exclude large directories:**
```toml
[indexing]
exclude_patterns = [
    ".*", "node_modules", ".git", "target",
    "Videos", "Downloads", "build", "dist"
]
```

### Database Issues

**Corruption recovery:**
```bash
# Stop daemon
systemctl --user stop novasearch-daemon

# Backup and remove database
mv ~/.local/share/novasearch/index.db ~/.local/share/novasearch/index.db.backup

# Restart (creates new database)
systemctl --user start novasearch-daemon

# Re-index
novasearch-daemon reindex
```

## File Locations

### Installed Files
- **Daemon**: `/usr/bin/novasearch-daemon`
- **Panel plugin**: `/usr/lib/x86_64-linux-gnu/xfce4/panel/plugins/libnovasearch-panel.so`
- **Desktop file**: `/usr/share/xfce4/panel/plugins/novasearch-panel.desktop`
- **Service file**: `/usr/lib/systemd/user/novasearch-daemon.service`
- **Default config**: `/etc/xdg/novasearch/config.toml`

### User Data
- **User config**: `~/.config/novasearch/config.toml`
- **Database**: `~/.local/share/novasearch/index.db`
- **Logs**: `journalctl --user -u novasearch-daemon`

## Development

### Running Tests

**Daemon tests:**
```bash
cd daemon
cargo test
cargo test --release  # For property-based tests
```

**Panel tests:**
```bash
cd panel
make test
```

### Development Build

**Debug build:**
```bash
cd daemon
cargo build  # Debug build with symbols

cd panel
make DEBUG=1  # Debug build with symbols
```

**Development daemon:**
```bash
cd daemon
RUST_LOG=debug cargo run -- start
```

## API and Integration

### Database Schema

The SQLite database (`~/.local/share/novasearch/index.db`) contains:

**files table:**
- `id` - Primary key
- `filename` - File name
- `path` - Full file path
- `size` - File size in bytes
- `modified_time` - Last modification timestamp
- `file_type` - File type (Regular, Directory, Symlink)
- `indexed_time` - When file was indexed

**usage_stats table:**
- `id` - Primary key
- `file_path` - File path
- `launch_count` - Number of times launched
- `last_launched` - Last launch timestamp

### Configuration API

Configuration is managed through TOML files with automatic reloading. The daemon monitors config file changes and reloads within 10 seconds.

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Add tests if applicable
5. Ensure all tests pass
6. Submit a pull request

### Code Style

**Rust code:**
- Follow `rustfmt` formatting
- Use `clippy` for linting
- Add documentation for public APIs

**C code:**
- Follow GNU C style
- Use consistent indentation (4 spaces)
- Add comments for complex logic

## License

GPL-3.0

## Author

Created by Kamil 'Novik' Nowicki

- GitHub: https://github.com/novik133
- Project: https://github.com/novik133/NovaSearch

## Support

If you find NovaSearch useful, consider supporting development:
- Ko-fi: https://ko-fi.com/novadesktop

## Changelog

See [CHANGELOG.md](CHANGELOG.md) for version history and changes.
