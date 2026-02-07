mod paths;
mod database;
mod models;
mod config;
mod watcher;
mod scanner;

use clap::{Parser, Subcommand};
use std::path::PathBuf;
use std::sync::Arc;
use tokio::sync::Mutex;
use tokio::time::{interval, Duration};
use std::sync::atomic::{AtomicBool, Ordering};

use config::{Config, ConfigWatcher};
use database::Database;
use watcher::{FilesystemWatcher, EventProcessor};
use scanner::Scanner;

/// NovaSearch Indexing Daemon
#[derive(Parser)]
#[command(name = "novasearch-daemon")]
#[command(about = "NovaSearch filesystem indexing daemon", long_about = None)]
struct Cli {
    /// Path to configuration file
    #[arg(short, long, value_name = "FILE")]
    config: Option<PathBuf>,

    #[command(subcommand)]
    command: Commands,
}

#[derive(Subcommand)]
enum Commands {
    /// Start the indexing daemon
    Start,
    /// Query indexing status
    Status,
    /// Force a full re-index
    Reindex,
}

/// Main daemon structure
struct IndexingDaemon {
    db: Arc<Database>,
    watcher: Arc<Mutex<FilesystemWatcher>>,
    config: Config,
    event_processor: Arc<Mutex<EventProcessor>>,
    running: Arc<AtomicBool>,
}

impl IndexingDaemon {
    /// Create a new indexing daemon
    async fn new(config: Config) -> Result<Self, Box<dyn std::error::Error>> {
        // Open database
        let db_path = paths::get_database_path();
        if let Some(parent) = db_path.parent() {
            std::fs::create_dir_all(parent)?;
        }
        let db = Arc::new(Database::open(&db_path)?);

        // Create filesystem watcher
        let watcher = Arc::new(Mutex::new(FilesystemWatcher::new(&config)?));

        // Create event processor
        let debounce_duration = Duration::from_millis(200);
        let max_queue_size = 10000;
        let event_processor = Arc::new(Mutex::new(EventProcessor::new(
            debounce_duration,
            max_queue_size,
        )));

        let running = Arc::new(AtomicBool::new(true));

        Ok(IndexingDaemon {
            db,
            watcher,
            config,
            event_processor,
            running,
        })
    }

    /// Initialize the daemon (perform initial scan and start watching)
    async fn initialize(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        println!("Initializing NovaSearch daemon...");

        // Perform initial filesystem scan
        println!("Performing initial filesystem scan...");
        let scanner = Scanner::new(self.config.clone());
        let entries = scanner.scan();
        println!("Found {} files/directories", entries.len());

        // Batch insert entries into database
        println!("Indexing files...");
        let batch_size = self.config.performance.batch_size;
        for chunk in entries.chunks(batch_size) {
            let operations: Vec<_> = chunk
                .iter()
                .map(|entry| models::IndexOperation::Add(entry.clone()))
                .collect();
            self.db.execute_batch(&operations)?;
        }
        println!("Initial indexing complete");

        // Start watching configured paths
        println!("Starting filesystem monitoring...");
        let paths = self.config.expand_paths();
        let mut watcher = self.watcher.lock().await;
        let errors = watcher.watch_paths(&paths);
        if !errors.is_empty() {
            eprintln!("Warning: Some paths could not be watched:");
            for error in errors {
                eprintln!("  {}", error);
            }
        }
        println!("Monitoring {} paths", watcher.watched_paths().len());

        Ok(())
    }

    /// Run the main event loop
    async fn run(&self) -> Result<(), Box<dyn std::error::Error>> {
        println!("NovaSearch daemon running");

        // Set up flush interval
        let flush_interval_duration = self.config.flush_interval();
        let mut flush_timer = interval(flush_interval_duration);

        // Clone Arc references for tasks
        let watcher = Arc::clone(&self.watcher);
        let event_processor = Arc::clone(&self.event_processor);
        let db = Arc::clone(&self.db);
        let running = Arc::clone(&self.running);
        let batch_size = self.config.performance.batch_size;

        // Main event loop
        while running.load(Ordering::Relaxed) {
            tokio::select! {
                // Process filesystem events
                _ = tokio::time::sleep(Duration::from_millis(50)) => {
                    // Receive filesystem events from watcher
                    let watcher = watcher.lock().await;
                    while let Some(event) = watcher.try_recv_event() {
                        let mut processor = event_processor.lock().await;
                        processor.add_event(event);
                    }
                    drop(watcher);

                    // Process pending events
                    let mut processor = event_processor.lock().await;
                    let operations = processor.process_pending();
                    
                    // Enqueue operations
                    for operation in operations {
                        if let Err(e) = processor.enqueue_operation(operation) {
                            eprintln!("Warning: Failed to enqueue operation: {}", e);
                        }
                    }
                }

                // Flush operations to database periodically
                _ = flush_timer.tick() => {
                    let mut processor = event_processor.lock().await;
                    let mut operations = Vec::new();
                    
                    // Dequeue up to batch_size operations
                    for _ in 0..batch_size {
                        if let Some(op) = processor.dequeue_operation() {
                            operations.push(op);
                        } else {
                            break;
                        }
                    }
                    
                    if !operations.is_empty() {
                        match db.execute_batch(&operations) {
                            Ok(()) => {
                                // Success
                            }
                            Err(e) => {
                                eprintln!("Error executing batch: {}", e);
                            }
                        }
                    }
                }
            }
        }

        println!("Daemon shutting down...");
        Ok(())
    }

    /// Gracefully shutdown the daemon
    async fn shutdown(&self) {
        println!("Shutting down gracefully...");
        self.running.store(false, Ordering::Relaxed);

        // Flush remaining operations
        let mut processor = self.event_processor.lock().await;
        let mut operations = Vec::new();
        while let Some(op) = processor.dequeue_operation() {
            operations.push(op);
        }

        if !operations.is_empty() {
            println!("Flushing {} pending operations...", operations.len());
            if let Err(e) = self.db.execute_batch(&operations) {
                eprintln!("Error flushing operations: {}", e);
            }
        }

        println!("Shutdown complete");
    }
}

/// Query and display indexing status
async fn show_status() -> Result<(), Box<dyn std::error::Error>> {
    let db_path = paths::get_database_path();
    
    if !db_path.exists() {
        println!("Status: Not initialized (database does not exist)");
        return Ok(());
    }

    let db = Database::open(&db_path)?;
    let file_count = db.count_files()?;

    println!("NovaSearch Indexing Status");
    println!("===========================");
    println!("Database: {}", db_path.display());
    println!("Indexed files: {}", file_count);
    println!("Status: Running");

    Ok(())
}

/// Force a full re-index
async fn reindex(config: Config) -> Result<(), Box<dyn std::error::Error>> {
    println!("Starting full re-index...");

    let db_path = paths::get_database_path();
    let db = Database::open(&db_path)?;

    // Clear existing index
    println!("Clearing existing index...");
    db.connection().execute("DELETE FROM files", [])?;

    // Perform scan
    println!("Scanning filesystem...");
    let scanner = Scanner::new(config.clone());
    let entries = scanner.scan();
    println!("Found {} files/directories", entries.len());

    // Batch insert
    println!("Indexing files...");
    let batch_size = config.performance.batch_size;
    for chunk in entries.chunks(batch_size) {
        let operations: Vec<_> = chunk
            .iter()
            .map(|entry| models::IndexOperation::Add(entry.clone()))
            .collect();
        db.execute_batch(&operations)?;
    }

    println!("Re-index complete");
    Ok(())
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let cli = Cli::parse();

    // Load configuration
    let config_path = cli.config.unwrap_or_else(|| paths::get_config_path());
    let config = Config::load_from_file(&config_path)?;

    match cli.command {
        Commands::Start => {
            // Set up signal handlers for graceful shutdown
            let running = Arc::new(AtomicBool::new(true));
            let r = running.clone();

            ctrlc::set_handler(move || {
                println!("\nReceived shutdown signal");
                r.store(false, Ordering::Relaxed);
            })?;

            // Create and initialize daemon
            let mut daemon = IndexingDaemon::new(config.clone()).await?;
            daemon.initialize().await?;

            // Set the daemon's running flag to match our signal handler
            daemon.running = running;

            // Run the daemon
            daemon.run().await?;

            // Shutdown
            daemon.shutdown().await;
        }
        Commands::Status => {
            show_status().await?;
        }
        Commands::Reindex => {
            reindex(config).await?;
        }
    }

    Ok(())
}
