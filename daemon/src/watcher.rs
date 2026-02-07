use crate::config::Config;
use crate::models::{FileEntry, FileType, IndexOperation};
use notify::{Event, EventKind, RecommendedWatcher, RecursiveMode, Watcher};
use std::collections::{HashMap, VecDeque};
use std::path::{Path, PathBuf};
use std::sync::mpsc::{channel, Receiver, Sender};
use std::time::{Duration, Instant, SystemTime};
use glob::Pattern;

/// Filesystem watcher that monitors directories for changes
pub struct FilesystemWatcher {
    watcher: RecommendedWatcher,
    event_receiver: Receiver<FilesystemEvent>,
    watched_paths: Vec<PathBuf>,
}

/// Filesystem event types
#[derive(Debug, Clone)]
pub enum FilesystemEvent {
    Created(PathBuf),
    Modified(PathBuf),
    Deleted(PathBuf),
    Moved { from: PathBuf, to: PathBuf },
}

impl FilesystemWatcher {
    /// Create a new filesystem watcher
    pub fn new(config: &Config) -> Result<Self, WatcherError> {
        let (event_sender, event_receiver) = channel();
        
        // Create the notify watcher with event handler
        let watcher = Self::create_watcher(event_sender, config)?;
        
        Ok(FilesystemWatcher {
            watcher,
            event_receiver,
            watched_paths: Vec::new(),
        })
    }
    
    /// Create the underlying notify watcher
    fn create_watcher(
        event_sender: Sender<FilesystemEvent>,
        config: &Config,
    ) -> Result<RecommendedWatcher, WatcherError> {
        let exclude_patterns = config.indexing.exclude_patterns.clone();
        
        let watcher = notify::recommended_watcher(move |res: Result<Event, notify::Error>| {
            match res {
                Ok(event) => {
                    // Convert notify events to our FilesystemEvent type
                    if let Some(fs_event) = Self::convert_event(event, &exclude_patterns) {
                        let _ = event_sender.send(fs_event);
                    }
                }
                Err(e) => {
                    eprintln!("Filesystem watch error: {:?}", e);
                }
            }
        })
        .map_err(|e| WatcherError::InitializationError(e.to_string()))?;
        
        Ok(watcher)
    }
    
    /// Convert notify Event to FilesystemEvent, applying filters
    fn convert_event(event: Event, exclude_patterns: &[String]) -> Option<FilesystemEvent> {
        // Filter out events for excluded paths
        for path in &event.paths {
            if Self::should_exclude(path, exclude_patterns) {
                return None;
            }
        }
        
        match event.kind {
            EventKind::Create(_) => {
                if let Some(path) = event.paths.first() {
                    Some(FilesystemEvent::Created(path.clone()))
                } else {
                    None
                }
            }
            EventKind::Modify(_) => {
                if let Some(path) = event.paths.first() {
                    Some(FilesystemEvent::Modified(path.clone()))
                } else {
                    None
                }
            }
            EventKind::Remove(_) => {
                if let Some(path) = event.paths.first() {
                    Some(FilesystemEvent::Deleted(path.clone()))
                } else {
                    None
                }
            }
            EventKind::Access(_) => None, // Ignore access events
            EventKind::Any | EventKind::Other => None,
        }
    }
    
    /// Check if a path should be excluded based on patterns
    fn should_exclude(path: &Path, exclude_patterns: &[String]) -> bool {
        for pattern_str in exclude_patterns {
            // Check if any component of the path matches the pattern
            for component in path.components() {
                let component_str = component.as_os_str().to_string_lossy();
                
                // Try glob pattern matching
                if let Ok(pattern) = Pattern::new(pattern_str) {
                    if pattern.matches(&component_str) {
                        return true;
                    }
                }
                
                // Also do simple string matching for patterns like ".*" (hidden files)
                if pattern_str.starts_with(".*") && component_str.starts_with('.') {
                    return true;
                }
            }
        }
        
        false
    }
    
    /// Watch a directory recursively
    pub fn watch_path<P: AsRef<Path>>(&mut self, path: P) -> Result<(), WatcherError> {
        let path = path.as_ref();
        
        self.watcher
            .watch(path, RecursiveMode::Recursive)
            .map_err(|e| WatcherError::WatchError(format!("Failed to watch {:?}: {}", path, e)))?;
        
        self.watched_paths.push(path.to_path_buf());
        
        Ok(())
    }
    
    /// Watch multiple directories
    pub fn watch_paths(&mut self, paths: &[PathBuf]) -> Vec<WatcherError> {
        let mut errors = Vec::new();
        
        for path in paths {
            if let Err(e) = self.watch_path(path) {
                eprintln!("Warning: {}", e);
                errors.push(e);
            }
        }
        
        errors
    }
    
    /// Receive the next filesystem event (non-blocking)
    pub fn try_recv_event(&self) -> Option<FilesystemEvent> {
        self.event_receiver.try_recv().ok()
    }
    
    /// Receive the next filesystem event (blocking)
    pub fn recv_event(&self) -> Option<FilesystemEvent> {
        self.event_receiver.recv().ok()
    }
    
    /// Get list of watched paths
    pub fn watched_paths(&self) -> &[PathBuf] {
        &self.watched_paths
    }
}

/// Event processor that handles debouncing and converts events to IndexOperations
pub struct EventProcessor {
    pending_events: HashMap<PathBuf, (FilesystemEvent, Instant)>,
    debounce_duration: Duration,
    operation_queue: VecDeque<IndexOperation>,
    max_queue_size: usize,
}

impl EventProcessor {
    /// Create a new event processor
    pub fn new(debounce_duration: Duration, max_queue_size: usize) -> Self {
        EventProcessor {
            pending_events: HashMap::new(),
            debounce_duration,
            operation_queue: VecDeque::new(),
            max_queue_size,
        }
    }
    
    /// Add a filesystem event for processing
    pub fn add_event(&mut self, event: FilesystemEvent) {
        let path = match &event {
            FilesystemEvent::Created(p) => p.clone(),
            FilesystemEvent::Modified(p) => p.clone(),
            FilesystemEvent::Deleted(p) => p.clone(),
            FilesystemEvent::Moved { to, .. } => to.clone(),
        };
        
        // Store event with current timestamp for debouncing
        self.pending_events.insert(path, (event, Instant::now()));
    }
    
    /// Process pending events and convert to IndexOperations
    pub fn process_pending(&mut self) -> Vec<IndexOperation> {
        let now = Instant::now();
        let mut operations = Vec::new();
        
        // Find events that have been pending long enough
        let ready_paths: Vec<PathBuf> = self.pending_events
            .iter()
            .filter(|(_, (_, timestamp))| now.duration_since(*timestamp) >= self.debounce_duration)
            .map(|(path, _)| path.clone())
            .collect();
        
        // Convert ready events to operations
        for path in ready_paths {
            if let Some((event, _)) = self.pending_events.remove(&path) {
                if let Some(operation) = self.event_to_operation(event) {
                    operations.push(operation);
                }
            }
        }
        
        operations
    }
    
    /// Convert a FilesystemEvent to an IndexOperation
    fn event_to_operation(&self, event: FilesystemEvent) -> Option<IndexOperation> {
        match event {
            FilesystemEvent::Created(path) => {
                Self::create_file_entry(&path).map(IndexOperation::Add)
            }
            FilesystemEvent::Modified(path) => {
                Self::create_file_entry(&path).map(IndexOperation::Update)
            }
            FilesystemEvent::Deleted(path) => {
                Some(IndexOperation::Delete(path))
            }
            FilesystemEvent::Moved { from, to } => {
                Some(IndexOperation::Move { from, to })
            }
        }
    }
    
    /// Create a FileEntry from a path
    fn create_file_entry(path: &Path) -> Option<FileEntry> {
        // Check if file exists
        if !path.exists() {
            return None;
        }
        
        // Get metadata
        let metadata = std::fs::metadata(path).ok()?;
        
        // Extract filename
        let filename = path.file_name()?.to_string_lossy().to_string();
        
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
        
        // Get modification time
        let modified_time = metadata.modified().unwrap_or_else(|_| SystemTime::now());
        
        Some(FileEntry::new(
            filename,
            path.to_path_buf(),
            metadata.len(),
            modified_time,
            file_type,
        ))
    }
    
    /// Add an operation to the queue
    pub fn enqueue_operation(&mut self, operation: IndexOperation) -> Result<(), QueueError> {
        if self.operation_queue.len() >= self.max_queue_size {
            return Err(QueueError::QueueFull);
        }
        
        self.operation_queue.push_back(operation);
        Ok(())
    }
    
    /// Get the next operation from the queue
    pub fn dequeue_operation(&mut self) -> Option<IndexOperation> {
        self.operation_queue.pop_front()
    }
    
    /// Get the number of pending events
    pub fn pending_event_count(&self) -> usize {
        self.pending_events.len()
    }
    
    /// Get the number of queued operations
    pub fn queued_operation_count(&self) -> usize {
        self.operation_queue.len()
    }
    
    /// Clear all pending events and queued operations
    pub fn clear(&mut self) {
        self.pending_events.clear();
        self.operation_queue.clear();
    }
}

/// Watcher error types
#[derive(Debug)]
pub enum WatcherError {
    InitializationError(String),
    WatchError(String),
}

impl std::fmt::Display for WatcherError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            WatcherError::InitializationError(msg) => write!(f, "Watcher initialization error: {}", msg),
            WatcherError::WatchError(msg) => write!(f, "Watch error: {}", msg),
        }
    }
}

impl std::error::Error for WatcherError {}

/// Queue error types
#[derive(Debug)]
pub enum QueueError {
    QueueFull,
}

impl std::fmt::Display for QueueError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            QueueError::QueueFull => write!(f, "Operation queue is full"),
        }
    }
}

impl std::error::Error for QueueError {}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;
    use std::fs;
    
    #[test]
    fn test_should_exclude_hidden_files() {
        let exclude_patterns = vec![".*".to_string()];
        
        assert!(FilesystemWatcher::should_exclude(
            Path::new("/home/user/.hidden"),
            &exclude_patterns
        ));
        
        assert!(!FilesystemWatcher::should_exclude(
            Path::new("/home/user/visible"),
            &exclude_patterns
        ));
    }
    
    #[test]
    fn test_should_exclude_node_modules() {
        let exclude_patterns = vec!["node_modules".to_string()];
        
        assert!(FilesystemWatcher::should_exclude(
            Path::new("/home/user/project/node_modules/package"),
            &exclude_patterns
        ));
        
        assert!(!FilesystemWatcher::should_exclude(
            Path::new("/home/user/project/src"),
            &exclude_patterns
        ));
    }
    
    #[test]
    fn test_should_exclude_glob_patterns() {
        let exclude_patterns = vec!["*.log".to_string(), "*.tmp".to_string()];
        
        assert!(FilesystemWatcher::should_exclude(
            Path::new("/home/user/file.log"),
            &exclude_patterns
        ));
        
        assert!(FilesystemWatcher::should_exclude(
            Path::new("/home/user/temp.tmp"),
            &exclude_patterns
        ));
        
        assert!(!FilesystemWatcher::should_exclude(
            Path::new("/home/user/file.txt"),
            &exclude_patterns
        ));
    }
    
    #[test]
    fn test_event_processor_debouncing() {
        let temp_dir = TempDir::new().unwrap();
        let file_path = temp_dir.path().join("test.txt");
        fs::write(&file_path, "test").unwrap();
        
        let mut processor = EventProcessor::new(Duration::from_millis(100), 1000);
        
        // Add an event
        processor.add_event(FilesystemEvent::Created(file_path.clone()));
        
        // Immediately process - should not return anything (not debounced yet)
        let operations = processor.process_pending();
        assert_eq!(operations.len(), 0);
        assert_eq!(processor.pending_event_count(), 1);
        
        // Wait for debounce duration
        std::thread::sleep(Duration::from_millis(150));
        
        // Process again - should return the operation
        let operations = processor.process_pending();
        assert_eq!(operations.len(), 1);
        assert_eq!(processor.pending_event_count(), 0);
    }
    
    #[test]
    fn test_event_processor_queue() {
        let mut processor = EventProcessor::new(Duration::from_millis(50), 2);
        
        let op1 = IndexOperation::Delete(PathBuf::from("/test/file1.txt"));
        let op2 = IndexOperation::Delete(PathBuf::from("/test/file2.txt"));
        let op3 = IndexOperation::Delete(PathBuf::from("/test/file3.txt"));
        
        // Enqueue operations
        assert!(processor.enqueue_operation(op1).is_ok());
        assert!(processor.enqueue_operation(op2).is_ok());
        assert_eq!(processor.queued_operation_count(), 2);
        
        // Queue is full
        assert!(processor.enqueue_operation(op3).is_err());
        
        // Dequeue
        assert!(processor.dequeue_operation().is_some());
        assert_eq!(processor.queued_operation_count(), 1);
    }
    
    #[test]
    fn test_create_file_entry() {
        let temp_dir = TempDir::new().unwrap();
        let file_path = temp_dir.path().join("test.txt");
        fs::write(&file_path, "test content").unwrap();
        
        let entry = EventProcessor::create_file_entry(&file_path);
        assert!(entry.is_some());
        
        let entry = entry.unwrap();
        assert_eq!(entry.filename, "test.txt");
        assert_eq!(entry.path, file_path);
        assert_eq!(entry.file_type, FileType::Regular);
        assert!(entry.size > 0);
    }
    
    #[test]
    fn test_create_file_entry_directory() {
        let temp_dir = TempDir::new().unwrap();
        let dir_path = temp_dir.path().join("testdir");
        fs::create_dir(&dir_path).unwrap();
        
        let entry = EventProcessor::create_file_entry(&dir_path);
        assert!(entry.is_some());
        
        let entry = entry.unwrap();
        assert_eq!(entry.filename, "testdir");
        assert_eq!(entry.file_type, FileType::Directory);
    }
    
    #[test]
    fn test_create_file_entry_nonexistent() {
        let entry = EventProcessor::create_file_entry(Path::new("/nonexistent/file.txt"));
        assert!(entry.is_none());
    }
    
    #[test]
    fn test_event_to_operation() {
        let temp_dir = TempDir::new().unwrap();
        let file_path = temp_dir.path().join("test.txt");
        fs::write(&file_path, "test").unwrap();
        
        let processor = EventProcessor::new(Duration::from_millis(50), 100);
        
        // Test Created event
        let event = FilesystemEvent::Created(file_path.clone());
        let op = processor.event_to_operation(event);
        assert!(op.is_some());
        assert!(matches!(op.unwrap(), IndexOperation::Add(_)));
        
        // Test Modified event
        let event = FilesystemEvent::Modified(file_path.clone());
        let op = processor.event_to_operation(event);
        assert!(op.is_some());
        assert!(matches!(op.unwrap(), IndexOperation::Update(_)));
        
        // Test Deleted event
        let event = FilesystemEvent::Deleted(file_path.clone());
        let op = processor.event_to_operation(event);
        assert!(op.is_some());
        assert!(matches!(op.unwrap(), IndexOperation::Delete(_)));
        
        // Test Moved event
        let to_path = temp_dir.path().join("moved.txt");
        let event = FilesystemEvent::Moved {
            from: file_path.clone(),
            to: to_path.clone(),
        };
        let op = processor.event_to_operation(event);
        assert!(op.is_some());
        assert!(matches!(op.unwrap(), IndexOperation::Move { .. }));
    }
    
    #[test]
    fn test_filesystem_watcher_creation() {
        let config = Config::default();
        let watcher = FilesystemWatcher::new(&config);
        assert!(watcher.is_ok());
    }
    
    #[test]
    fn test_filesystem_watcher_watch_path() {
        let temp_dir = TempDir::new().unwrap();
        let config = Config::default();
        let mut watcher = FilesystemWatcher::new(&config).unwrap();
        
        let result = watcher.watch_path(temp_dir.path());
        assert!(result.is_ok());
        assert_eq!(watcher.watched_paths().len(), 1);
    }
    
    #[test]
    fn test_filesystem_watcher_watch_invalid_path() {
        let config = Config::default();
        let mut watcher = FilesystemWatcher::new(&config).unwrap();
        
        let result = watcher.watch_path("/nonexistent/path");
        assert!(result.is_err());
    }
    
    #[test]
    fn test_filesystem_watcher_watch_multiple_paths() {
        let temp_dir1 = TempDir::new().unwrap();
        let temp_dir2 = TempDir::new().unwrap();
        
        let config = Config::default();
        let mut watcher = FilesystemWatcher::new(&config).unwrap();
        
        let paths = vec![
            temp_dir1.path().to_path_buf(),
            temp_dir2.path().to_path_buf(),
        ];
        
        let errors = watcher.watch_paths(&paths);
        assert_eq!(errors.len(), 0);
        assert_eq!(watcher.watched_paths().len(), 2);
    }
    
    #[test]
    fn test_event_processor_clear() {
        let mut processor = EventProcessor::new(Duration::from_millis(50), 100);
        
        processor.add_event(FilesystemEvent::Created(PathBuf::from("/test/file.txt")));
        processor.enqueue_operation(IndexOperation::Delete(PathBuf::from("/test/other.txt"))).unwrap();
        
        assert_eq!(processor.pending_event_count(), 1);
        assert_eq!(processor.queued_operation_count(), 1);
        
        processor.clear();
        
        assert_eq!(processor.pending_event_count(), 0);
        assert_eq!(processor.queued_operation_count(), 0);
    }
}
