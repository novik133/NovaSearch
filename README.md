# NovaSearch

Fast, system-wide file search for Linux with XFCE4 integration.

## Features

- **Real-time indexing**: Monitors filesystem changes using inotify
- **Fast search**: SQLite-backed index with optimized queries
- **Native integration**: GTK3 panel plugin for XFCE4
- **Configurable**: Customizable include/exclude patterns
- **Lightweight**: Minimal resource usage

## Project Structure

```
novasearch/
├── daemon/          # Rust indexing daemon
│   ├── src/
│   │   ├── main.rs
│   │   ├── config.rs
│   │   ├── database.rs
│   │   ├── models.rs
│   │   ├── watcher.rs
│   │   └── paths.rs
│   └── Cargo.toml
├── panel/           # GTK3/C search panel plugin
│   ├── src/
│   ├── tests/
│   └── meson.build
├── meson.build      # Top-level build configuration
├── build.sh         # Convenience build script
└── README.md
```

## Installation

### Prerequisites

**Required:**
- Rust 1.70+ and Cargo
- GCC or Clang
- GTK3 development libraries
- SQLite3 development libraries
- libxfce4panel development libraries
- Meson build system
- systemd (for daemon service)

**Ubuntu/Debian:**
```bash
sudo apt install build-essential rustc cargo meson ninja-build \
  libgtk-3-dev libsqlite3-dev libxfce4panel-2.0-dev pkg-config
```

**Fedora:**
```bash
sudo dnf install rust cargo meson ninja-build gtk3-devel \
  sqlite-devel xfce4-panel-devel gcc
```

**Arch Linux:**
```bash
sudo pacman -S rust meson ninja gtk3 sqlite xfce4-panel
```

### Build

```bash
./build.sh
```

Or using Meson directly:
```bash
meson setup builddir
meson compile -C builddir
```

See [BUILD.md](BUILD.md) for detailed build instructions.

### Install

Run the installation script:
```bash
./install.sh
```

The script will:
- Install binaries to `~/.local/bin/`
- Create default configuration at `~/.config/novasearch/config.toml`
- Set up database directory at `~/.local/share/novasearch/`
- Install systemd user service
- Register XFCE4 panel plugin

**Manual Installation:**

If you prefer manual installation, see [INSTALL.md](INSTALL.md) for detailed steps.

### Post-Installation

1. **Add to PATH** (if not already):
   ```bash
   echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
   source ~/.bashrc
   ```

2. **Enable daemon** (if not done during install):
   ```bash
   systemctl --user enable --now novasearch-daemon.service
   ```

3. **Add panel plugin**:
   - Right-click on XFCE4 panel → Panel → Add New Items
   - Search for "NovaSearch"
   - Add to panel

4. **Test the shortcut**:
   - Press `Super+Space` to open search window
   - Start typing to search for files

## Configuration

NovaSearch is configured via `~/.config/novasearch/config.toml`. The configuration file is created automatically with sensible defaults during installation.

### Configuration Options

#### [indexing]

Controls which directories and files are indexed.

```toml
[indexing]
include_paths = ["~", "/mnt/data"]
exclude_patterns = [".*", "node_modules", ".git", "target"]
```

- **include_paths**: Array of directories to index. Supports `~` for home directory.
- **exclude_patterns**: Array of glob patterns for files/directories to exclude.

#### [performance]

Controls resource usage and indexing behavior.

```toml
[performance]
max_cpu_percent = 10
max_memory_mb = 100
batch_size = 100
flush_interval_ms = 1000
```

- **max_cpu_percent**: Maximum CPU usage during indexing (1-100)
- **max_memory_mb**: Maximum memory usage in megabytes
- **batch_size**: Number of operations to batch before writing to database
- **flush_interval_ms**: Maximum time to wait before flushing batched operations

#### [ui]

Controls search panel behavior.

```toml
[ui]
keyboard_shortcut = "Super+Space"
max_results = 50
```

- **keyboard_shortcut**: Global keyboard shortcut to open search window
  - Format: `Modifier+Key` (e.g., `Super+Space`, `Control+Alt+F`, `Alt+Space`)
  - Common modifiers: `Super`, `Control`, `Alt`, `Shift`
- **max_results**: Maximum number of search results to display (1-1000)

### Glob Pattern Examples

Glob patterns are used in `exclude_patterns` to filter files and directories.

**Basic Patterns:**
- `*.log` - Exclude all .log files
- `temp` - Exclude any file or directory named "temp"
- `.*` - Exclude all hidden files and directories (starting with .)

**Wildcards:**
- `*` - Matches any characters except /
- `**` - Matches any characters including /
- `?` - Matches exactly one character

**Examples:**
```toml
exclude_patterns = [
    ".*",                    # All hidden files/directories
    "node_modules",          # Node.js dependencies
    ".git",                  # Git repository data
    "target",                # Rust build artifacts
    "*.tmp",                 # Temporary files
    "*.cache",               # Cache files
    "build",                 # Build directories
    "dist",                  # Distribution directories
    "__pycache__",           # Python cache
    "*.pyc",                 # Python compiled files
    ".venv",                 # Python virtual environments
    "vendor",                # Vendor directories
    ".DS_Store",             # macOS metadata
    "Thumbs.db",             # Windows thumbnails
]
```

**Advanced Patterns:**
```toml
exclude_patterns = [
    "**/.git",               # .git directories anywhere
    "**/node_modules",       # node_modules anywhere
    "*.log",                 # All log files
    "test_*.txt",            # Files starting with test_
    "backup_????_??.tar",    # backup_YYYY_MM.tar pattern
]
```

### Configuration Reload

The daemon automatically reloads configuration when the file changes. Changes take effect within 10 seconds and trigger re-indexing if include/exclude patterns changed.

## Usage

### Starting the Daemon

The daemon starts automatically on login if enabled during installation.

**Manual control:**
```bash
# Start daemon
systemctl --user start novasearch-daemon.service

# Stop daemon
systemctl --user stop novasearch-daemon.service

# Restart daemon
systemctl --user restart novasearch-daemon.service

# Check status
systemctl --user status novasearch-daemon.service

# View logs
journalctl --user -u novasearch-daemon -f
```

**CLI commands:**
```bash
# Start daemon in foreground (for debugging)
novasearch-daemon start

# Check indexing status
novasearch-daemon status

# Force re-index
novasearch-daemon reindex

# Use custom config
novasearch-daemon --config /path/to/config.toml start
```

### Using the Search Panel

1. **Open search**: Press `Super+Space` (or your configured shortcut)
2. **Search**: Start typing to search for files
3. **Navigate**: Use arrow keys to move through results
4. **Open file**: Press Enter or click on a result
5. **Open folder**: Right-click a result → "Open containing folder"
6. **Close search**: Press Escape or click outside the window

### Search Tips

- Search is case-insensitive
- Partial matches work: "doc" finds "document.txt"
- Results are ranked: exact matches first, then prefix matches, then substring matches
- Search updates as you type (200ms debounce)

## Troubleshooting

### Daemon won't start

**Check if daemon is running:**
```bash
systemctl --user status novasearch-daemon.service
```

**View error logs:**
```bash
journalctl --user -u novasearch-daemon -n 50
```

**Common issues:**
- **Binary not found**: Ensure `~/.local/bin` is in your PATH
- **Database locked**: Another instance may be running. Stop it with `systemctl --user stop novasearch-daemon`
- **Permission denied**: Check permissions on `~/.local/share/novasearch/` (should be 700)

### Search window doesn't appear

**Check keyboard shortcut:**
```bash
grep keyboard_shortcut ~/.config/novasearch/config.toml
```

**Test if shortcut conflicts:**
- Try changing shortcut in config to `Alt+Space` or `Control+Alt+F`
- Restart daemon: `systemctl --user restart novasearch-daemon`

**Check panel plugin:**
- Ensure panel plugin is added to XFCE4 panel
- Right-click panel → Panel → Add New Items → NovaSearch

### No search results

**Check if indexing is complete:**
```bash
novasearch-daemon status
```

**Check database:**
```bash
sqlite3 ~/.local/share/novasearch/index.db "SELECT COUNT(*) FROM files;"
```

**Force re-index:**
```bash
novasearch-daemon reindex
```

**Check configuration:**
```bash
cat ~/.config/novasearch/config.toml
```
- Ensure `include_paths` contains directories you want to search
- Check if files are excluded by `exclude_patterns`

### High CPU/memory usage

**Check current resource usage:**
```bash
systemctl --user status novasearch-daemon.service
```

**Adjust limits in config:**
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
    ".*",
    "node_modules",
    "target",
    "build",
    "Videos",            # Exclude large media directories
    "Downloads",
]
```

### Database corruption

**Symptoms:**
- Search returns no results
- Daemon crashes on startup
- "database disk image is malformed" errors

**Fix:**
```bash
# Stop daemon
systemctl --user stop novasearch-daemon

# Backup old database
mv ~/.local/share/novasearch/index.db ~/.local/share/novasearch/index.db.backup

# Restart daemon (will create new database)
systemctl --user start novasearch-daemon

# Force re-index
novasearch-daemon reindex
```

### Panel plugin not visible

**Check if plugin is installed:**
```bash
ls -la ~/.local/share/xfce4/panel/plugins/
```

**Reinstall plugin:**
```bash
./install.sh
```

**Restart XFCE panel:**
```bash
xfce4-panel -r
```

### Logs and debugging

**View daemon logs:**
```bash
# Last 50 lines
journalctl --user -u novasearch-daemon -n 50

# Follow logs in real-time
journalctl --user -u novasearch-daemon -f

# Logs since last boot
journalctl --user -u novasearch-daemon -b
```

**Enable debug logging:**
Set `RUST_LOG` environment variable in systemd service:
```bash
systemctl --user edit novasearch-daemon.service
```
Add:
```ini
[Service]
Environment="RUST_LOG=debug"
```
Then restart:
```bash
systemctl --user daemon-reload
systemctl --user restart novasearch-daemon
```

## File Locations

- **Daemon binary**: `~/.local/bin/novasearch-daemon`
- **Panel plugin binary**: `~/.local/bin/novasearch-panel`
- **Configuration**: `~/.config/novasearch/config.toml`
- **Database**: `~/.local/share/novasearch/index.db`
- **Systemd service**: `~/.config/systemd/user/novasearch-daemon.service`
- **Panel plugin desktop file**: `~/.local/share/xfce4/panel/plugins/novasearch-panel.desktop`
- **Logs**: `journalctl --user -u novasearch-daemon`

## Development

### Building from Source

**Rust Daemon:**
```bash
cd daemon
cargo test          # Run tests
cargo build         # Debug build
cargo build --release  # Release build
```

**Panel Plugin:**
```bash
meson setup builddir
meson test -C builddir  # Run tests
meson compile -C builddir
```

### Running Tests

**Unit tests:**
```bash
cd daemon
cargo test
```

**Property-based tests:**
```bash
cd daemon
cargo test --release -- --nocapture
```

**Panel tests:**
```bash
meson test -C builddir -v
```

### Project Structure

```
novasearch/
├── daemon/                    # Rust indexing daemon
│   ├── src/
│   │   ├── main.rs           # Entry point and CLI
│   │   ├── config.rs         # Configuration management
│   │   ├── database.rs       # SQLite operations
│   │   ├── models.rs         # Data structures
│   │   ├── scanner.rs        # Filesystem scanning
│   │   ├── watcher.rs        # Filesystem monitoring
│   │   └── paths.rs          # Path utilities
│   ├── examples/
│   │   └── watcher_demo.rs   # Watcher example
│   └── Cargo.toml
├── panel/                     # GTK3/C search panel plugin
│   ├── src/
│   │   ├── main.c            # Panel plugin implementation
│   │   ├── database.c        # Database interface
│   │   └── database.h
│   ├── tests/
│   │   ├── test_database.c
│   │   ├── test_database_query.c
│   │   ├── test_keyboard_shortcut.c
│   │   └── test_main.c
│   ├── meson.build
│   └── Makefile
├── .kiro/specs/nova-search/   # Specification documents
│   ├── requirements.md
│   ├── design.md
│   └── tasks.md
├── meson.build                # Top-level build configuration
├── build.sh                   # Convenience build script
├── install.sh                 # Installation script
├── novasearch-daemon.service  # Systemd service file
├── BUILD.md                   # Build documentation
├── INSTALL.md                 # Installation documentation
└── README.md
```

## Documentation

- [BUILD.md](BUILD.md) - Detailed build system documentation
- [INSTALL.md](INSTALL.md) - Installation guide
- [.kiro/specs/nova-search/](/.kiro/specs/nova-search/) - Complete specification
  - [requirements.md](/.kiro/specs/nova-search/requirements.md) - Requirements
  - [design.md](/.kiro/specs/nova-search/design.md) - Design document
  - [tasks.md](/.kiro/specs/nova-search/tasks.md) - Implementation tasks

## Status

**Current Progress**: Task 11 Complete - Installation and packaging in progress

- ✅ Project structure and dependencies
- ✅ Database layer
- ✅ Configuration management
- ✅ Filesystem monitoring
- ✅ Initial filesystem scanning
- ✅ Daemon main loop and CLI
- ✅ Search panel database interface
- ✅ Search panel UI
- ✅ Search panel interactions
- ✅ Keyboard shortcut handling
- 🔄 Installation and packaging (in progress)
- ⏳ Final integration testing (next)

## License

MIT
