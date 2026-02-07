use std::path::PathBuf;

/// Get the database directory path: ~/.local/share/novasearch/
pub fn get_database_dir() -> PathBuf {
    let home = std::env::var("HOME").expect("HOME environment variable not set");
    PathBuf::from(home)
        .join(".local")
        .join("share")
        .join("novasearch")
}

/// Get the database file path: ~/.local/share/novasearch/index.db
pub fn get_database_path() -> PathBuf {
    get_database_dir().join("index.db")
}

/// Get the config directory path: ~/.config/novasearch/
pub fn get_config_dir() -> PathBuf {
    let home = std::env::var("HOME").expect("HOME environment variable not set");
    PathBuf::from(home)
        .join(".config")
        .join("novasearch")
}

/// Get the config file path: ~/.config/novasearch/config.toml
pub fn get_config_path() -> PathBuf {
    get_config_dir().join("config.toml")
}

/// Ensure the database directory exists
pub fn ensure_database_dir() -> std::io::Result<()> {
    let dir = get_database_dir();
    if !dir.exists() {
        std::fs::create_dir_all(&dir)?;
    }
    Ok(())
}

/// Ensure the config directory exists
pub fn ensure_config_dir() -> std::io::Result<()> {
    let dir = get_config_dir();
    if !dir.exists() {
        std::fs::create_dir_all(&dir)?;
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_database_path() {
        let db_path = get_database_path();
        assert!(db_path.to_string_lossy().contains(".local/share/novasearch/index.db"));
    }

    #[test]
    fn test_config_path() {
        let config_path = get_config_path();
        assert!(config_path.to_string_lossy().contains(".config/novasearch/config.toml"));
    }
}
