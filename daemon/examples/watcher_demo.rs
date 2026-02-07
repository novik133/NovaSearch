use novasearch_daemon::config::Config;
use novasearch_daemon::watcher::{FilesystemWatcher, EventProcessor};
use std::time::Duration;

fn main() {
    println!("NovaSearch Filesystem Watcher Demo");
    println!("===================================\n");
    
    // Load configuration
    let config = Config::default();
    println!("Configuration loaded:");
    println!("  Include paths: {:?}", config.indexing.include_paths);
    println!("  Exclude patterns: {:?}", config.indexing.exclude_patterns);
    println!();
    
    // Create filesystem watcher
    let mut watcher = match FilesystemWatcher::new(&config) {
        Ok(w) => {
            println!("✓ Filesystem watcher created successfully");
            w
        }
        Err(e) => {
            eprintln!("✗ Failed to create watcher: {}", e);
            return;
        }
    };
    
    // Watch current directory for demo purposes
    let watch_path = std::env::current_dir().unwrap();
    println!("Watching directory: {:?}", watch_path);
    
    if let Err(e) = watcher.watch_path(&watch_path) {
        eprintln!("✗ Failed to watch path: {}", e);
        return;
    }
    
    println!("✓ Successfully watching directory");
    println!("\nWaiting for filesystem events (press Ctrl+C to exit)...\n");
    
    // Create event processor
    let mut processor = EventProcessor::new(
        Duration::from_millis(200), // 200ms debounce
        1000, // max queue size
    );
    
    // Event loop
    loop {
        // Check for new events (non-blocking)
        if let Some(event) = watcher.try_recv_event() {
            println!("📁 Event received: {:?}", event);
            processor.add_event(event);
        }
        
        // Process pending events
        let operations = processor.process_pending();
        for operation in operations {
            println!("⚙️  Operation: {:?}", operation);
        }
        
        // Small sleep to avoid busy-waiting
        std::thread::sleep(Duration::from_millis(50));
    }
}
