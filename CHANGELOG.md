# Changelog

All notable changes to NovaSearch will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.0] - 2024-02-09

### Added

#### Core Features
- **Real-time filesystem indexing** using inotify for instant file discovery
- **SQLite-backed search index** with optimized queries and full-text search
- **XFCE4 panel plugin** written in C with GTK3 for native desktop integration
- **Rust daemon** for high-performance filesystem monitoring and indexing
- **Usage tracking system** that prioritizes frequently accessed files and applications
- **Smart application discovery** that automatically indexes all application types

#### Search Interface
- **Spotlight-like search window** without decorations for clean, modern appearance
- **Real-time search results** with 200ms debounce for responsive typing
- **Keyboard navigation** with full arrow key support and Enter to launch
- **Mouse interaction** with click-to-launch and right-click context menus
- **Case-insensitive search** that works regardless of input case
- **Maximum results limit** (configurable, default 50) for performance

#### Application Support
- **Desktop file parsing** to extract application names and icons
- **Application icon display** showing proper Firefox, Chrome, etc. icons instead of generic file icons
- **Application launching** that executes applications instead of opening .desktop files in text editors
- **Application name display** showing "Firefox" instead of "firefox.desktop"
- **Comprehensive application discovery** covering:
  - System applications (`/usr/share/applications`)
  - User applications (`~/.local/share/applications`)
  - Snap applications (`/var/lib/snapd/desktop/applications`, `~/snap`)
  - Flatpak applications (`/var/lib/flatpak/exports/share/applications`, `~/.local/share/flatpak/exports/share/applications`)
  - AppImage applications (`~/Applications`, `~/.local/bin`, `~/AppImages`, `/opt`)

#### Configuration System
- **TOML configuration files** with user and system-wide support
- **GUI configuration interface** with tabbed settings dialog including:
  - **Hotkeys tab** with interactive keyboard shortcut capture
  - **Configuration tab** with direct config.toml editing and syntax highlighting
  - **About tab** with version, author, and donation information
- **Automatic configuration reloading** when files change
- **Configurable keyboard shortcuts** with modifier key support (default: Super+Space)
- **Performance tuning options** for CPU usage, memory limits, and batch processing

#### User Interface
- **System theme integration** that respects user's GTK theme
- **Modern CSS styling** with rounded corners, shadows, and smooth animations
- **Interactive shortcut capture** with real-time key detection and modifier support
- **Settings button integration** properly enabled in XFCE panel preferences
- **Escape key support** to cancel operations and close windows
- **Modal dialog behavior** during shortcut capture with proper focus management

#### Command Line Interface
- **Daemon management commands**:
  - `novasearch-daemon version` - Show version information
  - `novasearch-daemon about` - Show about information with license and website
  - `novasearch-daemon author` - Show author and contact information
  - `novasearch-daemon start` - Start the indexing daemon
  - `novasearch-daemon status` - Check daemon status
  - `novasearch-daemon reindex` - Force complete re-indexing
- **Systemd integration** with user service for automatic startup
- **Comprehensive logging** via systemd journal with debug support

#### Database Features
- **Usage statistics tracking** with launch count and last accessed timestamps
- **Smart ranking algorithm** that promotes frequently used files
- **Database schema versioning** with automatic migrations
- **Optimized queries** with proper indexing for fast search performance
- **Corruption recovery** with backup and rebuild capabilities

#### Build and Packaging
- **Debian package support** with complete .deb generation
- **Meson build system** for cross-platform compatibility
- **Automatic dependency handling** in packages
- **Multiple build methods** (package, manual, component-wise)
- **Comprehensive test suite** for both daemon and panel components

#### Developer Features
- **Property-based testing** support for correctness validation
- **Unit test coverage** for critical components
- **Debug build configurations** with symbols and logging
- **API documentation** for database schema and configuration
- **Development tools** for testing and debugging

### Technical Details

#### Architecture
- **Daemon**: Rust-based with tokio async runtime for high performance
- **Panel Plugin**: C with GTK3 for native XFCE integration
- **Database**: SQLite with optimized schema and indexing
- **IPC**: File-based communication between daemon and panel
- **Configuration**: TOML with automatic reloading and validation

#### Performance Optimizations
- **Configurable resource limits** for CPU and memory usage
- **Batch processing** for efficient database operations
- **Debounced search** to prevent excessive queries
- **Optimized file watching** with inotify for minimal overhead
- **Smart indexing** that skips unchanged files

#### Security and Reliability
- **Safe file operations** with proper error handling
- **Memory safety** through Rust's ownership system
- **Input validation** for all user-provided data
- **Graceful error recovery** with fallback mechanisms
- **Proper resource cleanup** to prevent leaks

### Installation and Compatibility

#### Supported Platforms
- **Ubuntu/Debian** (primary support with .deb packages)
- **Fedora** (build from source)
- **Arch Linux** (build from source)
- **Other Linux distributions** (manual build)

#### Dependencies
- **Runtime**: GTK3 ≥3.22, SQLite3 ≥3.0, XFCE4 Panel ≥4.12, systemd, keybinder-3.0
- **Build**: Rust 1.70+, GCC/Clang, Meson ≥0.59, Ninja, pkg-config, development libraries

#### File Locations
- **Daemon**: `/usr/bin/novasearch-daemon`
- **Panel Plugin**: `/usr/lib/x86_64-linux-gnu/xfce4/panel/plugins/libnovasearch-panel.so`
- **Desktop File**: `/usr/share/xfce4/panel/plugins/novasearch-panel.desktop`
- **Service**: `/usr/lib/systemd/user/novasearch-daemon.service`
- **Config**: `/etc/xdg/novasearch/config.toml` (system), `~/.config/novasearch/config.toml` (user)
- **Database**: `~/.local/share/novasearch/index.db`

### Author and Credits

**Author**: Kamil 'Novik' Nowicki
- **GitHub**: https://github.com/novik133
- **Project Repository**: https://github.com/novik133/NovaSearch
- **Support**: https://ko-fi.com/novadesktop

### License

This project is licensed under the GPL-3.0 License.

---

## Future Releases

Future versions will include additional features, performance improvements, and platform support. Check the project repository for development progress and upcoming features.