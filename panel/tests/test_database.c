/* NovaSearch Panel - Database Interface Tests */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include "../src/database.h"

/* Test database creation and connection */
void test_db_new_and_free(void) {
    printf("Testing database creation and cleanup...\n");
    
    NovaSearchDB *db = nova_search_db_new("/tmp/test.db");
    assert(db != NULL);
    assert(db->db_path != NULL);
    assert(strcmp(db->db_path, "/tmp/test.db") == 0);
    assert(db->is_connected == false);
    
    nova_search_db_free(db);
    printf("  ✓ Database creation and cleanup works\n");
}

/* Test database connection with non-existent database */
void test_db_open_nonexistent(void) {
    printf("Testing connection to non-existent database...\n");
    
    NovaSearchDB *db = nova_search_db_new("/tmp/nonexistent_db_12345.db");
    assert(db != NULL);
    
    bool result = nova_search_db_open(db);
    /* Should fail gracefully for non-existent database */
    assert(result == false);
    
    nova_search_db_free(db);
    printf("  ✓ Non-existent database handled correctly\n");
}

/* Test empty query handling */
void test_empty_query(void) {
    printf("Testing empty query handling...\n");
    
    NovaSearchDB *db = nova_search_db_new("/tmp/test.db");
    assert(db != NULL);
    
    /* Query should return NULL for empty string */
    SearchResult *results = nova_search_db_query(db, "", 50);
    assert(results == NULL);
    
    /* Query should return NULL for NULL query */
    results = nova_search_db_query(db, NULL, 50);
    assert(results == NULL);
    
    nova_search_db_free(db);
    printf("  ✓ Empty query handling works\n");
}

/* Test search result creation and cleanup */
void test_result_new_and_free(void) {
    printf("Testing search result creation and cleanup...\n");
    
    SearchResult *result = nova_search_result_new();
    assert(result != NULL);
    assert(result->filename == NULL);
    assert(result->path == NULL);
    assert(result->file_type == NULL);
    assert(result->size == 0);
    assert(result->modified_time == 0);
    assert(result->next == NULL);
    
    nova_search_result_free(result);
    printf("  ✓ Search result creation and cleanup works\n");
}

/* Test result list management */
void test_result_list(void) {
    printf("Testing result list management...\n");
    
    /* Create a list of results */
    SearchResult *r1 = nova_search_result_new();
    SearchResult *r2 = nova_search_result_new();
    SearchResult *r3 = nova_search_result_new();
    
    assert(r1 != NULL && r2 != NULL && r3 != NULL);
    
    r1->filename = strdup("file1.txt");
    r2->filename = strdup("file2.txt");
    r3->filename = strdup("file3.txt");
    
    r1->next = r2;
    r2->next = r3;
    r3->next = NULL;
    
    /* Count results */
    int count = nova_search_result_count(r1);
    assert(count == 3);
    
    /* Free entire list */
    nova_search_result_list_free(r1);
    printf("  ✓ Result list management works\n");
}

/* Test result count with empty list */
void test_result_count_empty(void) {
    printf("Testing result count with empty list...\n");
    
    int count = nova_search_result_count(NULL);
    assert(count == 0);
    
    printf("  ✓ Empty list count works\n");
}

/* Test database close and reopen */
void test_db_close_and_reopen(void) {
    printf("Testing database close and reopen...\n");
    
    NovaSearchDB *db = nova_search_db_new("/tmp/test.db");
    assert(db != NULL);
    
    /* Close without opening should be safe */
    nova_search_db_close(db);
    assert(db->is_connected == false);
    
    /* Multiple closes should be safe */
    nova_search_db_close(db);
    assert(db->is_connected == false);
    
    nova_search_db_free(db);
    printf("  ✓ Database close and reopen works\n");
}

/* Test NULL safety */
void test_null_safety(void) {
    printf("Testing NULL safety...\n");
    
    /* All functions should handle NULL gracefully */
    nova_search_db_close(NULL);
    nova_search_db_free(NULL);
    nova_search_result_free(NULL);
    nova_search_result_list_free(NULL);
    
    bool result = nova_search_db_open(NULL);
    assert(result == false);
    
    SearchResult *query_result = nova_search_db_query(NULL, "test", 50);
    assert(query_result == NULL);
    
    NovaSearchDB *db = nova_search_db_new(NULL);
    assert(db == NULL);
    
    printf("  ✓ NULL safety works\n");
}

int main(void) {
    printf("\n=== NovaSearch Database Interface Tests ===\n\n");
    
    test_db_new_and_free();
    test_db_open_nonexistent();
    test_empty_query();
    test_result_new_and_free();
    test_result_list();
    test_result_count_empty();
    test_db_close_and_reopen();
    test_null_safety();
    
    printf("\n=== All tests passed! ===\n\n");
    return 0;
}
