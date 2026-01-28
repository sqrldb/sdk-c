/**
 * SquirrelDB Query Builder
 *
 * Provides a fluent C API for building queries.
 * Uses MongoDB-like naming: find/sort/limit
 *
 * Example:
 *   sqrl_query_t* q = sqrl_table("users");
 *   sqrl_find_gt(q, "age", 21);
 *   sqrl_sort(q, "name", SQRL_ASC);
 *   sqrl_limit(q, 10);
 *   const char* query_str = sqrl_query_compile(q);
 *   sqrl_query_free(q);
 */

#ifndef SQUIRRELDB_QUERY_H
#define SQUIRRELDB_QUERY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdbool.h>

/* Sort direction */
typedef enum {
    SQRL_ASC = 0,
    SQRL_DESC = 1
} sqrl_sort_dir_t;

/* Query builder opaque type */
typedef struct sqrl_query sqrl_query_t;

/**
 * Create a new query builder for a table
 * @param table_name Name of the table
 * @return New query builder (must be freed with sqrl_query_free)
 */
sqrl_query_t* sqrl_table(const char* table_name);

/**
 * Free a query builder
 * @param query Query builder to free
 */
void sqrl_query_free(sqrl_query_t* query);

/* Filter operations - find documents matching condition */

/**
 * Find documents where field equals value (string)
 */
sqrl_query_t* sqrl_find_eq_str(sqrl_query_t* query, const char* field, const char* value);

/**
 * Find documents where field equals value (int)
 */
sqrl_query_t* sqrl_find_eq_int(sqrl_query_t* query, const char* field, long value);

/**
 * Find documents where field equals value (double)
 */
sqrl_query_t* sqrl_find_eq_double(sqrl_query_t* query, const char* field, double value);

/**
 * Find documents where field equals value (bool)
 */
sqrl_query_t* sqrl_find_eq_bool(sqrl_query_t* query, const char* field, bool value);

/**
 * Find documents where field does not equal value
 */
sqrl_query_t* sqrl_find_ne_str(sqrl_query_t* query, const char* field, const char* value);
sqrl_query_t* sqrl_find_ne_int(sqrl_query_t* query, const char* field, long value);

/**
 * Find documents where field is greater than value
 */
sqrl_query_t* sqrl_find_gt(sqrl_query_t* query, const char* field, double value);

/**
 * Find documents where field is greater than or equal to value
 */
sqrl_query_t* sqrl_find_gte(sqrl_query_t* query, const char* field, double value);

/**
 * Find documents where field is less than value
 */
sqrl_query_t* sqrl_find_lt(sqrl_query_t* query, const char* field, double value);

/**
 * Find documents where field is less than or equal to value
 */
sqrl_query_t* sqrl_find_lte(sqrl_query_t* query, const char* field, double value);

/**
 * Find documents where string field contains value
 */
sqrl_query_t* sqrl_find_contains(sqrl_query_t* query, const char* field, const char* value);

/**
 * Find documents where string field starts with value
 */
sqrl_query_t* sqrl_find_starts_with(sqrl_query_t* query, const char* field, const char* value);

/**
 * Find documents where string field ends with value
 */
sqrl_query_t* sqrl_find_ends_with(sqrl_query_t* query, const char* field, const char* value);

/**
 * Find documents where field exists (or not)
 */
sqrl_query_t* sqrl_find_exists(sqrl_query_t* query, const char* field, bool exists);

/**
 * Sort results by field
 * @param query Query builder
 * @param field Field to sort by
 * @param direction Sort direction (SQRL_ASC or SQRL_DESC)
 */
sqrl_query_t* sqrl_sort(sqrl_query_t* query, const char* field, sqrl_sort_dir_t direction);

/**
 * Limit number of results
 * @param query Query builder
 * @param n Maximum number of results
 */
sqrl_query_t* sqrl_limit(sqrl_query_t* query, size_t n);

/**
 * Skip results (offset)
 * @param query Query builder
 * @param n Number of results to skip
 */
sqrl_query_t* sqrl_skip(sqrl_query_t* query, size_t n);

/**
 * Set query to subscribe to changes
 */
sqrl_query_t* sqrl_changes(sqrl_query_t* query);

/**
 * Compile query to SquirrelDB JS string
 * @param query Query builder
 * @return Compiled query string (must be freed with free())
 */
char* sqrl_query_compile(sqrl_query_t* query);

#ifdef __cplusplus
}
#endif

#endif /* SQUIRRELDB_QUERY_H */
