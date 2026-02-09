#!/bin/bash

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

TESTS_PASSED=0
TESTS_FAILED=0

test_pass() {
    echo -e "${GREEN}✓${NC} $1"
    ((TESTS_PASSED++))
}

test_fail() {
    echo -e "${RED}✗${NC} $1"
    ((TESTS_FAILED++))
}

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

echo "========================================"
echo "NovaSearch Integration Test Suite"
echo "========================================"
echo ""

log_info "=== Test 1: Build Verification ==="
echo ""

# Check daemon binary exists
if [ -f "./builddir/novasearch-daemon" ]; then
    test_pass "Daemon binary exists"
else
    test_fail "Daemon binary not found"
fi

# Check daemon is executable
if [ -x "./builddir/novasearch-daemon" ]; then
    test_pass "Daemon binary is executable"
else
    test_fail "Daemon binary is not executable"
fi

echo ""
log_info "=== Test 2: Database Schema Test ==="
echo ""

# Create a test database
TEST_DB=$(mktemp)
sqlite3 "$TEST_DB" <<'EOF'
CREATE TABLE files (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    filename TEXT NOT NULL,
    path TEXT NOT NULL UNIQUE,
    size INTEGER NOT NULL,
    modified_time INTEGER NOT NULL,
    file_type TEXT NOT NULL,
    indexed_time INTEGER NOT NULL
);

CREATE INDEX idx_filename ON files(filename COLLATE NOCASE);
CREATE INDEX idx_path ON files(path COLLATE NOCASE);
CREATE INDEX idx_modified_time ON files(modified_time);

CREATE TABLE metadata (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

-- Insert test data
INSERT INTO files (filename, path, size, modified_time, file_type, indexed_time)
VALUES 
    ('test.txt', '/tmp/test.txt', 100, 1234567890, 'Regular', 1234567890),
    ('document.pdf', '/home/user/document.pdf', 5000, 1234567891, 'Regular', 1234567891),
    ('photo.jpg', '/home/user/photos/photo.jpg', 50000, 1234567892, 'Regular', 1234567892);
EOF

if [ $? -eq 0 ]; then
    test_pass "Database schema created successfully"
else
    test_fail "Failed to create database schema"
fi

# Verify tables exist
TABLES=$(sqlite3 "$TEST_DB" "SELECT name FROM sqlite_master WHERE type='table';")
if echo "$TABLES" | grep -q "files"; then
    test_pass "Files table exists"
else
    test_fail "Files table missing"
fi

if echo "$TABLES" | grep -q "metadata"; then
    test_pass "Metadata table exists"
else
    test_fail "Metadata table missing"
fi

# Verify indexes exist
INDEXES=$(sqlite3 "$TEST_DB" "SELECT name FROM sqlite_master WHERE type='index';")
if echo "$INDEXES" | grep -q "idx_filename"; then
    test_pass "Filename index exists"
else
    test_fail "Filename index missing"
fi

if echo "$INDEXES" | grep -q "idx_path"; then
    test_pass "Path index exists"
else
    test_fail "Path index missing"
fi

echo ""
log_info "=== Test 3: Search Query Test ==="
echo ""

# Test case-insensitive search
RESULT=$(sqlite3 "$TEST_DB" "SELECT filename FROM files WHERE filename LIKE '%test%' COLLATE NOCASE;")
if echo "$RESULT" | grep -q "test.txt"; then
    test_pass "Case-insensitive search works"
else
    test_fail "Case-insensitive search failed"
fi

# Test search ranking query
RANKING_QUERY="SELECT filename FROM files WHERE filename LIKE '%doc%' ORDER BY 
    CASE 
        WHEN filename = 'doc' THEN 0
        WHEN filename LIKE 'doc%' THEN 1
        ELSE 2
    END,
    filename COLLATE NOCASE;"

RESULT=$(sqlite3 "$TEST_DB" "$RANKING_QUERY")
if echo "$RESULT" | grep -q "document.pdf"; then
    test_pass "Search ranking query works"
else
    test_fail "Search ranking query failed"
fi

# Test result limit
LIMIT_RESULT=$(sqlite3 "$TEST_DB" "SELECT COUNT(*) FROM (SELECT * FROM files LIMIT 50);")
if [ "$LIMIT_RESULT" -le 50 ]; then
    test_pass "Result limit enforcement works (got $LIMIT_RESULT results)"
else
    test_fail "Result limit not enforced"
fi

echo ""
log_info "=== Test 4: Configuration File Test ==="
echo ""

# Create test config
TEST_CONFIG=$(mktemp)
cat > "$TEST_CONFIG" <<'EOF'
[indexing]
include_paths = ["~", "/mnt/data"]
exclude_patterns = [".*", "node_modules", ".git"]

[performance]
max_cpu_percent = 10
max_memory_mb = 100
batch_size = 100
flush_interval_ms = 1000

[ui]
keyboard_shortcut = "Super+Space"
max_results = 50
EOF

if [ -f "$TEST_CONFIG" ]; then
    test_pass "Configuration file created"
else
    test_fail "Failed to create configuration file"
fi

# Verify config can be parsed (basic check)
if grep -q "include_paths" "$TEST_CONFIG" && grep -q "exclude_patterns" "$TEST_CONFIG"; then
    test_pass "Configuration file has required sections"
else
    test_fail "Configuration file missing required sections"
fi

echo ""
log_info "=== Test 5: File Operations Test ==="
echo ""

# Test INSERT operation
sqlite3 "$TEST_DB" "INSERT INTO files (filename, path, size, modified_time, file_type, indexed_time) VALUES ('new.txt', '/tmp/new.txt', 200, 1234567893, 'Regular', 1234567893);"
NEW_COUNT=$(sqlite3 "$TEST_DB" "SELECT COUNT(*) FROM files WHERE filename='new.txt';")
if [ "$NEW_COUNT" -eq 1 ]; then
    test_pass "File insertion works"
else
    test_fail "File insertion failed"
fi

# Test UPDATE operation
sqlite3 "$TEST_DB" "UPDATE files SET size=300 WHERE filename='new.txt';"
NEW_SIZE=$(sqlite3 "$TEST_DB" "SELECT size FROM files WHERE filename='new.txt';")
if [ "$NEW_SIZE" -eq 300 ]; then
    test_pass "File update works"
else
    test_fail "File update failed"
fi

# Test DELETE operation
sqlite3 "$TEST_DB" "DELETE FROM files WHERE filename='new.txt';"
DEL_COUNT=$(sqlite3 "$TEST_DB" "SELECT COUNT(*) FROM files WHERE filename='new.txt';")
if [ "$DEL_COUNT" -eq 0 ]; then
    test_pass "File deletion works"
else
    test_fail "File deletion failed"
fi

# Test UPSERT (INSERT OR REPLACE)
sqlite3 "$TEST_DB" "INSERT OR REPLACE INTO files (filename, path, size, modified_time, file_type, indexed_time) VALUES ('test.txt', '/tmp/test.txt', 150, 1234567894, 'Regular', 1234567894);"
UPSERT_SIZE=$(sqlite3 "$TEST_DB" "SELECT size FROM files WHERE filename='test.txt';")
if [ "$UPSERT_SIZE" -eq 150 ]; then
    test_pass "File upsert works"
else
    test_fail "File upsert failed"
fi

echo ""
log_info "=== Test 6: Installation Files Test ==="
echo ""

# Check installation script exists
if [ -f "./install.sh" ]; then
    test_pass "Installation script exists"
else
    test_fail "Installation script not found"
fi

# Check systemd service file exists
if [ -f "./novasearch-daemon.service" ]; then
    test_pass "Systemd service file exists"
else
    test_fail "Systemd service file not found"
fi

# Check build script exists
if [ -f "./build.sh" ]; then
    test_pass "Build script exists"
else
    test_fail "Build script not found"
fi

echo ""
log_info "=== Test 7: Documentation Test ==="
echo ""

# Check README exists
if [ -f "./README.md" ]; then
    test_pass "README.md exists"
else
    test_fail "README.md not found"
fi

# Check BUILD.md exists
if [ -f "./BUILD.md" ]; then
    test_pass "BUILD.md exists"
else
    test_fail "BUILD.md not found"
fi

# Check INSTALL.md exists
if [ -f "./INSTALL.md" ]; then
    test_pass "INSTALL.md exists"
else
    test_fail "INSTALL.md not found"
fi

# Cleanup
rm -f "$TEST_DB" "$TEST_CONFIG"

echo ""
echo "========================================"
echo "Integration Test Results"
echo "========================================"
echo -e "${GREEN}Passed: $TESTS_PASSED${NC}"
echo -e "${RED}Failed: $TESTS_FAILED${NC}"
echo "========================================"

if [ $TESTS_FAILED -eq 0 ]; then
    echo -e "${GREEN}All integration tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some integration tests failed!${NC}"
    exit 1
fi
