/**
 * Basic example demonstrating SquirrelDB C SDK usage.
 *
 * Compile: cc -I../include basic.c -L.. -lsquirreldb -lpthread -o basic
 * Run: ./basic
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "squirreldb.h"

static volatile int running = 1;

static void signal_handler(int sig) {
  (void)sig;
  running = 0;
}

static void change_callback(const sqrl_change_event_t *event, void *user_data) {
  (void)user_data;

  switch (event->type) {
    case SQRL_CHANGE_INITIAL:
      printf("Initial: %s\n", event->document ? event->document->data : "null");
      break;
    case SQRL_CHANGE_INSERT:
      printf("Insert: %s\n", event->new_doc ? event->new_doc->data : "null");
      break;
    case SQRL_CHANGE_UPDATE:
      printf("Update: %s -> %s\n",
        event->old_data ? event->old_data : "null",
        event->new_doc ? event->new_doc->data : "null");
      break;
    case SQRL_CHANGE_DELETE:
      printf("Delete: %s\n", event->old_data ? event->old_data : "null");
      break;
  }
}

int main(void) {
  sqrl_error_t err;

  /* Initialize library */
  err = sqrl_init();
  if (err != SQRL_OK) {
    fprintf(stderr, "Failed to initialize: %s\n", sqrl_error_string(err));
    return 1;
  }

  /* Connect to server */
  sqrl_client_t *client = NULL;
  err = sqrl_connect(&client, "localhost", SQRL_DEFAULT_PORT, NULL);
  if (err != SQRL_OK) {
    fprintf(stderr, "Failed to connect: %s\n", sqrl_error_string(err));
    sqrl_cleanup();
    return 1;
  }

  printf("Connected! Session ID: %s\n", sqrl_session_id(client));

  /* Ping */
  err = sqrl_ping(client);
  if (err != SQRL_OK) {
    fprintf(stderr, "Ping failed: %s\n", sqrl_error_string(err));
  } else {
    printf("Ping successful!\n");
  }

  /* List collections */
  char **collections = NULL;
  size_t count = 0;
  err = sqrl_list_collections(client, &collections, &count);
  if (err == SQRL_OK) {
    printf("Collections (%zu):", count);
    for (size_t i = 0; i < count; i++) {
      printf(" %s", collections[i]);
    }
    printf("\n");
    sqrl_string_array_free(collections, count);
  }

  /* Insert a document */
  sqrl_document_t *doc = NULL;
  err = sqrl_insert(client, "users",
    "{\"name\":\"Alice\",\"email\":\"alice@example.com\",\"active\":true}",
    &doc);
  if (err != SQRL_OK) {
    fprintf(stderr, "Insert failed: %s\n", sqrl_error_string(err));
  } else {
    printf("Inserted document:\n");
    printf("  ID: %s\n", doc->id);
    printf("  Collection: %s\n", doc->collection);
    printf("  Data: %s\n", doc->data);
    printf("  Created: %s\n", doc->created_at);
  }

  /* Query documents */
  char *result = NULL;
  err = sqrl_query(client, "db.table(\\\"users\\\").filter(u => u.active).run()", &result);
  if (err == SQRL_OK) {
    printf("Active users: %s\n", result);
    sqrl_string_free(result);
  }

  /* Update the document */
  if (doc) {
    sqrl_document_t *updated = NULL;
    err = sqrl_update(client, "users", doc->id,
      "{\"name\":\"Alice Updated\",\"email\":\"alice.updated@example.com\",\"active\":true}",
      &updated);
    if (err == SQRL_OK) {
      printf("Updated document:\n");
      printf("  ID: %s\n", updated->id);
      printf("  Data: %s\n", updated->data);
      sqrl_document_free(updated);
    }
  }

  /* Subscribe to changes */
  printf("\nSubscribing to user changes...\n");
  printf("(Insert/update/delete users from another client to see changes)\n");
  printf("Press Ctrl+C to exit.\n\n");

  sqrl_subscription_t *sub = NULL;
  err = sqrl_subscribe(client,
    "db.table(\\\"users\\\").changes()",
    change_callback, NULL, &sub);
  if (err != SQRL_OK) {
    fprintf(stderr, "Subscribe failed: %s\n", sqrl_error_string(err));
  } else {
    /* Wait for Ctrl+C */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    while (running && sqrl_is_connected(client)) {
      sleep(1);
    }

    printf("\nUnsubscribing...\n");
    sqrl_unsubscribe(sub);
  }

  /* Cleanup */
  sqrl_document_free(doc);
  sqrl_disconnect(client);
  sqrl_cleanup();

  printf("Done.\n");
  return 0;
}
