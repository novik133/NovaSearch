/* NovaSearch Panel - Database Interface Header */

#ifndef NOVASEARCH_DATABASE_H
#define NOVASEARCH_DATABASE_H

#include <sqlite3.h>
#include <stdbool.h>
#include <stdint.h>

/* Database connection structure */
typedef struct {
    sqlite3 *db;
    char *db_path;
    bool is_connected;
} NovaSearchDB;

/* Search result structure */
typedef struct SearchResult {
    char *filename;
    char *path;
    char *file_type;
    int64_t size;
    int64_t modified_time;
    struct SearchResult *next;
} SearchResult;

/* Database connection functions */
NovaSearchDB* nova_search_db_new(const char *db_path);
bool nova_search_db_open(NovaSearchDB *db);
void nova_search_db_close(NovaSearchDB *db);
void nova_search_db_free(NovaSearchDB *db);

/* Query functions */
SearchResult* nova_search_db_query(NovaSearchDB *db, const char *query, int max_results);

/* Usage tracking functions */
bool nova_search_db_record_launch(NovaSearchDB *db, const char *file_path);

/* Result management functions */
SearchResult* nova_search_result_new(void);
void nova_search_result_free(SearchResult *result);
void nova_search_result_list_free(SearchResult *results);
int nova_search_result_count(SearchResult *results);

#endif /* NOVASEARCH_DATABASE_H */
