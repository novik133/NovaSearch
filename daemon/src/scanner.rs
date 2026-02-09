use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};
use std::time::SystemTime;
use walkdir::{WalkDir, DirEntry};
use glob::Pattern;
use crate::models::{FileEntry, FileType};
use crate::config::Config;

/// Progress tracking for filesystem scanning
#[derive(Debug, Clone)]
pub struct ScanProgress {
    pub files_scanned: usize,
    pub directories_scanned: usize,
    pub errors_encountered: usize,
    pub current_path: Option<PathBuf>,
}

impl ScanProgress {
    pub fn new() -> Self {
        ScanProgress {
            files_scanned: 0,
            directories_scanned: 0,
            errors_encountered: 0,
            current_path: None,
        }
    }
}

/// Filesystem scanner for initial indexing
pub struct Scanner {
    config: Config,
    progress: Arc<Mutex<ScanProgress>>,
}

impl Scanner {
    /// Create a new scanner with the given configuration
    pub fn new(config: Config) -> Self {
        Scanner {
            config,
            progress: Arc::new(Mutex::new(ScanProgress::new())),
        }
    }

    /// Get a clone of the current progress
    pub fn get_progress(&self) -> ScanProgress {
        self.progress.lock().unwrap().clone()
    }

    /// Scan all configured directories and return file entries
    pub fn scan(&self) -> Vec<FileEntry> {
        let mut entries = Vec::new();
        
        // Always scan application directories first (regardless of user config)
        let app_dirs = self.get_application_directories();
        for path in app_dirs {
            if path.exists() {
                entries.extend(self.scan_application_directory(&path));
            }
        }
        
        // Then scan user-configured paths
        let include_paths = self.config.expand_paths();
        for path in include_paths {
            if path.exists() {
                entries.extend(self.scan_directory(&path));
            } else {
                eprintln!("Warning: Include path does not exist: {}", path.display());
            }
        }

        entries
    }

    /// Get standard application directories that contain .desktop files
    fn get_application_directories(&self) -> Vec<PathBuf> {
        let mut app_dirs = Vec::new();
        
        // System application directories
        app_dirs.push(PathBuf::from("/usr/share/applications"));
        app_dirs.push(PathBuf::from("/usr/local/share/applications"));
        
        // User application directory
        if let Ok(home) = std::env::var("HOME") {
            let home_path = PathBuf::from(&home);
            app_dirs.push(home_path.join(".local/share/applications"));
        }
        
        // Snap applications
        app_dirs.push(PathBuf::from("/var/lib/snapd/desktop/applications"));
        if let Ok(home) = std::env::var("HOME") {
            let home_path = PathBuf::from(&home);
            app_dirs.push(home_path.join("snap"));
        }
        
        // Flatpak applications
        app_dirs.push(PathBuf::from("/var/lib/flatpak/exports/share/applications"));
        if let Ok(home) = std::env::var("HOME") {
            let home_path = PathBuf::from(&home);
            app_dirs.push(home_path.join(".local/share/flatpak/exports/share/applications"));
        }
        
        // AppImage applications (common locations)
        if let Ok(home) = std::env::var("HOME") {
            let home_path = PathBuf::from(&home);
            app_dirs.push(home_path.join("Applications"));
            app_dirs.push(home_path.join(".local/bin"));
            app_dirs.push(home_path.join("AppImages"));
        }
        app_dirs.push(PathBuf::from("/opt"));
        
        app_dirs
    }

    /// Scan application directory specifically for .desktop files and AppImages
    fn scan_application_directory(&self, path: &Path) -> Vec<FileEntry> {
        let mut entries = Vec::new();
        
        for entry_result in WalkDir::new(path)
            .follow_links(false)
            .into_iter()
        {
            match entry_result {
                Ok(entry) => {
                    let entry_path = entry.path();
                    
                    // Update progress
                    {
                        let mut progress = self.progress.lock().unwrap();
                        progress.current_path = Some(entry_path.to_path_buf());
                        
                        if entry.file_type().is_dir() {
                            progress.directories_scanned += 1;
                        } else {
                            progress.files_scanned += 1;
                        }
                    }

                    // Check if this is a .desktop file or AppImage
                    let should_include = if entry.file_type().is_file() {
                        if let Some(extension) = entry_path.extension() {
                            extension == "desktop" || extension == "AppImage"
                        } else {
                            // Check if it's an AppImage without extension
                            if let Some(filename) = entry_path.file_name() {
                                let filename_str = filename.to_string_lossy();
                                filename_str.contains("AppImage") || 
                                self.is_appimage_file(entry_path)
                            } else {
                                false
                            }
                        }
                    } else {
                        // Include directories in application paths
                        true
                    };

                    if should_include {
                        if let Some(file_entry) = self.extract_file_entry(&entry) {
                            entries.push(file_entry);
                        }
                    }
                }
                Err(err) => {
                    // Handle permission errors gracefully for system directories
                    if !err.to_string().contains("Permission denied") {
                        eprintln!("Warning: Failed to access application path: {}", err);
                    }
                    let mut progress = self.progress.lock().unwrap();
                    progress.errors_encountered += 1;
                }
            }
        }

        entries
    }

    /// Check if a file is an AppImage by examining its content
    fn is_appimage_file(&self, path: &Path) -> bool {
        use std::fs::File;
        use std::io::Read;
        
        if let Ok(mut file) = File::open(path) {
            let mut buffer = [0; 1024];
            if let Ok(bytes_read) = file.read(&mut buffer) {
                let content = String::from_utf8_lossy(&buffer[..bytes_read]);
                // AppImages typically contain "AppImage" in their header or have ELF magic
                return content.contains("AppImage") || buffer.starts_with(b"\x7fELF");
            }
        }
        false
    }

    /// Scan a single directory recursively
    fn scan_directory(&self, path: &Path) -> Vec<FileEntry> {
        let mut entries = Vec::new();
        
        // Create glob patterns for exclusion
        let exclude_patterns: Vec<Pattern> = self.config.indexing.exclude_patterns
            .iter()
            .filter_map(|pattern| {
                Pattern::new(pattern).ok()
            })
            .collect();

        let root_path = path.to_path_buf();

        for entry_result in WalkDir::new(path)
            .follow_links(false)
            .into_iter()
            .filter_entry(|e| self.should_include_entry(e, &exclude_patterns, &root_path))
        {
            match entry_result {
                Ok(entry) => {
                    // Update progress
                    {
                        let mut progress = self.progress.lock().unwrap();
                        progress.current_path = Some(entry.path().to_path_buf());
                        
                        if entry.file_type().is_dir() {
                            progress.directories_scanned += 1;
                        } else {
                            progress.files_scanned += 1;
                        }
                    }

                    // Extract file entry
                    if let Some(file_entry) = self.extract_file_entry(&entry) {
                        entries.push(file_entry);
                    }
                }
                Err(err) => {
                    // Handle permission errors and other issues gracefully
                    eprintln!("Warning: Failed to access path: {}", err);
                    let mut progress = self.progress.lock().unwrap();
                    progress.errors_encountered += 1;
                }
            }
        }

        entries
    }

    /// Check if an entry should be included based on exclude patterns
    fn should_include_entry(&self, entry: &DirEntry, exclude_patterns: &[Pattern], root_path: &Path) -> bool {
        let path = entry.path();
        
        // Always include the root directory itself
        if path == root_path {
            return true;
        }
        
        // Get the file/directory name
        let name = match path.file_name() {
            Some(n) => n.to_string_lossy(),
            None => return true,
        };

        // Check against exclude patterns
        for pattern in exclude_patterns {
            // Check if the name matches the pattern
            if pattern.matches(&name) {
                return false;
            }
        }

        true
    }

    /// Extract file entry from a directory entry
    fn extract_file_entry(&self, entry: &DirEntry) -> Option<FileEntry> {
        let path = entry.path();
        
        // Get filename
        let filename = path.file_name()?.to_string_lossy().to_string();
        
        // Get metadata
        let metadata = match entry.metadata() {
            Ok(m) => m,
            Err(err) => {
                eprintln!("Warning: Failed to get metadata for {}: {}", path.display(), err);
                return None;
            }
        };

        // Get file size
        let size = metadata.len();

        // Get modification time
        let modified_time = metadata.modified().unwrap_or_else(|_| SystemTime::now());

        // Determine file type
        let file_type = if metadata.is_dir() {
            FileType::Directory
        } else if metadata.is_symlink() {
            FileType::Symlink
        } else if metadata.is_file() {
            FileType::Regular
        } else {
            FileType::Other
        };

        Some(FileEntry::new(
            filename,
            path.to_path_buf(),
            size,
            modified_time,
            file_type,
        ))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;
    use std::fs;

    fn create_test_directory_structure(base: &Path) {
        // Create some directories
        fs::create_dir_all(base.join("documents")).unwrap();
        fs::create_dir_all(base.join("projects/rust")).unwrap();
        fs::create_dir_all(base.join(".hidden")).unwrap();
        fs::create_dir_all(base.join("node_modules/package")).unwrap();

        // Create some files
        fs::write(base.join("readme.txt"), "test content").unwrap();
        fs::write(base.join("documents/file1.txt"), "content 1").unwrap();
        fs::write(base.join("documents/file2.txt"), "content 2").unwrap();
        fs::write(base.join("projects/rust/main.rs"), "fn main() {}").unwrap();
        fs::write(base.join(".hidden/secret.txt"), "secret").unwrap();
        fs::write(base.join("node_modules/package/index.js"), "module.exports = {}").unwrap();
    }

    #[test]
    fn test_scanner_basic() {
        let temp_dir = TempDir::new().unwrap();
        create_test_directory_structure(temp_dir.path());

        let mut config = Config::default();
        config.indexing.include_paths = vec![temp_dir.path().to_string_lossy().to_string()];
        config.indexing.exclude_patterns = vec![];

        let scanner = Scanner::new(config);
        let entries = scanner.scan();

        // Should find all files and directories
        assert!(entries.len() > 0);
        
        // Check that we found some specific files
        let filenames: Vec<String> = entries.iter().map(|e| e.filename.clone()).collect();
        assert!(filenames.contains(&"readme.txt".to_string()));
        assert!(filenames.contains(&"file1.txt".to_string()));
        assert!(filenames.contains(&"main.rs".to_string()));
    }

    #[test]
    fn test_scanner_with_exclusions() {
        let temp_dir = TempDir::new().unwrap();
        create_test_directory_structure(temp_dir.path());

        let mut config = Config::default();
        config.indexing.include_paths = vec![temp_dir.path().to_string_lossy().to_string()];
        config.indexing.exclude_patterns = vec![".*".to_string(), "node_modules".to_string()];

        let scanner = Scanner::new(config);
        let entries = scanner.scan();

        // Check that hidden files and node_modules are excluded
        let filenames: Vec<String> = entries.iter().map(|e| e.filename.clone()).collect();
        
        assert!(!filenames.contains(&"secret.txt".to_string()));
        assert!(!filenames.contains(&"index.js".to_string()));
        
        // But regular files should be included
        assert!(filenames.contains(&"readme.txt".to_string()));
        assert!(filenames.contains(&"file1.txt".to_string()));
    }

    #[test]
    fn test_scanner_file_types() {
        let temp_dir = TempDir::new().unwrap();
        fs::write(temp_dir.path().join("file.txt"), "content").unwrap();
        fs::create_dir(temp_dir.path().join("directory")).unwrap();

        let mut config = Config::default();
        config.indexing.include_paths = vec![temp_dir.path().to_string_lossy().to_string()];
        config.indexing.exclude_patterns = vec![];

        let scanner = Scanner::new(config);
        let entries = scanner.scan();

        // Find the file and directory entries
        let file_entry = entries.iter().find(|e| e.filename == "file.txt");
        let dir_entry = entries.iter().find(|e| e.filename == "directory");

        assert!(file_entry.is_some());
        assert_eq!(file_entry.unwrap().file_type, FileType::Regular);

        assert!(dir_entry.is_some());
        assert_eq!(dir_entry.unwrap().file_type, FileType::Directory);
    }

    #[test]
    fn test_scanner_metadata() {
        let temp_dir = TempDir::new().unwrap();
        let file_path = temp_dir.path().join("test.txt");
        let content = "test content";
        fs::write(&file_path, content).unwrap();

        let mut config = Config::default();
        config.indexing.include_paths = vec![temp_dir.path().to_string_lossy().to_string()];
        config.indexing.exclude_patterns = vec![];

        let scanner = Scanner::new(config);
        let entries = scanner.scan();

        let file_entry = entries.iter().find(|e| e.filename == "test.txt");
        assert!(file_entry.is_some());

        let entry = file_entry.unwrap();
        assert_eq!(entry.size, content.len() as u64);
        assert_eq!(entry.path, file_path);
        assert!(entry.modified_time <= SystemTime::now());
    }

    #[test]
    fn test_scanner_progress_tracking() {
        let temp_dir = TempDir::new().unwrap();
        create_test_directory_structure(temp_dir.path());

        let mut config = Config::default();
        config.indexing.include_paths = vec![temp_dir.path().to_string_lossy().to_string()];
        config.indexing.exclude_patterns = vec![];

        let scanner = Scanner::new(config);
        let _entries = scanner.scan();

        let progress = scanner.get_progress();
        assert!(progress.files_scanned > 0);
        assert!(progress.directories_scanned > 0);
    }

    #[test]
    fn test_scanner_nonexistent_path() {
        let mut config = Config::default();
        config.indexing.include_paths = vec!["/nonexistent/path/that/does/not/exist".to_string()];
        config.indexing.exclude_patterns = vec![];

        let scanner = Scanner::new(config);
        let entries = scanner.scan();

        // Should return empty vec, not crash
        assert_eq!(entries.len(), 0);
    }

    #[test]
    fn test_scanner_empty_directory() {
        let temp_dir = TempDir::new().unwrap();

        let mut config = Config::default();
        config.indexing.include_paths = vec![temp_dir.path().to_string_lossy().to_string()];
        config.indexing.exclude_patterns = vec![];

        let scanner = Scanner::new(config);
        let entries = scanner.scan();

        // Should find at least the root directory itself
        assert!(entries.len() >= 1);
    }

    #[test]
    fn test_scanner_glob_patterns() {
        let temp_dir = TempDir::new().unwrap();
        fs::write(temp_dir.path().join("file.txt"), "content").unwrap();
        fs::write(temp_dir.path().join("file.log"), "log content").unwrap();
        fs::write(temp_dir.path().join("file.tmp"), "temp content").unwrap();

        let mut config = Config::default();
        config.indexing.include_paths = vec![temp_dir.path().to_string_lossy().to_string()];
        config.indexing.exclude_patterns = vec!["*.log".to_string(), "*.tmp".to_string()];

        let scanner = Scanner::new(config);
        let entries = scanner.scan();

        let filenames: Vec<String> = entries.iter().map(|e| e.filename.clone()).collect();
        assert!(filenames.contains(&"file.txt".to_string()));
        assert!(!filenames.contains(&"file.log".to_string()));
        assert!(!filenames.contains(&"file.tmp".to_string()));
    }
}
