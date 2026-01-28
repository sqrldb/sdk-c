/**
 * SquirrelDB Object Storage Client
 *
 * S3-compatible storage operations for C.
 *
 * Example:
 *   sqrl_storage_t* storage = sqrl_storage_new("http://localhost:9000", NULL);
 *   sqrl_storage_create_bucket(storage, "my-bucket");
 *   sqrl_storage_put_object(storage, "my-bucket", "hello.txt", "Hello!", 6, NULL);
 *   sqrl_storage_free(storage);
 */

#ifndef SQUIRRELDB_STORAGE_H
#define SQUIRRELDB_STORAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdbool.h>
#include <time.h>

/* Storage client opaque type */
typedef struct sqrl_storage sqrl_storage_t;

/* Storage options */
typedef struct {
    const char* endpoint;
    const char* access_key;
    const char* secret_key;
    const char* region;
} sqrl_storage_options_t;

/* Bucket info */
typedef struct {
    char* name;
    time_t created_at;
} sqrl_bucket_t;

/* Object info */
typedef struct {
    char* key;
    size_t size;
    char* etag;
    time_t last_modified;
    char* content_type;
} sqrl_object_t;

/* Bucket list */
typedef struct {
    sqrl_bucket_t* buckets;
    size_t count;
} sqrl_bucket_list_t;

/* Object list */
typedef struct {
    sqrl_object_t* objects;
    size_t count;
} sqrl_object_list_t;

/* Multipart upload */
typedef struct {
    char* upload_id;
    char* bucket;
    char* key;
} sqrl_multipart_upload_t;

/* Upload part */
typedef struct {
    int part_number;
    char* etag;
} sqrl_upload_part_t;

/* Error codes */
typedef enum {
    SQRL_STORAGE_OK = 0,
    SQRL_STORAGE_ERR_CONNECTION = -1,
    SQRL_STORAGE_ERR_AUTH = -2,
    SQRL_STORAGE_ERR_NOT_FOUND = -3,
    SQRL_STORAGE_ERR_CONFLICT = -4,
    SQRL_STORAGE_ERR_INVALID = -5,
    SQRL_STORAGE_ERR_INTERNAL = -6
} sqrl_storage_error_t;

/**
 * Create a new storage client
 * @param endpoint Storage endpoint URL
 * @param options Optional configuration (can be NULL)
 * @return New storage client (must be freed with sqrl_storage_free)
 */
sqrl_storage_t* sqrl_storage_new(const char* endpoint, const sqrl_storage_options_t* options);

/**
 * Free a storage client
 */
void sqrl_storage_free(sqrl_storage_t* storage);

/* Bucket operations */

/**
 * List all buckets
 * @param storage Storage client
 * @param out_list Output bucket list (must be freed with sqrl_bucket_list_free)
 * @return Error code
 */
sqrl_storage_error_t sqrl_storage_list_buckets(sqrl_storage_t* storage, sqrl_bucket_list_t** out_list);

/**
 * Free a bucket list
 */
void sqrl_bucket_list_free(sqrl_bucket_list_t* list);

/**
 * Create a bucket
 */
sqrl_storage_error_t sqrl_storage_create_bucket(sqrl_storage_t* storage, const char* name);

/**
 * Delete a bucket
 */
sqrl_storage_error_t sqrl_storage_delete_bucket(sqrl_storage_t* storage, const char* name);

/**
 * Check if bucket exists
 */
bool sqrl_storage_bucket_exists(sqrl_storage_t* storage, const char* name);

/* Object operations */

/**
 * List objects in a bucket
 * @param storage Storage client
 * @param bucket Bucket name
 * @param prefix Optional prefix filter (can be NULL)
 * @param max_keys Maximum number of objects to return
 * @param out_list Output object list (must be freed with sqrl_object_list_free)
 * @return Error code
 */
sqrl_storage_error_t sqrl_storage_list_objects(
    sqrl_storage_t* storage,
    const char* bucket,
    const char* prefix,
    size_t max_keys,
    sqrl_object_list_t** out_list
);

/**
 * Free an object list
 */
void sqrl_object_list_free(sqrl_object_list_t* list);

/**
 * Get object content
 * @param storage Storage client
 * @param bucket Bucket name
 * @param key Object key
 * @param out_data Output data (must be freed with free())
 * @param out_size Output data size
 * @return Error code
 */
sqrl_storage_error_t sqrl_storage_get_object(
    sqrl_storage_t* storage,
    const char* bucket,
    const char* key,
    char** out_data,
    size_t* out_size
);

/**
 * Put object
 * @param storage Storage client
 * @param bucket Bucket name
 * @param key Object key
 * @param data Object data
 * @param size Data size
 * @param content_type Content type (can be NULL for default)
 * @return Error code
 */
sqrl_storage_error_t sqrl_storage_put_object(
    sqrl_storage_t* storage,
    const char* bucket,
    const char* key,
    const void* data,
    size_t size,
    const char* content_type
);

/**
 * Delete object
 */
sqrl_storage_error_t sqrl_storage_delete_object(
    sqrl_storage_t* storage,
    const char* bucket,
    const char* key
);

/**
 * Copy object
 */
sqrl_storage_error_t sqrl_storage_copy_object(
    sqrl_storage_t* storage,
    const char* src_bucket,
    const char* src_key,
    const char* dst_bucket,
    const char* dst_key
);

/**
 * Check if object exists
 */
bool sqrl_storage_object_exists(
    sqrl_storage_t* storage,
    const char* bucket,
    const char* key
);

/* Multipart upload */

/**
 * Create multipart upload
 */
sqrl_storage_error_t sqrl_storage_create_multipart_upload(
    sqrl_storage_t* storage,
    const char* bucket,
    const char* key,
    const char* content_type,
    sqrl_multipart_upload_t** out_upload
);

/**
 * Free multipart upload info
 */
void sqrl_multipart_upload_free(sqrl_multipart_upload_t* upload);

/**
 * Upload part
 */
sqrl_storage_error_t sqrl_storage_upload_part(
    sqrl_storage_t* storage,
    const char* bucket,
    const char* key,
    const char* upload_id,
    int part_number,
    const void* data,
    size_t size,
    sqrl_upload_part_t** out_part
);

/**
 * Free upload part info
 */
void sqrl_upload_part_free(sqrl_upload_part_t* part);

/**
 * Complete multipart upload
 */
sqrl_storage_error_t sqrl_storage_complete_multipart_upload(
    sqrl_storage_t* storage,
    const char* bucket,
    const char* key,
    const char* upload_id,
    sqrl_upload_part_t** parts,
    size_t part_count
);

/**
 * Abort multipart upload
 */
sqrl_storage_error_t sqrl_storage_abort_multipart_upload(
    sqrl_storage_t* storage,
    const char* bucket,
    const char* key,
    const char* upload_id
);

#ifdef __cplusplus
}
#endif

#endif /* SQUIRRELDB_STORAGE_H */
