/**
 * SquirrelDB C Client SDK
 *
 * A native TCP client library for SquirrelDB.
 *
 * Example:
 *   sqrl_client_t *client = NULL;
 *   sqrl_error_t err = sqrl_connect(&client, "localhost", 8082, NULL);
 *   if (err != SQRL_OK) {
 *     fprintf(stderr, "Connect failed: %s\n", sqrl_error_string(err));
 *     return 1;
 *   }
 *
 *   sqrl_document_t *doc = NULL;
 *   err = sqrl_insert(client, "users", "{\"name\":\"Alice\"}", &doc);
 *   if (err == SQRL_OK) {
 *     printf("Inserted: %s\n", doc->id);
 *     sqrl_document_free(doc);
 *   }
 *
 *   sqrl_disconnect(client);
 */

#ifndef SQUIRRELDB_H
#define SQUIRRELDB_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Version */
#define SQRL_VERSION_MAJOR 0
#define SQRL_VERSION_MINOR 1
#define SQRL_VERSION_PATCH 0
#define SQRL_VERSION_STRING "0.1.0"

/* Protocol constants */
#define SQRL_PROTOCOL_VERSION 0x01
#define SQRL_MAX_MESSAGE_SIZE (16 * 1024 * 1024)
#define SQRL_DEFAULT_PORT 8082

/* Error codes */
typedef enum {
  SQRL_OK = 0,
  SQRL_ERR_CONNECT = 1,
  SQRL_ERR_HANDSHAKE = 2,
  SQRL_ERR_VERSION_MISMATCH = 3,
  SQRL_ERR_AUTH_FAILED = 4,
  SQRL_ERR_SEND = 5,
  SQRL_ERR_RECV = 6,
  SQRL_ERR_TIMEOUT = 7,
  SQRL_ERR_CLOSED = 8,
  SQRL_ERR_INVALID_ARG = 9,
  SQRL_ERR_MEMORY = 10,
  SQRL_ERR_ENCODE = 11,
  SQRL_ERR_DECODE = 12,
  SQRL_ERR_SERVER = 13,
  SQRL_ERR_NOT_FOUND = 14,
} sqrl_error_t;

/* Encoding formats */
typedef enum {
  SQRL_ENCODING_MSGPACK = 0x01,
  SQRL_ENCODING_JSON = 0x02,
} sqrl_encoding_t;

/* Change event types */
typedef enum {
  SQRL_CHANGE_INITIAL = 0,
  SQRL_CHANGE_INSERT = 1,
  SQRL_CHANGE_UPDATE = 2,
  SQRL_CHANGE_DELETE = 3,
} sqrl_change_type_t;

/* Forward declarations */
typedef struct sqrl_client sqrl_client_t;
typedef struct sqrl_subscription sqrl_subscription_t;

/* Document structure */
typedef struct {
  char *id;           /* UUID string (owned) */
  char *collection;   /* Collection name (owned) */
  char *data;         /* JSON data string (owned) */
  char *created_at;   /* ISO 8601 timestamp (owned) */
  char *updated_at;   /* ISO 8601 timestamp (owned) */
} sqrl_document_t;

/* Change event structure */
typedef struct {
  sqrl_change_type_t type;
  sqrl_document_t *document;  /* For initial events */
  sqrl_document_t *new_doc;   /* For insert/update events */
  char *old_data;             /* JSON string for update/delete (owned) */
} sqrl_change_event_t;

/* Connection options */
typedef struct {
  const char *auth_token;     /* Optional auth token */
  bool use_msgpack;           /* Use MessagePack encoding (default: true) */
  int connect_timeout_ms;     /* Connection timeout in ms (default: 5000) */
  int request_timeout_ms;     /* Request timeout in ms (default: 30000) */
} sqrl_options_t;

/* Subscription callback */
typedef void (*sqrl_change_callback_t)(
  const sqrl_change_event_t *event,
  void *user_data
);

/* ----------------------------------------------------------------------------
 * Initialization and Cleanup
 * --------------------------------------------------------------------------*/

/**
 * Initialize the library. Call once at program start.
 * Returns SQRL_OK on success.
 */
sqrl_error_t sqrl_init(void);

/**
 * Cleanup the library. Call once at program end.
 */
void sqrl_cleanup(void);

/**
 * Get error string for an error code.
 */
const char *sqrl_error_string(sqrl_error_t err);

/* ----------------------------------------------------------------------------
 * Connection Management
 * --------------------------------------------------------------------------*/

/**
 * Create default options.
 */
sqrl_options_t sqrl_options_default(void);

/**
 * Connect to SquirrelDB server.
 *
 * @param client_out  Output pointer to receive client handle
 * @param host        Server hostname
 * @param port        Server port (use SQRL_DEFAULT_PORT for default)
 * @param options     Connection options (NULL for defaults)
 * @return SQRL_OK on success
 */
sqrl_error_t sqrl_connect(
  sqrl_client_t **client_out,
  const char *host,
  uint16_t port,
  const sqrl_options_t *options
);

/**
 * Disconnect and free client.
 */
void sqrl_disconnect(sqrl_client_t *client);

/**
 * Get session ID (UUID string). Returns NULL if not connected.
 * The returned string is owned by the client and valid until disconnect.
 */
const char *sqrl_session_id(const sqrl_client_t *client);

/**
 * Check if client is connected.
 */
bool sqrl_is_connected(const sqrl_client_t *client);

/**
 * Ping the server.
 */
sqrl_error_t sqrl_ping(sqrl_client_t *client);

/* ----------------------------------------------------------------------------
 * Document Operations
 * --------------------------------------------------------------------------*/

/**
 * Execute a query and return raw JSON result.
 *
 * @param client      Client handle
 * @param query       Query string (e.g., "db.table(\"users\").run()")
 * @param result_out  Output pointer to receive JSON string (caller must free)
 * @return SQRL_OK on success
 */
sqrl_error_t sqrl_query(
  sqrl_client_t *client,
  const char *query,
  char **result_out
);

/**
 * Insert a document.
 *
 * @param client      Client handle
 * @param collection  Collection name
 * @param data        JSON data string
 * @param doc_out     Output pointer to receive document (caller must free with sqrl_document_free)
 * @return SQRL_OK on success
 */
sqrl_error_t sqrl_insert(
  sqrl_client_t *client,
  const char *collection,
  const char *data,
  sqrl_document_t **doc_out
);

/**
 * Update a document.
 *
 * @param client       Client handle
 * @param collection   Collection name
 * @param document_id  Document UUID string
 * @param data         New JSON data string
 * @param doc_out      Output pointer to receive updated document
 * @return SQRL_OK on success
 */
sqrl_error_t sqrl_update(
  sqrl_client_t *client,
  const char *collection,
  const char *document_id,
  const char *data,
  sqrl_document_t **doc_out
);

/**
 * Delete a document.
 *
 * @param client       Client handle
 * @param collection   Collection name
 * @param document_id  Document UUID string
 * @param doc_out      Output pointer to receive deleted document (optional, can be NULL)
 * @return SQRL_OK on success
 */
sqrl_error_t sqrl_delete(
  sqrl_client_t *client,
  const char *collection,
  const char *document_id,
  sqrl_document_t **doc_out
);

/**
 * List all collections.
 *
 * @param client       Client handle
 * @param names_out    Output array of collection names (caller must free with sqrl_string_array_free)
 * @param count_out    Output count of collections
 * @return SQRL_OK on success
 */
sqrl_error_t sqrl_list_collections(
  sqrl_client_t *client,
  char ***names_out,
  size_t *count_out
);

/* ----------------------------------------------------------------------------
 * Subscriptions
 * --------------------------------------------------------------------------*/

/**
 * Subscribe to changes.
 *
 * @param client      Client handle
 * @param query       Query string (e.g., "db.table(\"users\").changes()")
 * @param callback    Callback function for change events
 * @param user_data   User data passed to callback
 * @param sub_out     Output pointer to receive subscription handle
 * @return SQRL_OK on success
 */
sqrl_error_t sqrl_subscribe(
  sqrl_client_t *client,
  const char *query,
  sqrl_change_callback_t callback,
  void *user_data,
  sqrl_subscription_t **sub_out
);

/**
 * Unsubscribe from changes.
 */
sqrl_error_t sqrl_unsubscribe(sqrl_subscription_t *sub);

/**
 * Get subscription ID.
 */
const char *sqrl_subscription_id(const sqrl_subscription_t *sub);

/* ----------------------------------------------------------------------------
 * Memory Management
 * --------------------------------------------------------------------------*/

/**
 * Free a document.
 */
void sqrl_document_free(sqrl_document_t *doc);

/**
 * Free a change event.
 */
void sqrl_change_event_free(sqrl_change_event_t *event);

/**
 * Free a string allocated by the library.
 */
void sqrl_string_free(char *str);

/**
 * Free an array of strings.
 */
void sqrl_string_array_free(char **arr, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* SQUIRRELDB_H */
