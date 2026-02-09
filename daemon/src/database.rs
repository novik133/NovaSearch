use rusqlite::{Connection, Result as SqliteResult, params, OptionalExtension};
use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH, Duration};
use crate::models::{FileEntry, FileType, IndexOperation};

/// Database schema version
const SCHEMA_VERSION: i32 = 2;

/// Database connection wrapper
pub struct Database {
    connection: Connection,
}

impl Database {
    /// Open or create the database at the specified path
    pub fn open<P: AsRef<Path>>(path: P) -> SqliteResult<Self> {
        let connection = Connection::open(path)?;
        let db = Database { connection };
        db.initialize()?;
        Ok(db)
    }

    /// Initialize the database schema
    fn initialize(&self) -> SqliteResult<()> {
        // Check current schema version
        let current_version = self.get_schema_version()?;
        
        if current_version == 0 {
            // Fresh database, create schema
            self.create_schema()?;
            self.set_schema_version(SCHEMA_VERSION)?;
        } else if current_version < SCHEMA_VERSION {
            // Migration needed
            self.migrate_schema(current_version, SCHEMA_VERSION)?;
        }
        
        Ok(())
    }

    /// Create the database schema from scratch
    fn create_schema(&self) -> SqliteResult<()> {
        // Create files table
        self.connection.execute(
            "CREATE TABLE IF NOT EXISTS files (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                filename TEXT NOT NULL,
                path TEXT NOT NULL UNIQUE,
                size INTEGER NOT NULL,
                modified_time INTEGER NOT NULL,
                file_type TEXT NOT NULL,
                indexed_time INTEGER NOT NULL
            )",
            [],
        )?;

        // Create usage statistics table
        self.connection.execute(
            "CREATE TABLE IF NOT EXISTS usage_stats (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                file_id INTEGER NOT NULL,
                launch_count INTEGER NOT NULL DEFAULT 0,
                last_launched INTEGER,
                FOREIGN KEY (file_id) REFERENCES files (id) ON DELETE CASCADE
            )",
            [],
        )?;

        // Create indexes for efficient searching
        self.connection.execute(
            "CREATE INDEX IF NOT EXISTS idx_filename ON files(filename COLLATE NOCASE)",
            [],
        )?;

        self.connection.execute(
            "CREATE INDEX IF NOT EXISTS idx_path ON files(path COLLATE NOCASE)",
            [],
        )?;

        self.connection.execute(
            "CREATE INDEX IF NOT EXISTS idx_modified_time ON files(modified_time)",
            [],
        )?;

        self.connection.execute(
            "CREATE INDEX IF NOT EXISTS idx_usage_file_id ON usage_stats(file_id)",
            [],
        )?;

        self.connection.execute(
            "CREATE INDEX IF NOT EXISTS idx_usage_launch_count ON usage_stats(launch_count DESC)",
            [],
        )?;

        // Create metadata table
        self.connection.execute(
            "CREATE TABLE IF NOT EXISTS metadata (
                key TEXT PRIMARY KEY,
                value TEXT NOT NULL
            )",
            [],
        )?;

        Ok(())
    }

    /// Get the current schema version
    fn get_schema_version(&self) -> SqliteResult<i32> {
        // Check if metadata table exists
        let table_exists: bool = self.connection.query_row(
            "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='metadata'",
            [],
            |row| row.get::<_, i32>(0),
        )? > 0;

        if !table_exists {
            return Ok(0);
        }

        // Try to get schema version
        match self.connection.query_row(
            "SELECT value FROM metadata WHERE key = 'schema_version'",
            [],
            |row| row.get::<_, String>(0),
        ) {
            Ok(version_str) => Ok(version_str.parse().unwrap_or(0)),
            Err(_) => Ok(0),
        }
    }

    /// Set the schema version
    fn set_schema_version(&self, version: i32) -> SqliteResult<()> {
        self.connection.execute(
            "INSERT OR REPLACE INTO metadata (key, value) VALUES ('schema_version', ?)",
            [version.to_string()],
        )?;
        Ok(())
    }

    /// Migrate schema from one version to another
    fn migrate_schema(&self, from_version: i32, to_version: i32) -> SqliteResult<()> {
        for version in from_version..to_version {
            match version {
                1 => self.migrate_v1_to_v2()?,
                _ => {
                    // Unknown migration path
                    return Err(rusqlite::Error::InvalidQuery);
                }
            }
        }
        self.set_schema_version(to_version)?;
        Ok(())
    }

    /// Migrate from version 1 to version 2 (add usage tracking)
    fn migrate_v1_to_v2(&self) -> SqliteResult<()> {
        // Create usage statistics table
        self.connection.execute(
            "CREATE TABLE IF NOT EXISTS usage_stats (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                file_id INTEGER NOT NULL,
                launch_count INTEGER NOT NULL DEFAULT 0,
                last_launched INTEGER,
                FOREIGN KEY (file_id) REFERENCES files (id) ON DELETE CASCADE
            )",
            [],
        )?;

        // Create indexes for usage stats
        self.connection.execute(
            "CREATE INDEX IF NOT EXISTS idx_usage_file_id ON usage_stats(file_id)",
            [],
        )?;

        self.connection.execute(
            "CREATE INDEX IF NOT EXISTS idx_usage_launch_count ON usage_stats(launch_count DESC)",
            [],
        )?;

        Ok(())
    }

    /// Get the underlying connection (for testing and operations)
    pub fn connection(&self) -> &Connection {
        &self.connection
    }

    /// Insert a new file entry into the database
    pub fn insert_file(&self, entry: &FileEntry) -> SqliteResult<i64> {
        let modified_time = system_time_to_timestamp(entry.modified_time);
        let indexed_time = system_time_to_timestamp(entry.indexed_time);
        
        self.connection.execute(
            "INSERT INTO files (filename, path, size, modified_time, file_type, indexed_time)
             VALUES (?, ?, ?, ?, ?, ?)",
            params![
                entry.filename,
                entry.path.to_string_lossy().to_string(),
                entry.size as i64,
                modified_time,
                entry.file_type.as_str(),
                indexed_time,
            ],
        )?;
        
        Ok(self.connection.last_insert_rowid())
    }

    /// Update an existing file entry
    pub fn update_file(&self, entry: &FileEntry) -> SqliteResult<()> {
        let modified_time = system_time_to_timestamp(entry.modified_time);
        let indexed_time = system_time_to_timestamp(entry.indexed_time);
        
        self.connection.execute(
            "INSERT INTO files (filename, path, size, modified_time, file_type, indexed_time)
             VALUES (?, ?, ?, ?, ?, ?)
             ON CONFLICT(path) DO UPDATE SET
                filename = excluded.filename,
                size = excluded.size,
                modified_time = excluded.modified_time,
                file_type = excluded.file_type,
                indexed_time = excluded.indexed_time",
            params![
                entry.filename,
                entry.path.to_string_lossy().to_string(),
                entry.size as i64,
                modified_time,
                entry.file_type.as_str(),
                indexed_time,
            ],
        )?;
        
        Ok(())
    }

    /// Delete a file entry by path
    pub fn delete_file<P: AsRef<Path>>(&self, path: P) -> SqliteResult<()> {
        self.connection.execute(
            "DELETE FROM files WHERE path = ?",
            params![path.as_ref().to_string_lossy().to_string()],
        )?;
        Ok(())
    }

    /// Move a file entry (update its path)
    pub fn move_file<P: AsRef<Path>>(&self, from: P, to: P) -> SqliteResult<()> {
        let to_path = to.as_ref();
        let filename = to_path
            .file_name()
            .and_then(|n| n.to_str())
            .unwrap_or("")
            .to_string();
        
        self.connection.execute(
            "UPDATE files SET path = ?, filename = ? WHERE path = ?",
            params![
                to_path.to_string_lossy().to_string(),
                filename,
                from.as_ref().to_string_lossy().to_string(),
            ],
        )?;
        Ok(())
    }

    /// Query files by filename pattern with usage-based ranking
    pub fn query_files(&self, query: &str, limit: usize) -> SqliteResult<Vec<FileEntry>> {
        let mut stmt = self.connection.prepare(
            "SELECT f.id, f.filename, f.path, f.size, f.modified_time, f.file_type, f.indexed_time,
                    COALESCE(u.launch_count, 0) as launch_count,
                    COALESCE(u.last_launched, 0) as last_launched
             FROM files f
             LEFT JOIN usage_stats u ON f.id = u.file_id
             WHERE f.filename LIKE '%' || ? || '%'
             ORDER BY 
                CASE 
                    WHEN f.filename = ? THEN 0
                    WHEN f.filename LIKE ? || '%' THEN 1
                    ELSE 2
                END,
                COALESCE(u.launch_count, 0) DESC,
                f.filename COLLATE NOCASE
             LIMIT ?"
        )?;

        let entries = stmt.query_map(
            params![query, query, query, limit as i64],
            |row| {
                Ok(FileEntry {
                    id: Some(row.get(0)?),
                    filename: row.get(1)?,
                    path: PathBuf::from(row.get::<_, String>(2)?),
                    size: row.get::<_, i64>(3)? as u64,
                    modified_time: timestamp_to_system_time(row.get(4)?),
                    file_type: FileType::from_str(&row.get::<_, String>(5)?),
                    indexed_time: timestamp_to_system_time(row.get(6)?),
                })
            },
        )?;

        entries.collect()
    }

    /// Execute a batch of operations with retry logic
    pub fn execute_batch(&self, operations: &[IndexOperation]) -> SqliteResult<()> {
        let max_retries = 5;
        let mut delay_ms = 100;
        
        for attempt in 0..max_retries {
            match self.try_execute_batch(operations) {
                Ok(()) => return Ok(()),
                Err(rusqlite::Error::SqliteFailure(err, _)) 
                    if err.code == rusqlite::ErrorCode::DatabaseBusy 
                    || err.code == rusqlite::ErrorCode::DatabaseLocked => 
                {
                    if attempt < max_retries - 1 {
                        std::thread::sleep(Duration::from_millis(delay_ms));
                        delay_ms = (delay_ms * 2).min(1600); // Cap at 1600ms
                    } else {
                        return Err(rusqlite::Error::SqliteFailure(err, None));
                    }
                }
                Err(e) => return Err(e),
            }
        }
        
        unreachable!()
    }

    /// Try to execute a batch of operations (helper for retry logic)
    fn try_execute_batch(&self, operations: &[IndexOperation]) -> SqliteResult<()> {
        // Use unchecked_transaction to work with immutable self
        let tx = self.connection.unchecked_transaction()?;
            
            for operation in operations {
                match operation {
                    IndexOperation::Add(entry) | IndexOperation::Update(entry) => {
                        let modified_time = system_time_to_timestamp(entry.modified_time);
                        let indexed_time = system_time_to_timestamp(entry.indexed_time);
                        
                        tx.execute(
                            "INSERT INTO files (filename, path, size, modified_time, file_type, indexed_time)
                             VALUES (?, ?, ?, ?, ?, ?)
                             ON CONFLICT(path) DO UPDATE SET
                                filename = excluded.filename,
                                size = excluded.size,
                                modified_time = excluded.modified_time,
                                file_type = excluded.file_type,
                                indexed_time = excluded.indexed_time",
                            params![
                                entry.filename,
                                entry.path.to_string_lossy().to_string(),
                                entry.size as i64,
                                modified_time,
                                entry.file_type.as_str(),
                                indexed_time,
                            ],
                        )?;
                    }
                    IndexOperation::Delete(path) => {
                        tx.execute(
                            "DELETE FROM files WHERE path = ?",
                            params![path.to_string_lossy().to_string()],
                        )?;
                    }
                    IndexOperation::Move { from, to } => {
                        let filename = to
                            .file_name()
                            .and_then(|n| n.to_str())
                            .unwrap_or("")
                            .to_string();
                        
                        tx.execute(
                            "UPDATE files SET path = ?, filename = ? WHERE path = ?",
                            params![
                                to.to_string_lossy().to_string(),
                                filename,
                                from.to_string_lossy().to_string(),
                            ],
                        )?;
                    }
                }
            }
            
            tx.commit()?;
            Ok(())
    }

    /// Execute an operation with exponential backoff retry logic
    fn execute_with_retry<F, T>(&self, mut operation: F) -> SqliteResult<T>
    where
        F: FnMut() -> SqliteResult<T>,
    {
        let max_retries = 5;
        let mut delay_ms = 100;
        
        for attempt in 0..max_retries {
            match operation() {
                Ok(result) => return Ok(result),
                Err(rusqlite::Error::SqliteFailure(err, _)) 
                    if err.code == rusqlite::ErrorCode::DatabaseBusy 
                    || err.code == rusqlite::ErrorCode::DatabaseLocked => 
                {
                    if attempt < max_retries - 1 {
                        std::thread::sleep(Duration::from_millis(delay_ms));
                        delay_ms = (delay_ms * 2).min(1600); // Cap at 1600ms
                    } else {
                        return Err(rusqlite::Error::SqliteFailure(err, None));
                    }
                }
                Err(e) => return Err(e),
            }
        }
        
        unreachable!()
    }

    /// Get the count of indexed files
    pub fn count_files(&self) -> SqliteResult<i64> {
        self.connection.query_row(
            "SELECT COUNT(*) FROM files",
            [],
            |row| row.get(0),
        )
    }

    /// Record that a file was launched/opened
    pub fn record_file_launch<P: AsRef<Path>>(&self, path: P) -> SqliteResult<()> {
        let path_str = path.as_ref().to_string_lossy().to_string();
        let current_time = current_timestamp();
        
        // First, get the file ID
        let file_id: Option<i64> = self.connection.query_row(
            "SELECT id FROM files WHERE path = ?",
            params![path_str],
            |row| row.get(0),
        ).optional()?;
        
        if let Some(file_id) = file_id {
            // Insert or update usage stats
            self.connection.execute(
                "INSERT INTO usage_stats (file_id, launch_count, last_launched)
                 VALUES (?, 1, ?)
                 ON CONFLICT(file_id) DO UPDATE SET
                    launch_count = launch_count + 1,
                    last_launched = ?",
                params![file_id, current_time, current_time],
            )?;
        }
        
        Ok(())
    }

    /// Get usage statistics for a file
    pub fn get_file_usage<P: AsRef<Path>>(&self, path: P) -> SqliteResult<Option<(i32, i64)>> {
        let path_str = path.as_ref().to_string_lossy().to_string();
        
        let result = self.connection.query_row(
            "SELECT u.launch_count, u.last_launched
             FROM files f
             JOIN usage_stats u ON f.id = u.file_id
             WHERE f.path = ?",
            params![path_str],
            |row| Ok((row.get::<_, i32>(0)?, row.get::<_, i64>(1)?)),
        ).optional()?;
        
        Ok(result)
    }

    /// Get most frequently used files
    pub fn get_most_used_files(&self, limit: usize) -> SqliteResult<Vec<FileEntry>> {
        let mut stmt = self.connection.prepare(
            "SELECT f.id, f.filename, f.path, f.size, f.modified_time, f.file_type, f.indexed_time
             FROM files f
             JOIN usage_stats u ON f.id = u.file_id
             ORDER BY u.launch_count DESC, u.last_launched DESC
             LIMIT ?"
        )?;

        let entries = stmt.query_map(
            params![limit as i64],
            |row| {
                Ok(FileEntry {
                    id: Some(row.get(0)?),
                    filename: row.get(1)?,
                    path: PathBuf::from(row.get::<_, String>(2)?),
                    size: row.get::<_, i64>(3)? as u64,
                    modified_time: timestamp_to_system_time(row.get(4)?),
                    file_type: FileType::from_str(&row.get::<_, String>(5)?),
                    indexed_time: timestamp_to_system_time(row.get(6)?),
                })
            },
        )?;

        entries.collect()
    }
}

/// Get current Unix timestamp
pub fn current_timestamp() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap()
        .as_secs() as i64
}

/// Convert SystemTime to Unix timestamp
fn system_time_to_timestamp(time: SystemTime) -> i64 {
    time.duration_since(UNIX_EPOCH)
        .unwrap_or(Duration::from_secs(0))
        .as_secs() as i64
}

/// Convert Unix timestamp to SystemTime
fn timestamp_to_system_time(timestamp: i64) -> SystemTime {
    UNIX_EPOCH + Duration::from_secs(timestamp as u64)
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::NamedTempFile;

    #[test]
    fn test_database_creation() {
        let temp_file = NamedTempFile::new().unwrap();
        let db = Database::open(temp_file.path()).unwrap();
        
        // Verify files table exists
        let table_exists: i32 = db.connection()
            .query_row(
                "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='files'",
                [],
                |row| row.get(0),
            )
            .unwrap();
        assert_eq!(table_exists, 1);

        // Verify metadata table exists
        let table_exists: i32 = db.connection()
            .query_row(
                "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='metadata'",
                [],
                |row| row.get(0),
            )
            .unwrap();
        assert_eq!(table_exists, 1);
    }

    #[test]
    fn test_schema_version() {
        let temp_file = NamedTempFile::new().unwrap();
        let db = Database::open(temp_file.path()).unwrap();
        
        let version = db.get_schema_version().unwrap();
        assert_eq!(version, SCHEMA_VERSION);
    }

    #[test]
    fn test_indexes_created() {
        let temp_file = NamedTempFile::new().unwrap();
        let db = Database::open(temp_file.path()).unwrap();
        
        // Check for filename index
        let index_exists: i32 = db.connection()
            .query_row(
                "SELECT COUNT(*) FROM sqlite_master WHERE type='index' AND name='idx_filename'",
                [],
                |row| row.get(0),
            )
            .unwrap();
        assert_eq!(index_exists, 1);

        // Check for path index
        let index_exists: i32 = db.connection()
            .query_row(
                "SELECT COUNT(*) FROM sqlite_master WHERE type='index' AND name='idx_path'",
                [],
                |row| row.get(0),
            )
            .unwrap();
        assert_eq!(index_exists, 1);

        // Check for modified_time index
        let index_exists: i32 = db.connection()
            .query_row(
                "SELECT COUNT(*) FROM sqlite_master WHERE type='index' AND name='idx_modified_time'",
                [],
                |row| row.get(0),
            )
            .unwrap();
        assert_eq!(index_exists, 1);
    }

    #[test]
    fn test_reopen_existing_database() {
        let temp_file = NamedTempFile::new().unwrap();
        let path = temp_file.path().to_path_buf();
        
        // Create database
        {
            let _db = Database::open(&path).unwrap();
        }
        
        // Reopen database
        let db = Database::open(&path).unwrap();
        let version = db.get_schema_version().unwrap();
        assert_eq!(version, SCHEMA_VERSION);
    }

    #[test]
    fn test_insert_file() {
        let temp_file = NamedTempFile::new().unwrap();
        let db = Database::open(temp_file.path()).unwrap();
        
        let entry = FileEntry::new(
            "test.txt".to_string(),
            PathBuf::from("/home/user/test.txt"),
            1024,
            SystemTime::now(),
            FileType::Regular,
        );
        
        let id = db.insert_file(&entry).unwrap();
        assert!(id > 0);
        
        let count = db.count_files().unwrap();
        assert_eq!(count, 1);
    }

    #[test]
    fn test_update_file() {
        let temp_file = NamedTempFile::new().unwrap();
        let db = Database::open(temp_file.path()).unwrap();
        
        let entry = FileEntry::new(
            "test.txt".to_string(),
            PathBuf::from("/home/user/test.txt"),
            1024,
            SystemTime::now(),
            FileType::Regular,
        );
        
        db.insert_file(&entry).unwrap();
        
        let updated_entry = FileEntry::new(
            "test.txt".to_string(),
            PathBuf::from("/home/user/test.txt"),
            2048,
            SystemTime::now(),
            FileType::Regular,
        );
        
        db.update_file(&updated_entry).unwrap();
        
        let count = db.count_files().unwrap();
        assert_eq!(count, 1);
        
        let results = db.query_files("test", 10).unwrap();
        assert_eq!(results.len(), 1);
        assert_eq!(results[0].size, 2048);
    }

    #[test]
    fn test_delete_file() {
        let temp_file = NamedTempFile::new().unwrap();
        let db = Database::open(temp_file.path()).unwrap();
        
        let entry = FileEntry::new(
            "test.txt".to_string(),
            PathBuf::from("/home/user/test.txt"),
            1024,
            SystemTime::now(),
            FileType::Regular,
        );
        
        db.insert_file(&entry).unwrap();
        assert_eq!(db.count_files().unwrap(), 1);
        
        db.delete_file(&entry.path).unwrap();
        assert_eq!(db.count_files().unwrap(), 0);
    }

    #[test]
    fn test_move_file() {
        let temp_file = NamedTempFile::new().unwrap();
        let db = Database::open(temp_file.path()).unwrap();
        
        let entry = FileEntry::new(
            "test.txt".to_string(),
            PathBuf::from("/home/user/test.txt"),
            1024,
            SystemTime::now(),
            FileType::Regular,
        );
        
        db.insert_file(&entry).unwrap();
        
        let new_path = PathBuf::from("/home/user/documents/test.txt");
        db.move_file(&entry.path, &new_path).unwrap();
        
        let results = db.query_files("test", 10).unwrap();
        assert_eq!(results.len(), 1);
        assert_eq!(results[0].path, new_path);
        assert_eq!(results[0].filename, "test.txt");
    }

    #[test]
    fn test_query_files() {
        let temp_file = NamedTempFile::new().unwrap();
        let db = Database::open(temp_file.path()).unwrap();
        
        // Insert multiple files
        let files = vec![
            ("test.txt", "/home/user/test.txt"),
            ("testing.txt", "/home/user/testing.txt"),
            ("document.txt", "/home/user/document.txt"),
            ("test_file.txt", "/home/user/test_file.txt"),
        ];
        
        for (filename, path) in files {
            let entry = FileEntry::new(
                filename.to_string(),
                PathBuf::from(path),
                1024,
                SystemTime::now(),
                FileType::Regular,
            );
            db.insert_file(&entry).unwrap();
        }
        
        // Query for "test"
        let results = db.query_files("test", 10).unwrap();
        assert_eq!(results.len(), 3);
        
        // Verify ranking: exact match first
        assert_eq!(results[0].filename, "test.txt");
        
        // Query with limit
        let results = db.query_files("test", 2).unwrap();
        assert_eq!(results.len(), 2);
    }

    #[test]
    fn test_execute_batch() {
        let temp_file = NamedTempFile::new().unwrap();
        let db = Database::open(temp_file.path()).unwrap();
        
        let operations = vec![
            IndexOperation::Add(FileEntry::new(
                "file1.txt".to_string(),
                PathBuf::from("/home/user/file1.txt"),
                1024,
                SystemTime::now(),
                FileType::Regular,
            )),
            IndexOperation::Add(FileEntry::new(
                "file2.txt".to_string(),
                PathBuf::from("/home/user/file2.txt"),
                2048,
                SystemTime::now(),
                FileType::Regular,
            )),
            IndexOperation::Add(FileEntry::new(
                "file3.txt".to_string(),
                PathBuf::from("/home/user/file3.txt"),
                3072,
                SystemTime::now(),
                FileType::Regular,
            )),
        ];
        
        db.execute_batch(&operations).unwrap();
        
        let count = db.count_files().unwrap();
        assert_eq!(count, 3);
    }

    #[test]
    fn test_batch_with_mixed_operations() {
        let temp_file = NamedTempFile::new().unwrap();
        let db = Database::open(temp_file.path()).unwrap();
        
        // First, add some files
        let entry1 = FileEntry::new(
            "file1.txt".to_string(),
            PathBuf::from("/home/user/file1.txt"),
            1024,
            SystemTime::now(),
            FileType::Regular,
        );
        db.insert_file(&entry1).unwrap();
        
        // Now execute a batch with mixed operations
        let operations = vec![
            IndexOperation::Update(FileEntry::new(
                "file1.txt".to_string(),
                PathBuf::from("/home/user/file1.txt"),
                2048,
                SystemTime::now(),
                FileType::Regular,
            )),
            IndexOperation::Add(FileEntry::new(
                "file2.txt".to_string(),
                PathBuf::from("/home/user/file2.txt"),
                1024,
                SystemTime::now(),
                FileType::Regular,
            )),
            IndexOperation::Delete(PathBuf::from("/home/user/file1.txt")),
        ];
        
        db.execute_batch(&operations).unwrap();
        
        let count = db.count_files().unwrap();
        assert_eq!(count, 1);
        
        let results = db.query_files("file2", 10).unwrap();
        assert_eq!(results.len(), 1);
    }
}
