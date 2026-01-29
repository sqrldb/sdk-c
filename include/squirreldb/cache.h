/**
 * SquirrelDB Cache Client
 *
 * Redis-compatible cache operations using RESP protocol over TCP.
 *
 * Example:
 *   sqrl_cache_t* cache = sqrl_cache_connect("localhost", 6379);
 *   if (cache) {
 *     sqrl_cache_set(cache, "greeting", "hello", 0);
 *     char* value = sqrl_cache_get(cache, "greeting");
 *     if (value) {
 *       printf("Value: %s\n", value);
 *       sqrl_cache_free_string(value);
 *     }
 *     sqrl_cache_close(cache);
 *   }
 */

#ifndef SQUIRRELDB_CACHE_H
#define SQUIRRELDB_CACHE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sqrl_cache sqrl_cache_t;

/* Connection */
sqrl_cache_t* sqrl_cache_connect(const char* host, int port);
void sqrl_cache_close(sqrl_cache_t* cache);

/* Basic operations */
char* sqrl_cache_get(sqrl_cache_t* cache, const char* key);
int sqrl_cache_set(sqrl_cache_t* cache, const char* key, const char* value, int ttl);
int sqrl_cache_del(sqrl_cache_t* cache, const char* key);
int sqrl_cache_exists(sqrl_cache_t* cache, const char* key);

/* TTL operations */
int sqrl_cache_expire(sqrl_cache_t* cache, const char* key, int seconds);
long sqrl_cache_ttl(sqrl_cache_t* cache, const char* key);
int sqrl_cache_persist(sqrl_cache_t* cache, const char* key);

/* Numeric operations */
long sqrl_cache_incr(sqrl_cache_t* cache, const char* key);
long sqrl_cache_decr(sqrl_cache_t* cache, const char* key);
long sqrl_cache_incrby(sqrl_cache_t* cache, const char* key, long amount);

/* Bulk operations */
char** sqrl_cache_keys(sqrl_cache_t* cache, const char* pattern, int* count);
int sqrl_cache_dbsize(sqrl_cache_t* cache);
int sqrl_cache_flush(sqrl_cache_t* cache);

/* Admin */
int sqrl_cache_ping(sqrl_cache_t* cache);

/* Memory management */
void sqrl_cache_free_string(char* str);
void sqrl_cache_free_strings(char** strs, int count);

#ifdef __cplusplus
}
#endif

#endif /* SQUIRRELDB_CACHE_H */
