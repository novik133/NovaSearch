use std::path::PathBuf;
use std::time::SystemTime;

/// File type enumeration
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum FileType {
    Regular,
    Directory,
    Symlink,
    Other,
}

impl FileType {
    pub fn as_str(&self) -> &str {
        match self {
            FileType::Regular => "regular",
            FileType::Directory => "directory",
            FileType::Symlink => "symlink",
            FileType::Other => "other",
        }
    }

    pub fn from_str(s: &str) -> Self {
        match s {
            "regular" => FileType::Regular,
            "directory" => FileType::Directory,
            "symlink" => FileType::Symlink,
            _ => FileType::Other,
        }
    }
}

/// Represents a file entry in the index
#[derive(Debug, Clone)]
pub struct FileEntry {
    pub id: Option<i64>,
    pub filename: String,
    pub path: PathBuf,
    pub size: u64,
    pub modified_time: SystemTime,
    pub file_type: FileType,
    pub indexed_time: SystemTime,
}

impl FileEntry {
    /// Create a new file entry
    pub fn new(
        filename: String,
        path: PathBuf,
        size: u64,
        modified_time: SystemTime,
        file_type: FileType,
    ) -> Self {
        FileEntry {
            id: None,
            filename,
            path,
            size,
            modified_time,
            file_type,
            indexed_time: SystemTime::now(),
        }
    }
}

/// Indexing operation types
#[derive(Debug, Clone)]
pub enum IndexOperation {
    Add(FileEntry),
    Update(FileEntry),
    Delete(PathBuf),
    Move { from: PathBuf, to: PathBuf },
}
