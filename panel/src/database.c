/* NovaSearch Panel - Database Interface Implementation */

#include "database.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

/* Retry configuration */
#define MAX_RETRY_ATTEMPTS 5
#define INITIAL_RETRY_DELAY_MS 100
#define MAX_RETRY_DELAY_MS 1600

/* Helper function to sleep for milliseconds */
static void sleep_ms(int milliseconds) {
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

/* Create a new database connection object */
NovaSearchDB* nova_search_db_new(const char *db_path) {
    if (!db_path) {
        fprintf(stderr, "Database path cannot be NULL\n");
        return NULL;
    }

    NovaSearchDB *db = malloc(sizeof(NovaSearchDB));
    if (!db) {
        fprintf(stderr, "Failed to allocate memory for database connection\n");
        return NULL;
    }

    db->db = NULL;
    db->db_path = strdup(db_path);
    db->is_connected = false;

    if (!db->db_path) {
        fprintf(stderr, "Failed to duplicate database path\n");
        free(db);
        return NULL;
    }

    return db;
}

/* Open database connection with retry logic */
bool nova_search_db_open(NovaSearchDB *db) {
    if (!db) {
        fprintf(stderr, "Database object is NULL\n");
        return false;
    }

    if (db->is_connected) {
        return true;
    }

    int retry_delay = INITIAL_RETRY_DELAY_MS;
    
    for (int attempt = 0; attempt < MAX_RETRY_ATTEMPTS; attempt++) {
        int rc = sqlite3_open_v2(
            db->db_path,
            &db->db,
            SQLITE_OPEN_READONLY,
            NULL
        );

        if (rc == SQLITE_OK) {
            db->is_connected = true;
            return true;
        }

        /* Check if error is due to database being locked or busy */
        if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED) {
            fprintf(stderr, "Database is busy/locked (attempt %d/%d), retrying...\n",
                    attempt + 1, MAX_RETRY_ATTEMPTS);
            
            if (db->db) {
                sqlite3_close(db->db);
                db->db = NULL;
            }

            if (attempt < MAX_RETRY_ATTEMPTS - 1) {
                sleep_ms(retry_delay);
                retry_delay = (retry_delay * 2 > MAX_RETRY_DELAY_MS) ? 
                              MAX_RETRY_DELAY_MS : retry_delay * 2;
            }
        } else {
            /* Non-recoverable error */
            fprintf(stderr, "Failed to open database: %s\n", sqlite3_errmsg(db->db));
            if (db->db) {
                sqlite3_close(db->db);
                db->db = NULL;
            }
            return false;
        }
    }

    fprintf(stderr, "Failed to open database after %d attempts\n", MAX_RETRY_ATTEMPTS);
    return false;
}

/* Close database connection */
void nova_search_db_close(NovaSearchDB *db) {
    if (!db) {
        return;
    }

    if (db->db) {
        sqlite3_close(db->db);
        db->db = NULL;
    }

    db->is_connected = false;
}

/* Free database connection object */
void nova_search_db_free(NovaSearchDB *db) {
    if (!db) {
        return;
    }

    nova_search_db_close(db);
    
    if (db->db_path) {
        free(db->db_path);
        db->db_path = NULL;
    }

    free(db);
}

/* Execute search query with ranking logic */
SearchResult* nova_search_db_query(NovaSearchDB *db, const char *query, int max_results) {
    if (!db || !db->is_connected || !db->db) {
        fprintf(stderr, "Database is not connected\n");
        return NULL;
    }

    if (!query || strlen(query) == 0) {
        /* Empty query returns no results */
        return NULL;
    }

    if (max_results <= 0) {
        max_results = 50; /* Default limit */
    }

    /* Prepare SQL query with ranking logic */
    const char *sql = 
        "SELECT filename, path, file_type, size, modified_time "
        "FROM files "
        "WHERE filename LIKE '%' || ? || '%' "
        "ORDER BY "
        "  CASE "
        "    WHEN filename = ? THEN 0 "           /* Exact match */
        "    WHEN filename LIKE ? || '%' THEN 1 " /* Prefix match */
        "    ELSE 2 "                              /* Substring match */
        "  END, "
        "  filename COLLATE NOCASE "
        "LIMIT ?";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db->db));
        return NULL;
    }

    /* Bind parameters */
    sqlite3_bind_text(stmt, 1, query, -1, SQLITE_TRANSIENT); /* LIKE pattern */
    sqlite3_bind_text(stmt, 2, query, -1, SQLITE_TRANSIENT); /* Exact match */
    sqlite3_bind_text(stmt, 3, query, -1, SQLITE_TRANSIENT); /* Prefix match */
    sqlite3_bind_int(stmt, 4, max_results);

    /* Execute query and build result list */
    SearchResult *head = NULL;
    SearchResult *tail = NULL;

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        SearchResult *result = nova_search_result_new();
        if (!result) {
            fprintf(stderr, "Failed to allocate search result\n");
            break;
        }

        /* Extract columns */
        const unsigned char *filename = sqlite3_column_text(stmt, 0);
        const unsigned char *path = sqlite3_column_text(stmt, 1);
        const unsigned char *file_type = sqlite3_column_text(stmt, 2);
        
        result->filename = filename ? strdup((const char *)filename) : NULL;
        result->path = path ? strdup((const char *)path) : NULL;
        result->file_type = file_type ? strdup((const char *)file_type) : NULL;
        result->size = sqlite3_column_int64(stmt, 3);
        result->modified_time = sqlite3_column_int64(stmt, 4);
        result->next = NULL;

        /* Add to linked list */
        if (!head) {
            head = result;
            tail = result;
        } else {
            tail->next = result;
            tail = result;
        }
    }

    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        fprintf(stderr, "Query execution error: %s\n", sqlite3_errmsg(db->db));
    }

    sqlite3_finalize(stmt);
    return head;
}

/* Create a new search result */
SearchResult* nova_search_result_new(void) {
    SearchResult *result = malloc(sizeof(SearchResult));
    if (!result) {
        return NULL;
    }

    result->filename = NULL;
    result->path = NULL;
    result->file_type = NULL;
    result->size = 0;
    result->modified_time = 0;
    result->next = NULL;

    return result;
}

/* Free a single search result */
void nova_search_result_free(SearchResult *result) {
    if (!result) {
        return;
    }

    if (result->filename) {
        free(result->filename);
    }
    if (result->path) {
        free(result->path);
    }
    if (result->file_type) {
        free(result->file_type);
    }

    free(result);
}

/* Free entire result list */
void nova_search_result_list_free(SearchResult *results) {
    SearchResult *current = results;
    while (current) {
        SearchResult *next = current->next;
        nova_search_result_free(current);
        current = next;
    }
}

/* Count results in list */
int nova_search_result_count(SearchResult *results) {
    int count = 0;
    SearchResult *current = results;
    while (current) {
        count++;
        current = current->next;
    }
    return count;
}
