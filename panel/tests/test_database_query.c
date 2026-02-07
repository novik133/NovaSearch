/* NovaSearch Panel - Database Query Integration Test */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sqlite3.h>
#include "../src/database.h"

#define TEST_DB_PATH "/tmp/novasearch_test.db"

/* Helper function to create a test database */
void create_test_database(void) {
    sqlite3 *db;
    int rc = sqlite3_open(TEST_DB_PATH, &db);
    assert(rc == SQLITE_OK);
    
    /* Create schema */
    const char *schema = 
        "CREATE TABLE IF NOT EXISTS files ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  filename TEXT NOT NULL,"
        "  path TEXT NOT NULL UNIQUE,"
        "  size INTEGER NOT NULL,"
        "  modified_time INTEGER NOT NULL,"
        "  file_type TEXT NOT NULL,"
        "  indexed_time INTEGER NOT NULL"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_filename ON files(filename COLLATE NOCASE);"
        "CREATE INDEX IF NOT EXISTS idx_path ON files(path COLLATE NOCASE);";
    
    char *err_msg = NULL;
    rc = sqlite3_exec(db, schema, NULL, NULL, &err_msg);
    assert(rc == SQLITE_OK);
    
    /* Insert test data */
    const char *insert = 
        "INSERT OR REPLACE INTO files (filename, path, size, modified_time, file_type, indexed_time) VALUES "
        "('document.txt', '/home/user/document.txt', 1024, 1234567890, 'Regular', 1234567890),"
        "('Document.pdf', '/home/user/Document.pdf', 2048, 1234567891, 'Regular', 1234567891),"
        "('my_document.doc', '/home/user/my_document.doc', 4096, 1234567892, 'Regular', 1234567892),"
        "('image.png', '/home/user/image.png', 8192, 1234567893, 'Regular', 1234567893),"
        "('test.txt', '/home/user/test.txt', 512, 1234567894, 'Regular', 1234567894);";
    
    rc = sqlite3_exec(db, insert, NULL, NULL, &err_msg);
    assert(rc == SQLITE_OK);
    
    sqlite3_close(db);
}

/* Test basic query functionality */
void test_basic_query(void) {
    printf("Testing basic query functionality...\n");
    
    NovaSearchDB *db = nova_search_db_new(TEST_DB_PATH);
    assert(db != NULL);
    
    bool opened = nova_search_db_open(db);
    assert(opened == true);
    assert(db->is_connected == true);
    
    /* Query for "document" */
    SearchResult *results = nova_search_db_query(db, "document", 50);
    assert(results != NULL);
    
    int count = nova_search_result_count(results);
    assert(count == 3); /* document.txt, Document.pdf, my_document.doc */
    
    nova_search_result_list_free(results);
    nova_search_db_free(db);
    
    printf("  ✓ Basic query works\n");
}

/* Test case-insensitive matching */
void test_case_insensitive(void) {
    printf("Testing case-insensitive matching...\n");
    
    NovaSearchDB *db = nova_search_db_new(TEST_DB_PATH);
    assert(db != NULL);
    assert(nova_search_db_open(db) == true);
    
    /* Query with different cases */
    SearchResult *results1 = nova_search_db_query(db, "DOCUMENT", 50);
    SearchResult *results2 = nova_search_db_query(db, "document", 50);
    SearchResult *results3 = nova_search_db_query(db, "DoC", 50);
    
    int count1 = nova_search_result_count(results1);
    int count2 = nova_search_result_count(results2);
    int count3 = nova_search_result_count(results3);
    
    assert(count1 == 3);
    assert(count2 == 3);
    assert(count3 == 3);
    
    nova_search_result_list_free(results1);
    nova_search_result_list_free(results2);
    nova_search_result_list_free(results3);
    nova_search_db_free(db);
    
    printf("  ✓ Case-insensitive matching works\n");
}

/* Test result ranking (exact, prefix, substring) */
void test_result_ranking(void) {
    printf("Testing result ranking...\n");
    
    NovaSearchDB *db = nova_search_db_new(TEST_DB_PATH);
    assert(db != NULL);
    assert(nova_search_db_open(db) == true);
    
    /* Query for "document" - should rank exact match first */
    SearchResult *results = nova_search_db_query(db, "document", 50);
    assert(results != NULL);
    
    /* First result should be exact match (case-insensitive) */
    assert(results->filename != NULL);
    assert(strcasecmp(results->filename, "document.txt") == 0 || 
           strcasecmp(results->filename, "document.pdf") == 0);
    
    nova_search_result_list_free(results);
    nova_search_db_free(db);
    
    printf("  ✓ Result ranking works\n");
}

/* Test result limit enforcement */
void test_result_limit(void) {
    printf("Testing result limit enforcement...\n");
    
    NovaSearchDB *db = nova_search_db_new(TEST_DB_PATH);
    assert(db != NULL);
    assert(nova_search_db_open(db) == true);
    
    /* Query with limit of 2 */
    SearchResult *results = nova_search_db_query(db, "document", 2);
    assert(results != NULL);
    
    int count = nova_search_result_count(results);
    assert(count == 2); /* Should only return 2 results */
    
    nova_search_result_list_free(results);
    nova_search_db_free(db);
    
    printf("  ✓ Result limit enforcement works\n");
}

/* Test query with no matches */
void test_no_matches(void) {
    printf("Testing query with no matches...\n");
    
    NovaSearchDB *db = nova_search_db_new(TEST_DB_PATH);
    assert(db != NULL);
    assert(nova_search_db_open(db) == true);
    
    /* Query for something that doesn't exist */
    SearchResult *results = nova_search_db_query(db, "nonexistent_file_xyz", 50);
    assert(results == NULL); /* No results */
    
    nova_search_db_free(db);
    
    printf("  ✓ No matches handled correctly\n");
}

/* Test result data completeness */
void test_result_data_completeness(void) {
    printf("Testing result data completeness...\n");
    
    NovaSearchDB *db = nova_search_db_new(TEST_DB_PATH);
    assert(db != NULL);
    assert(nova_search_db_open(db) == true);
    
    SearchResult *results = nova_search_db_query(db, "test", 50);
    assert(results != NULL);
    
    /* Verify all fields are populated */
    assert(results->filename != NULL);
    assert(results->path != NULL);
    assert(results->file_type != NULL);
    assert(results->size > 0);
    assert(results->modified_time > 0);
    
    printf("  Result: %s at %s (%ld bytes, type: %s)\n",
           results->filename, results->path, results->size, results->file_type);
    
    nova_search_result_list_free(results);
    nova_search_db_free(db);
    
    printf("  ✓ Result data completeness verified\n");
}

/* Cleanup test database */
void cleanup_test_database(void) {
    unlink(TEST_DB_PATH);
}

int main(void) {
    printf("\n=== NovaSearch Database Query Integration Tests ===\n\n");
    
    /* Create test database */
    create_test_database();
    
    /* Run tests */
    test_basic_query();
    test_case_insensitive();
    test_result_ranking();
    test_result_limit();
    test_no_matches();
    test_result_data_completeness();
    
    /* Cleanup */
    cleanup_test_database();
    
    printf("\n=== All integration tests passed! ===\n\n");
    return 0;
}
