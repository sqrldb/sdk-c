/**
 * Tests for SquirrelDB C SDK
 *
 * A simple test framework using assertions.
 * Compile: cc -I../include test_protocol.c -o test_protocol
 * Run: ./test_protocol
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "squirreldb.h"

static int tests_run = 0;
static int tests_passed = 0;

#define RUN_TEST(test_func) do { \
  tests_run++; \
  printf("  Running %s... ", #test_func); \
  fflush(stdout); \
  if (test_func()) { \
    tests_passed++; \
    printf("PASS\n"); \
  } else { \
    printf("FAIL\n"); \
  } \
} while(0)

/* Test protocol constants */
static int test_version_constants(void) {
  if (SQRL_VERSION_MAJOR != 0) return 0;
  if (SQRL_VERSION_MINOR != 1) return 0;
  if (SQRL_VERSION_PATCH != 0) return 0;
  if (strcmp(SQRL_VERSION_STRING, "0.1.0") != 0) return 0;
  return 1;
}

static int test_protocol_constants(void) {
  if (SQRL_PROTOCOL_VERSION != 0x01) return 0;
  if (SQRL_MAX_MESSAGE_SIZE != 16 * 1024 * 1024) return 0;
  if (SQRL_DEFAULT_PORT != 8082) return 0;
  return 1;
}

/* Test error codes */
static int test_error_codes(void) {
  if (SQRL_OK != 0) return 0;
  if (SQRL_ERR_CONNECT != 1) return 0;
  if (SQRL_ERR_HANDSHAKE != 2) return 0;
  if (SQRL_ERR_VERSION_MISMATCH != 3) return 0;
  if (SQRL_ERR_AUTH_FAILED != 4) return 0;
  if (SQRL_ERR_SEND != 5) return 0;
  if (SQRL_ERR_RECV != 6) return 0;
  if (SQRL_ERR_TIMEOUT != 7) return 0;
  if (SQRL_ERR_CLOSED != 8) return 0;
  if (SQRL_ERR_INVALID_ARG != 9) return 0;
  if (SQRL_ERR_MEMORY != 10) return 0;
  if (SQRL_ERR_ENCODE != 11) return 0;
  if (SQRL_ERR_DECODE != 12) return 0;
  if (SQRL_ERR_SERVER != 13) return 0;
  if (SQRL_ERR_NOT_FOUND != 14) return 0;
  return 1;
}

/* Test error strings */
static int test_error_strings(void) {
  const char *err;

  err = sqrl_error_string(SQRL_OK);
  if (err == NULL || strcmp(err, "Success") != 0) return 0;

  err = sqrl_error_string(SQRL_ERR_CONNECT);
  if (err == NULL || strlen(err) == 0) return 0;

  err = sqrl_error_string(SQRL_ERR_AUTH_FAILED);
  if (err == NULL || strlen(err) == 0) return 0;

  err = sqrl_error_string(SQRL_ERR_TIMEOUT);
  if (err == NULL || strlen(err) == 0) return 0;

  /* Test invalid error code */
  err = sqrl_error_string((sqrl_error_t)999);
  if (err == NULL) return 0; /* Should return something for unknown errors */

  return 1;
}

/* Test encoding constants */
static int test_encoding_constants(void) {
  if (SQRL_ENCODING_MSGPACK != 0x01) return 0;
  if (SQRL_ENCODING_JSON != 0x02) return 0;
  return 1;
}

/* Test change type constants */
static int test_change_type_constants(void) {
  if (SQRL_CHANGE_INITIAL != 0) return 0;
  if (SQRL_CHANGE_INSERT != 1) return 0;
  if (SQRL_CHANGE_UPDATE != 2) return 0;
  if (SQRL_CHANGE_DELETE != 3) return 0;
  return 1;
}

/* Test default options */
static int test_default_options(void) {
  sqrl_options_t opts = sqrl_options_default();

  if (opts.auth_token != NULL) return 0;
  if (!opts.use_msgpack) return 0;
  if (opts.connect_timeout_ms <= 0) return 0;
  if (opts.request_timeout_ms <= 0) return 0;

  return 1;
}

/* Test init/cleanup */
static int test_init_cleanup(void) {
  sqrl_error_t err;

  err = sqrl_init();
  if (err != SQRL_OK) return 0;

  sqrl_cleanup();

  /* Should be safe to init again */
  err = sqrl_init();
  if (err != SQRL_OK) return 0;

  sqrl_cleanup();

  return 1;
}

/* Test connect with NULL client_out */
static int test_connect_null_client(void) {
  sqrl_error_t err;

  err = sqrl_init();
  if (err != SQRL_OK) return 0;

  err = sqrl_connect(NULL, "localhost", SQRL_DEFAULT_PORT, NULL);
  if (err != SQRL_ERR_INVALID_ARG) {
    sqrl_cleanup();
    return 0;
  }

  sqrl_cleanup();
  return 1;
}

/* Test connect with NULL host */
static int test_connect_null_host(void) {
  sqrl_error_t err;
  sqrl_client_t *client = NULL;

  err = sqrl_init();
  if (err != SQRL_OK) return 0;

  err = sqrl_connect(&client, NULL, SQRL_DEFAULT_PORT, NULL);
  if (err != SQRL_ERR_INVALID_ARG) {
    sqrl_cleanup();
    return 0;
  }

  sqrl_cleanup();
  return 1;
}

/* Test connect to non-existent server */
static int test_connect_refused(void) {
  sqrl_error_t err;
  sqrl_client_t *client = NULL;

  err = sqrl_init();
  if (err != SQRL_OK) return 0;

  /* Port 59999 unlikely to be listening */
  err = sqrl_connect(&client, "127.0.0.1", 59999, NULL);
  if (err == SQRL_OK) {
    sqrl_disconnect(client);
    sqrl_cleanup();
    return 0; /* Should have failed */
  }

  /* Should return a connection error */
  if (err != SQRL_ERR_CONNECT) {
    sqrl_cleanup();
    return 0;
  }

  sqrl_cleanup();
  return 1;
}

/* Test document free with NULL */
static int test_document_free_null(void) {
  /* Should not crash */
  sqrl_document_free(NULL);
  return 1;
}

/* Test change event free with NULL */
static int test_change_event_free_null(void) {
  /* Should not crash */
  sqrl_change_event_free(NULL);
  return 1;
}

/* Test string free with NULL */
static int test_string_free_null(void) {
  /* Should not crash */
  sqrl_string_free(NULL);
  return 1;
}

/* Test string array free with NULL */
static int test_string_array_free_null(void) {
  /* Should not crash */
  sqrl_string_array_free(NULL, 0);
  sqrl_string_array_free(NULL, 10);
  return 1;
}

/* Test is_connected with NULL */
static int test_is_connected_null(void) {
  /* Should return false and not crash */
  if (sqrl_is_connected(NULL) != false) return 0;
  return 1;
}

/* Test session_id with NULL */
static int test_session_id_null(void) {
  /* Should return NULL and not crash */
  if (sqrl_session_id(NULL) != NULL) return 0;
  return 1;
}

int main(void) {
  printf("SquirrelDB C SDK Tests\n");
  printf("======================\n\n");

  printf("Protocol Constants:\n");
  RUN_TEST(test_version_constants);
  RUN_TEST(test_protocol_constants);
  RUN_TEST(test_error_codes);
  RUN_TEST(test_error_strings);
  RUN_TEST(test_encoding_constants);
  RUN_TEST(test_change_type_constants);

  printf("\nOptions:\n");
  RUN_TEST(test_default_options);

  printf("\nInitialization:\n");
  RUN_TEST(test_init_cleanup);

  printf("\nConnection Errors:\n");
  RUN_TEST(test_connect_null_client);
  RUN_TEST(test_connect_null_host);
  RUN_TEST(test_connect_refused);

  printf("\nNULL Safety:\n");
  RUN_TEST(test_document_free_null);
  RUN_TEST(test_change_event_free_null);
  RUN_TEST(test_string_free_null);
  RUN_TEST(test_string_array_free_null);
  RUN_TEST(test_is_connected_null);
  RUN_TEST(test_session_id_null);

  printf("\n======================\n");
  printf("Results: %d/%d tests passed\n", tests_passed, tests_run);

  return (tests_passed == tests_run) ? 0 : 1;
}
