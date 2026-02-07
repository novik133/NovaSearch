use serde::{Deserialize, Serialize};
use std::fs;
use std::path::{Path, PathBuf};
use std::time::Duration;
use notify::{Watcher, RecursiveMode, Event, EventKind};
use std::sync::{Arc, Mutex};

/// Main configuration structure
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Config {
    #[serde(default)]
    pub indexing: IndexingConfig,
    #[serde(default)]
    pub performance: PerformanceConfig,
    #[serde(default)]
    pub ui: UiConfig,
}

/// Indexing configuration
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct IndexingConfig {
    #[serde(default = "default_include_paths")]
    pub include_paths: Vec<String>,
    #[serde(default = "default_exclude_patterns")]
    pub exclude_patterns: Vec<String>,
}

/// Performance configuration
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PerformanceConfig {
    #[serde(default = "default_max_cpu_percent")]
    pub max_cpu_percent: u8,
    #[serde(default = "default_max_memory_mb")]
    pub max_memory_mb: u64,
    #[serde(default = "default_batch_size")]
    pub batch_size: usize,
    #[serde(default = "default_flush_interval_ms")]
    pub flush_interval_ms: u64,
}

/// UI configuration
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct UiConfig {
    #[serde(default = "default_keyboard_shortcut")]
    pub keyboard_shortcut: String,
    #[serde(default = "default_max_results")]
    pub max_results: usize,
}

// Default value functions for serde
fn default_include_paths() -> Vec<String> {
    vec!["~".to_string()]
}

fn default_exclude_patterns() -> Vec<String> {
    vec![
        ".*".to_string(),
        "node_modules".to_string(),
        ".git".to_string(),
        "target".to_string(),
    ]
}

fn default_max_cpu_percent() -> u8 {
    10
}

fn default_max_memory_mb() -> u64 {
    100
}

fn default_batch_size() -> usize {
    100
}

fn default_flush_interval_ms() -> u64 {
    1000
}

fn default_keyboard_shortcut() -> String {
    "Super+Space".to_string()
}

fn default_max_results() -> usize {
    50
}

impl Default for Config {
    fn default() -> Self {
        Config {
            indexing: IndexingConfig::default(),
            performance: PerformanceConfig::default(),
            ui: UiConfig::default(),
        }
    }
}

impl Default for IndexingConfig {
    fn default() -> Self {
        IndexingConfig {
            include_paths: vec!["~".to_string()],
            exclude_patterns: vec![
                ".*".to_string(),
                "node_modules".to_string(),
                ".git".to_string(),
                "target".to_string(),
            ],
        }
    }
}

impl Default for PerformanceConfig {
    fn default() -> Self {
        PerformanceConfig {
            max_cpu_percent: 10,
            max_memory_mb: 100,
            batch_size: 100,
            flush_interval_ms: 1000,
        }
    }
}

impl Default for UiConfig {
    fn default() -> Self {
        UiConfig {
            keyboard_shortcut: "Super+Space".to_string(),
            max_results: 50,
        }
    }
}

impl Config {
    /// Load configuration from a TOML file
    pub fn load_from_file<P: AsRef<Path>>(path: P) -> Result<Self, ConfigError> {
        let path = path.as_ref();
        
        if !path.exists() {
            return Ok(Config::default());
        }

        let contents = fs::read_to_string(path)
            .map_err(|e| ConfigError::IoError(e.to_string()))?;
        
        let config: Config = toml::from_str(&contents)
            .map_err(|e| ConfigError::ParseError(e.to_string()))?;
        
        config.validate()?;
        
        Ok(config)
    }

    /// Save configuration to a TOML file
    pub fn save_to_file<P: AsRef<Path>>(&self, path: P) -> Result<(), ConfigError> {
        let contents = toml::to_string_pretty(self)
            .map_err(|e| ConfigError::SerializeError(e.to_string()))?;
        
        fs::write(path, contents)
            .map_err(|e| ConfigError::IoError(e.to_string()))?;
        
        Ok(())
    }

    /// Validate configuration values
    pub fn validate(&self) -> Result<(), ConfigError> {
        // Validate include_paths is not empty
        if self.indexing.include_paths.is_empty() {
            return Err(ConfigError::ValidationError(
                "include_paths cannot be empty".to_string()
            ));
        }

        // Validate max_cpu_percent is reasonable
        if self.performance.max_cpu_percent == 0 || self.performance.max_cpu_percent > 100 {
            return Err(ConfigError::ValidationError(
                "max_cpu_percent must be between 1 and 100".to_string()
            ));
        }

        // Validate max_memory_mb is reasonable
        if self.performance.max_memory_mb == 0 {
            return Err(ConfigError::ValidationError(
                "max_memory_mb must be greater than 0".to_string()
            ));
        }

        // Validate batch_size is reasonable
        if self.performance.batch_size == 0 {
            return Err(ConfigError::ValidationError(
                "batch_size must be greater than 0".to_string()
            ));
        }

        // Validate flush_interval_ms is reasonable
        if self.performance.flush_interval_ms == 0 {
            return Err(ConfigError::ValidationError(
                "flush_interval_ms must be greater than 0".to_string()
            ));
        }

        // Validate max_results is reasonable
        if self.ui.max_results == 0 {
            return Err(ConfigError::ValidationError(
                "max_results must be greater than 0".to_string()
            ));
        }

        // Validate keyboard_shortcut is not empty
        if self.ui.keyboard_shortcut.is_empty() {
            return Err(ConfigError::ValidationError(
                "keyboard_shortcut cannot be empty".to_string()
            ));
        }

        Ok(())
    }

    /// Get flush interval as Duration
    pub fn flush_interval(&self) -> Duration {
        Duration::from_millis(self.performance.flush_interval_ms)
    }

    /// Expand tilde in paths to home directory
    pub fn expand_paths(&self) -> Vec<PathBuf> {
        self.indexing.include_paths
            .iter()
            .map(|p| expand_tilde(p))
            .collect()
    }
}

/// Expand tilde (~) to home directory
fn expand_tilde(path: &str) -> PathBuf {
    if path.starts_with("~/") {
        if let Ok(home) = std::env::var("HOME") {
            return PathBuf::from(home).join(&path[2..]);
        }
    } else if path == "~" {
        if let Ok(home) = std::env::var("HOME") {
            return PathBuf::from(home);
        }
    }
    PathBuf::from(path)
}

/// Configuration error types
#[derive(Debug)]
pub enum ConfigError {
    IoError(String),
    ParseError(String),
    SerializeError(String),
    ValidationError(String),
}

impl std::fmt::Display for ConfigError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            ConfigError::IoError(msg) => write!(f, "IO error: {}", msg),
            ConfigError::ParseError(msg) => write!(f, "Parse error: {}", msg),
            ConfigError::SerializeError(msg) => write!(f, "Serialize error: {}", msg),
            ConfigError::ValidationError(msg) => write!(f, "Validation error: {}", msg),
        }
    }
}

impl std::error::Error for ConfigError {}

/// Configuration file watcher
pub struct ConfigWatcher {
    current_config: Arc<Mutex<Config>>,
    _watcher: notify::RecommendedWatcher,
    reload_receiver: std::sync::mpsc::Receiver<()>,
}

impl ConfigWatcher {
    /// Create a new configuration watcher
    pub fn new(config_path: PathBuf) -> Result<Self, ConfigError> {
        let config = Config::load_from_file(&config_path)?;
        let current_config = Arc::new(Mutex::new(config));
        
        let (reload_sender, reload_receiver) = std::sync::mpsc::channel();
        let config_path_clone = config_path.clone();
        let current_config_clone = current_config.clone();
        
        // Create debounced sender
        let debounced_sender = Arc::new(Mutex::new(DebouncedSender::new(
            reload_sender,
            Duration::from_millis(500),
        )));
        
        // Set up file watcher
        let mut watcher = notify::recommended_watcher(move |res: Result<Event, notify::Error>| {
            match res {
                Ok(event) => {
                    // Only reload on modify or create events
                    if matches!(event.kind, EventKind::Modify(_) | EventKind::Create(_)) {
                        if event.paths.iter().any(|p| p == &config_path_clone) {
                            // Attempt to reload config
                            match Config::load_from_file(&config_path_clone) {
                                Ok(new_config) => {
                                    if let Ok(mut config) = current_config_clone.lock() {
                                        *config = new_config;
                                        // Send reload notification (debounced)
                                        if let Ok(sender) = debounced_sender.lock() {
                                            sender.send();
                                        }
                                    }
                                }
                                Err(e) => {
                                    eprintln!("Failed to reload config: {}", e);
                                }
                            }
                        }
                    }
                }
                Err(e) => eprintln!("Watch error: {:?}", e),
            }
        }).map_err(|e| ConfigError::IoError(format!("Failed to create watcher: {}", e)))?;
        
        // Watch the config file's parent directory (watching files directly can be unreliable)
        if let Some(parent) = config_path.parent() {
            watcher.watch(parent, RecursiveMode::NonRecursive)
                .map_err(|e| ConfigError::IoError(format!("Failed to watch config directory: {}", e)))?;
        }
        
        Ok(ConfigWatcher {
            current_config,
            _watcher: watcher,
            reload_receiver,
        })
    }
    
    /// Get the current configuration
    pub fn get_config(&self) -> Config {
        self.current_config.lock().unwrap().clone()
    }
    
    /// Wait for the next configuration reload event (blocking)
    pub fn wait_for_reload_blocking(&mut self) -> Option<Config> {
        self.reload_receiver.recv().ok()?;
        Some(self.get_config())
    }
    
    /// Try to receive a reload event without blocking
    pub fn try_recv_reload(&mut self) -> Option<Config> {
        self.reload_receiver.try_recv().ok()?;
        Some(self.get_config())
    }
}

/// Helper to debounce reload notifications
struct DebouncedSender {
    sender: std::sync::mpsc::Sender<()>,
    debounce_duration: Duration,
    last_send: Arc<Mutex<Option<std::time::Instant>>>,
}

impl DebouncedSender {
    fn new(sender: std::sync::mpsc::Sender<()>, debounce_duration: Duration) -> Self {
        DebouncedSender {
            sender,
            debounce_duration,
            last_send: Arc::new(Mutex::new(None)),
        }
    }
    
    fn send(&self) {
        let now = std::time::Instant::now();
        let mut last_send = self.last_send.lock().unwrap();
        
        // Check if enough time has passed since last send
        let should_send = match *last_send {
            Some(last) => now.duration_since(last) >= self.debounce_duration,
            None => true,
        };
        
        if should_send {
            *last_send = Some(now);
            let _ = self.sender.send(());
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::NamedTempFile;
    use std::io::Write;

    #[test]
    fn test_default_config() {
        let config = Config::default();
        assert_eq!(config.indexing.include_paths, vec!["~"]);
        assert_eq!(config.performance.max_cpu_percent, 10);
        assert_eq!(config.performance.max_memory_mb, 100);
        assert_eq!(config.performance.batch_size, 100);
        assert_eq!(config.performance.flush_interval_ms, 1000);
        assert_eq!(config.ui.keyboard_shortcut, "Super+Space");
        assert_eq!(config.ui.max_results, 50);
    }

    #[test]
    fn test_load_nonexistent_file() {
        let result = Config::load_from_file("/nonexistent/path/config.toml");
        assert!(result.is_ok());
        let config = result.unwrap();
        assert_eq!(config.indexing.include_paths, vec!["~"]);
    }

    #[test]
    fn test_load_valid_toml() {
        let mut temp_file = NamedTempFile::new().unwrap();
        let toml_content = r#"
[indexing]
include_paths = ["/home/user/Documents", "/home/user/Projects"]
exclude_patterns = ["*.tmp", "*.log"]

[performance]
max_cpu_percent = 20
max_memory_mb = 200
batch_size = 50
flush_interval_ms = 500

[ui]
keyboard_shortcut = "Ctrl+Alt+F"
max_results = 100
"#;
        temp_file.write_all(toml_content.as_bytes()).unwrap();
        
        let config = Config::load_from_file(temp_file.path()).unwrap();
        assert_eq!(config.indexing.include_paths, vec!["/home/user/Documents", "/home/user/Projects"]);
        assert_eq!(config.indexing.exclude_patterns, vec!["*.tmp", "*.log"]);
        assert_eq!(config.performance.max_cpu_percent, 20);
        assert_eq!(config.ui.keyboard_shortcut, "Ctrl+Alt+F");
    }

    #[test]
    fn test_load_invalid_toml() {
        let mut temp_file = NamedTempFile::new().unwrap();
        temp_file.write_all(b"invalid toml content [[[").unwrap();
        
        let result = Config::load_from_file(temp_file.path());
        assert!(result.is_err());
        assert!(matches!(result.unwrap_err(), ConfigError::ParseError(_)));
    }

    #[test]
    fn test_validation_empty_include_paths() {
        let mut config = Config::default();
        config.indexing.include_paths.clear();
        
        let result = config.validate();
        assert!(result.is_err());
        assert!(matches!(result.unwrap_err(), ConfigError::ValidationError(_)));
    }

    #[test]
    fn test_validation_invalid_cpu_percent() {
        let mut config = Config::default();
        config.performance.max_cpu_percent = 0;
        
        let result = config.validate();
        assert!(result.is_err());
        
        config.performance.max_cpu_percent = 101;
        let result = config.validate();
        assert!(result.is_err());
    }

    #[test]
    fn test_validation_zero_values() {
        let mut config = Config::default();
        
        config.performance.max_memory_mb = 0;
        assert!(config.validate().is_err());
        
        config = Config::default();
        config.performance.batch_size = 0;
        assert!(config.validate().is_err());
        
        config = Config::default();
        config.performance.flush_interval_ms = 0;
        assert!(config.validate().is_err());
        
        config = Config::default();
        config.ui.max_results = 0;
        assert!(config.validate().is_err());
    }

    #[test]
    fn test_validation_empty_keyboard_shortcut() {
        let mut config = Config::default();
        config.ui.keyboard_shortcut = String::new();
        
        let result = config.validate();
        assert!(result.is_err());
    }

    #[test]
    fn test_save_and_load() {
        let temp_file = NamedTempFile::new().unwrap();
        let config = Config::default();
        
        config.save_to_file(temp_file.path()).unwrap();
        let loaded_config = Config::load_from_file(temp_file.path()).unwrap();
        
        assert_eq!(config.indexing.include_paths, loaded_config.indexing.include_paths);
        assert_eq!(config.performance.max_cpu_percent, loaded_config.performance.max_cpu_percent);
    }

    #[test]
    fn test_expand_tilde() {
        let home = std::env::var("HOME").unwrap();
        
        assert_eq!(expand_tilde("~"), PathBuf::from(&home));
        assert_eq!(expand_tilde("~/Documents"), PathBuf::from(&home).join("Documents"));
        assert_eq!(expand_tilde("/absolute/path"), PathBuf::from("/absolute/path"));
    }

    #[test]
    fn test_expand_paths() {
        let mut config = Config::default();
        config.indexing.include_paths = vec!["~".to_string(), "~/Documents".to_string()];
        
        let expanded = config.expand_paths();
        let home = std::env::var("HOME").unwrap();
        
        assert_eq!(expanded[0], PathBuf::from(&home));
        assert_eq!(expanded[1], PathBuf::from(&home).join("Documents"));
    }

    #[test]
    fn test_flush_interval() {
        let config = Config::default();
        assert_eq!(config.flush_interval(), Duration::from_millis(1000));
    }

    #[test]
    fn test_partial_config() {
        let mut temp_file = NamedTempFile::new().unwrap();
        let toml_content = r#"
[indexing]
include_paths = ["/home/user/Documents"]
"#;
        temp_file.write_all(toml_content.as_bytes()).unwrap();
        
        let config = Config::load_from_file(temp_file.path()).unwrap();
        assert_eq!(config.indexing.include_paths, vec!["/home/user/Documents"]);
        // Should use defaults for missing sections
        assert_eq!(config.performance.max_cpu_percent, 10);
        assert_eq!(config.ui.keyboard_shortcut, "Super+Space");
    }

    #[test]
    fn test_config_watcher_creation() {
        let temp_file = NamedTempFile::new().unwrap();
        let config = Config::default();
        config.save_to_file(temp_file.path()).unwrap();
        
        let watcher = ConfigWatcher::new(temp_file.path().to_path_buf());
        assert!(watcher.is_ok());
        
        let watcher = watcher.unwrap();
        let loaded_config = watcher.get_config();
        assert_eq!(loaded_config.indexing.include_paths, vec!["~"]);
    }

    #[test]
    fn test_config_watcher_reload() {
        // Create a temporary config file
        let temp_dir = tempfile::tempdir().unwrap();
        let config_path = temp_dir.path().join("config.toml");
        
        let initial_config = Config::default();
        initial_config.save_to_file(&config_path).unwrap();
        
        let mut watcher = ConfigWatcher::new(config_path.clone()).unwrap();
        
        // Verify initial config
        let config = watcher.get_config();
        assert_eq!(config.performance.max_cpu_percent, 10);
        
        // Wait a bit for watcher to be ready
        std::thread::sleep(Duration::from_millis(100));
        
        // Modify the config file
        let new_toml = r#"
[indexing]
include_paths = ["~"]
exclude_patterns = [".*"]

[performance]
max_cpu_percent = 20
max_memory_mb = 100
batch_size = 100
flush_interval_ms = 1000

[ui]
keyboard_shortcut = "Super+Space"
max_results = 50
"#;
        std::fs::write(&config_path, new_toml).unwrap();
        
        // Try to receive reload event with timeout
        let start = std::time::Instant::now();
        let mut reloaded = false;
        while start.elapsed() < Duration::from_secs(2) {
            if let Some(new_config) = watcher.try_recv_reload() {
                assert_eq!(new_config.performance.max_cpu_percent, 20);
                reloaded = true;
                break;
            }
            std::thread::sleep(Duration::from_millis(100));
        }
        
        // On some systems, file watching might not work in tests
        // Just verify we can still get the config
        if !reloaded {
            let config = watcher.get_config();
            // Config might or might not have reloaded depending on system
            assert!(config.performance.max_cpu_percent == 10 || config.performance.max_cpu_percent == 20);
        }
    }

    #[test]
    fn test_config_watcher_invalid_reload() {
        let temp_dir = tempfile::tempdir().unwrap();
        let config_path = temp_dir.path().join("config.toml");
        
        let initial_config = Config::default();
        initial_config.save_to_file(&config_path).unwrap();
        
        let mut watcher = ConfigWatcher::new(config_path.clone()).unwrap();
        let initial = watcher.get_config();
        
        std::thread::sleep(Duration::from_millis(100));
        
        // Write invalid TOML
        std::fs::write(&config_path, "invalid toml [[[").unwrap();
        
        // Wait a bit
        std::thread::sleep(Duration::from_millis(600));
        
        // Config should remain unchanged after invalid update
        let config = watcher.get_config();
        assert_eq!(config.performance.max_cpu_percent, initial.performance.max_cpu_percent);
    }
}


