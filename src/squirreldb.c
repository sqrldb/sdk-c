/**
 * SquirrelDB C Client SDK Implementation
 */

#include "squirreldb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <fcntl.h>

/* Protocol constants */
static const uint8_t MAGIC[4] = {'S', 'Q', 'R', 'L'};

/* Message types */
#define MSG_TYPE_REQUEST      0x01
#define MSG_TYPE_RESPONSE     0x02
#define MSG_TYPE_NOTIFICATION 0x03

/* Handshake status */
#define HANDSHAKE_SUCCESS         0x00
#define HANDSHAKE_VERSION_MISMATCH 0x01
#define HANDSHAKE_AUTH_FAILED     0x02

/* Internal structures */
typedef struct pending_request {
  char *id;
  char *response;
  sqrl_error_t error;
  bool completed;
  pthread_cond_t cond;
  pthread_mutex_t mutex;
  struct pending_request *next;
} pending_request_t;

typedef struct subscription_entry {
  char *id;
  sqrl_change_callback_t callback;
  void *user_data;
  struct subscription_entry *next;
} subscription_entry_t;

struct sqrl_client {
  int fd;
  char *session_id;
  sqrl_encoding_t encoding;
  bool connected;
  uint64_t request_id;
  int request_timeout_ms;

  pthread_t reader_thread;
  bool reader_running;

  pthread_mutex_t write_mutex;
  pthread_mutex_t pending_mutex;
  pending_request_t *pending_requests;

  pthread_mutex_t subs_mutex;
  subscription_entry_t *subscriptions;
};

struct sqrl_subscription {
  char *id;
  sqrl_client_t *client;
};

/* Global init flag */
static bool g_initialized = false;

/* ----------------------------------------------------------------------------
 * Utility Functions
 * --------------------------------------------------------------------------*/

static char *strdup_safe(const char *s) {
  if (!s) return NULL;
  return strdup(s);
}

static uint32_t read_u32_be(const uint8_t *buf) {
  return ((uint32_t)buf[0] << 24) |
         ((uint32_t)buf[1] << 16) |
         ((uint32_t)buf[2] << 8) |
         (uint32_t)buf[3];
}

static void write_u32_be(uint8_t *buf, uint32_t val) {
  buf[0] = (val >> 24) & 0xFF;
  buf[1] = (val >> 16) & 0xFF;
  buf[2] = (val >> 8) & 0xFF;
  buf[3] = val & 0xFF;
}

static void write_u16_be(uint8_t *buf, uint16_t val) {
  buf[0] = (val >> 8) & 0xFF;
  buf[1] = val & 0xFF;
}

static void uuid_to_string(const uint8_t *bytes, char *out) {
  sprintf(out,
    "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
    bytes[0], bytes[1], bytes[2], bytes[3],
    bytes[4], bytes[5], bytes[6], bytes[7],
    bytes[8], bytes[9], bytes[10], bytes[11],
    bytes[12], bytes[13], bytes[14], bytes[15]);
}

/* Simple JSON string extraction (finds "key":"value" and returns value) */
static char *json_get_string(const char *json, const char *key) {
  char search[256];
  snprintf(search, sizeof(search), "\"%s\":\"", key);

  const char *start = strstr(json, search);
  if (!start) {
    /* Try without quotes for nested objects */
    snprintf(search, sizeof(search), "\"%s\":", key);
    start = strstr(json, search);
    if (!start) return NULL;
    start += strlen(search);

    /* Skip whitespace */
    while (*start == ' ' || *start == '\t') start++;

    if (*start == '"') {
      start++;
      const char *end = strchr(start, '"');
      if (!end) return NULL;
      size_t len = end - start;
      char *result = malloc(len + 1);
      if (!result) return NULL;
      memcpy(result, start, len);
      result[len] = '\0';
      return result;
    }
    return NULL;
  }

  start += strlen(search);
  const char *end = strchr(start, '"');
  if (!end) return NULL;

  size_t len = end - start;
  char *result = malloc(len + 1);
  if (!result) return NULL;
  memcpy(result, start, len);
  result[len] = '\0';
  return result;
}

/* Extract JSON object value */
static char *json_get_object(const char *json, const char *key) {
  char search[256];
  snprintf(search, sizeof(search), "\"%s\":", key);

  const char *start = strstr(json, search);
  if (!start) return NULL;
  start += strlen(search);

  /* Skip whitespace */
  while (*start == ' ' || *start == '\t' || *start == '\n') start++;

  if (*start != '{') return NULL;

  /* Find matching brace */
  int depth = 1;
  const char *end = start + 1;
  while (*end && depth > 0) {
    if (*end == '{') depth++;
    else if (*end == '}') depth--;
    end++;
  }

  if (depth != 0) return NULL;

  size_t len = end - start;
  char *result = malloc(len + 1);
  if (!result) return NULL;
  memcpy(result, start, len);
  result[len] = '\0';
  return result;
}

/* ----------------------------------------------------------------------------
 * Network I/O
 * --------------------------------------------------------------------------*/

static ssize_t send_all(int fd, const void *buf, size_t len) {
  const uint8_t *p = buf;
  size_t remaining = len;

  while (remaining > 0) {
    ssize_t n = send(fd, p, remaining, 0);
    if (n < 0) {
      if (errno == EINTR) continue;
      return -1;
    }
    p += n;
    remaining -= n;
  }
  return len;
}

static ssize_t recv_all(int fd, void *buf, size_t len, int timeout_ms) {
  uint8_t *p = buf;
  size_t remaining = len;

  while (remaining > 0) {
    if (timeout_ms > 0) {
      fd_set fds;
      struct timeval tv;
      FD_ZERO(&fds);
      FD_SET(fd, &fds);
      tv.tv_sec = timeout_ms / 1000;
      tv.tv_usec = (timeout_ms % 1000) * 1000;

      int ret = select(fd + 1, &fds, NULL, NULL, &tv);
      if (ret < 0) {
        if (errno == EINTR) continue;
        return -1;
      }
      if (ret == 0) {
        errno = ETIMEDOUT;
        return -1;
      }
    }

    ssize_t n = recv(fd, p, remaining, 0);
    if (n < 0) {
      if (errno == EINTR) continue;
      return -1;
    }
    if (n == 0) {
      /* Connection closed */
      return -1;
    }
    p += n;
    remaining -= n;
  }
  return len;
}

/* ----------------------------------------------------------------------------
 * Protocol Implementation
 * --------------------------------------------------------------------------*/

static sqrl_error_t do_handshake(sqrl_client_t *client, const sqrl_options_t *opts) {
  /* Build handshake packet */
  const char *token = opts && opts->auth_token ? opts->auth_token : "";
  size_t token_len = strlen(token);

  size_t pkt_len = 8 + token_len;
  uint8_t *pkt = malloc(pkt_len);
  if (!pkt) return SQRL_ERR_MEMORY;

  memcpy(pkt, MAGIC, 4);
  pkt[4] = SQRL_PROTOCOL_VERSION;

  uint8_t flags = 0;
  if (!opts || opts->use_msgpack) flags |= 0x01;
  flags |= 0x02; /* JSON fallback */
  pkt[5] = flags;

  write_u16_be(pkt + 6, (uint16_t)token_len);
  if (token_len > 0) {
    memcpy(pkt + 8, token, token_len);
  }

  if (send_all(client->fd, pkt, pkt_len) < 0) {
    free(pkt);
    return SQRL_ERR_SEND;
  }
  free(pkt);

  /* Read response (19 bytes) */
  uint8_t resp[19];
  if (recv_all(client->fd, resp, 19, opts ? opts->connect_timeout_ms : 5000) < 0) {
    return SQRL_ERR_RECV;
  }

  uint8_t status = resp[0];
  /* uint8_t version = resp[1]; -- unused, for future version negotiation */
  uint8_t resp_flags = resp[2];

  if (status == HANDSHAKE_VERSION_MISMATCH) {
    return SQRL_ERR_VERSION_MISMATCH;
  }
  if (status == HANDSHAKE_AUTH_FAILED) {
    return SQRL_ERR_AUTH_FAILED;
  }
  if (status != HANDSHAKE_SUCCESS) {
    return SQRL_ERR_HANDSHAKE;
  }

  /* Parse session ID */
  client->session_id = malloc(37);
  if (!client->session_id) return SQRL_ERR_MEMORY;
  uuid_to_string(resp + 3, client->session_id);

  /* Set encoding */
  client->encoding = (resp_flags & 0x01) ? SQRL_ENCODING_MSGPACK : SQRL_ENCODING_JSON;

  return SQRL_OK;
}

static sqrl_error_t send_frame(sqrl_client_t *client, const char *json) {
  size_t payload_len = strlen(json);
  uint32_t length = (uint32_t)(payload_len + 2);

  size_t frame_len = 6 + payload_len;
  uint8_t *frame = malloc(frame_len);
  if (!frame) return SQRL_ERR_MEMORY;

  write_u32_be(frame, length);
  frame[4] = MSG_TYPE_REQUEST;
  frame[5] = SQRL_ENCODING_JSON; /* Always use JSON for simplicity in C */
  memcpy(frame + 6, json, payload_len);

  pthread_mutex_lock(&client->write_mutex);
  ssize_t sent = send_all(client->fd, frame, frame_len);
  pthread_mutex_unlock(&client->write_mutex);

  free(frame);
  return (sent < 0) ? SQRL_ERR_SEND : SQRL_OK;
}

static sqrl_error_t recv_frame(sqrl_client_t *client, uint8_t *msg_type, char **json_out) {
  /* Read header (6 bytes) */
  uint8_t header[6];
  if (recv_all(client->fd, header, 6, 0) < 0) {
    return SQRL_ERR_RECV;
  }

  uint32_t length = read_u32_be(header);
  *msg_type = header[4];
  /* uint8_t encoding = header[5]; */

  if (length < 2 || length > SQRL_MAX_MESSAGE_SIZE) {
    return SQRL_ERR_DECODE;
  }

  uint32_t payload_len = length - 2;
  char *payload = malloc(payload_len + 1);
  if (!payload) return SQRL_ERR_MEMORY;

  if (recv_all(client->fd, payload, payload_len, 0) < 0) {
    free(payload);
    return SQRL_ERR_RECV;
  }
  payload[payload_len] = '\0';

  *json_out = payload;
  return SQRL_OK;
}

/* ----------------------------------------------------------------------------
 * Reader Thread
 * --------------------------------------------------------------------------*/

static void dispatch_change(sqrl_client_t *client, const char *id, const char *json) {
  pthread_mutex_lock(&client->subs_mutex);

  subscription_entry_t *entry = client->subscriptions;
  while (entry) {
    if (strcmp(entry->id, id) == 0) {
      /* Parse change event */
      sqrl_change_event_t event = {0};

      char *type_str = json_get_string(json, "type");
      if (type_str) {
        if (strcmp(type_str, "initial") == 0) event.type = SQRL_CHANGE_INITIAL;
        else if (strcmp(type_str, "insert") == 0) event.type = SQRL_CHANGE_INSERT;
        else if (strcmp(type_str, "update") == 0) event.type = SQRL_CHANGE_UPDATE;
        else if (strcmp(type_str, "delete") == 0) event.type = SQRL_CHANGE_DELETE;
        free(type_str);
      }

      /* Call callback */
      entry->callback(&event, entry->user_data);

      /* Clean up */
      if (event.document) sqrl_document_free(event.document);
      if (event.new_doc) sqrl_document_free(event.new_doc);
      if (event.old_data) free(event.old_data);

      break;
    }
    entry = entry->next;
  }

  pthread_mutex_unlock(&client->subs_mutex);
}

static void dispatch_response(sqrl_client_t *client, const char *id, const char *json) {
  pthread_mutex_lock(&client->pending_mutex);

  pending_request_t *req = client->pending_requests;

  while (req) {
    if (strcmp(req->id, id) == 0) {
      pthread_mutex_lock(&req->mutex);
      req->response = strdup_safe(json);
      req->completed = true;
      pthread_cond_signal(&req->cond);
      pthread_mutex_unlock(&req->mutex);
      break;
    }
    req = req->next;
  }

  pthread_mutex_unlock(&client->pending_mutex);
}

static void *reader_thread_func(void *arg) {
  sqrl_client_t *client = arg;

  while (client->reader_running) {
    uint8_t msg_type;
    char *json = NULL;

    sqrl_error_t err = recv_frame(client, &msg_type, &json);
    if (err != SQRL_OK) {
      if (client->reader_running) {
        client->connected = false;
      }
      break;
    }

    /* Extract message ID and type */
    char *msg_id = json_get_string(json, "id");
    char *resp_type = json_get_string(json, "type");

    if (msg_id && resp_type) {
      if (strcmp(resp_type, "change") == 0) {
        char *change_json = json_get_object(json, "change");
        if (change_json) {
          dispatch_change(client, msg_id, change_json);
          free(change_json);
        }
      } else {
        dispatch_response(client, msg_id, json);
      }
    }

    free(msg_id);
    free(resp_type);
    free(json);
  }

  return NULL;
}

/* ----------------------------------------------------------------------------
 * Request/Response
 * --------------------------------------------------------------------------*/

static sqrl_error_t send_request(
  sqrl_client_t *client,
  const char *json,
  const char *id,
  char **response_out
) {
  /* Create pending request */
  pending_request_t *req = calloc(1, sizeof(pending_request_t));
  if (!req) return SQRL_ERR_MEMORY;

  req->id = strdup_safe(id);
  pthread_mutex_init(&req->mutex, NULL);
  pthread_cond_init(&req->cond, NULL);

  /* Add to pending list */
  pthread_mutex_lock(&client->pending_mutex);
  req->next = client->pending_requests;
  client->pending_requests = req;
  pthread_mutex_unlock(&client->pending_mutex);

  /* Send request */
  sqrl_error_t err = send_frame(client, json);
  if (err != SQRL_OK) {
    /* Remove from pending */
    pthread_mutex_lock(&client->pending_mutex);
    if (client->pending_requests == req) {
      client->pending_requests = req->next;
    } else {
      pending_request_t *p = client->pending_requests;
      while (p && p->next != req) p = p->next;
      if (p) p->next = req->next;
    }
    pthread_mutex_unlock(&client->pending_mutex);

    free(req->id);
    pthread_mutex_destroy(&req->mutex);
    pthread_cond_destroy(&req->cond);
    free(req);
    return err;
  }

  /* Wait for response */
  pthread_mutex_lock(&req->mutex);
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  ts.tv_sec += client->request_timeout_ms / 1000;
  ts.tv_nsec += (client->request_timeout_ms % 1000) * 1000000;
  if (ts.tv_nsec >= 1000000000) {
    ts.tv_sec++;
    ts.tv_nsec -= 1000000000;
  }

  while (!req->completed) {
    int ret = pthread_cond_timedwait(&req->cond, &req->mutex, &ts);
    if (ret == ETIMEDOUT) {
      pthread_mutex_unlock(&req->mutex);
      err = SQRL_ERR_TIMEOUT;
      goto cleanup;
    }
  }
  pthread_mutex_unlock(&req->mutex);

  if (req->response) {
    *response_out = req->response;
    req->response = NULL;
    err = SQRL_OK;
  } else {
    err = SQRL_ERR_RECV;
  }

cleanup:
  /* Remove from pending */
  pthread_mutex_lock(&client->pending_mutex);
  if (client->pending_requests == req) {
    client->pending_requests = req->next;
  } else {
    pending_request_t *p = client->pending_requests;
    while (p && p->next != req) p = p->next;
    if (p) p->next = req->next;
  }
  pthread_mutex_unlock(&client->pending_mutex);

  free(req->id);
  free(req->response);
  pthread_mutex_destroy(&req->mutex);
  pthread_cond_destroy(&req->cond);
  free(req);

  return err;
}

static char *next_request_id(sqrl_client_t *client) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%llu", (unsigned long long)++client->request_id);
  return strdup(buf);
}

/* ----------------------------------------------------------------------------
 * Public API Implementation
 * --------------------------------------------------------------------------*/

sqrl_error_t sqrl_init(void) {
  if (g_initialized) return SQRL_OK;
  g_initialized = true;
  return SQRL_OK;
}

void sqrl_cleanup(void) {
  g_initialized = false;
}

const char *sqrl_error_string(sqrl_error_t err) {
  switch (err) {
    case SQRL_OK: return "Success";
    case SQRL_ERR_CONNECT: return "Connection failed";
    case SQRL_ERR_HANDSHAKE: return "Handshake failed";
    case SQRL_ERR_VERSION_MISMATCH: return "Protocol version mismatch";
    case SQRL_ERR_AUTH_FAILED: return "Authentication failed";
    case SQRL_ERR_SEND: return "Send failed";
    case SQRL_ERR_RECV: return "Receive failed";
    case SQRL_ERR_TIMEOUT: return "Timeout";
    case SQRL_ERR_CLOSED: return "Connection closed";
    case SQRL_ERR_INVALID_ARG: return "Invalid argument";
    case SQRL_ERR_MEMORY: return "Memory allocation failed";
    case SQRL_ERR_ENCODE: return "Encoding failed";
    case SQRL_ERR_DECODE: return "Decoding failed";
    case SQRL_ERR_SERVER: return "Server error";
    case SQRL_ERR_NOT_FOUND: return "Not found";
    default: return "Unknown error";
  }
}

sqrl_options_t sqrl_options_default(void) {
  sqrl_options_t opts = {
    .auth_token = NULL,
    .use_msgpack = true,
    .connect_timeout_ms = 5000,
    .request_timeout_ms = 30000,
  };
  return opts;
}

sqrl_error_t sqrl_connect(
  sqrl_client_t **client_out,
  const char *host,
  uint16_t port,
  const sqrl_options_t *options
) {
  if (!client_out || !host) return SQRL_ERR_INVALID_ARG;

  sqrl_client_t *client = calloc(1, sizeof(sqrl_client_t));
  if (!client) return SQRL_ERR_MEMORY;

  client->fd = -1;
  client->request_timeout_ms = options ? options->request_timeout_ms : 30000;
  pthread_mutex_init(&client->write_mutex, NULL);
  pthread_mutex_init(&client->pending_mutex, NULL);
  pthread_mutex_init(&client->subs_mutex, NULL);

  /* Resolve hostname */
  struct addrinfo hints = {0}, *res = NULL;
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  char port_str[16];
  snprintf(port_str, sizeof(port_str), "%u", port);

  int gai_err = getaddrinfo(host, port_str, &hints, &res);
  if (gai_err != 0) {
    free(client);
    return SQRL_ERR_CONNECT;
  }

  /* Connect */
  client->fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (client->fd < 0) {
    freeaddrinfo(res);
    free(client);
    return SQRL_ERR_CONNECT;
  }

  /* Set TCP_NODELAY */
  int flag = 1;
  setsockopt(client->fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

  if (connect(client->fd, res->ai_addr, res->ai_addrlen) < 0) {
    freeaddrinfo(res);
    close(client->fd);
    free(client);
    return SQRL_ERR_CONNECT;
  }
  freeaddrinfo(res);

  /* Handshake */
  sqrl_error_t err = do_handshake(client, options);
  if (err != SQRL_OK) {
    close(client->fd);
    free(client->session_id);
    free(client);
    return err;
  }

  client->connected = true;

  /* Start reader thread */
  client->reader_running = true;
  if (pthread_create(&client->reader_thread, NULL, reader_thread_func, client) != 0) {
    close(client->fd);
    free(client->session_id);
    free(client);
    return SQRL_ERR_CONNECT;
  }

  *client_out = client;
  return SQRL_OK;
}

void sqrl_disconnect(sqrl_client_t *client) {
  if (!client) return;

  client->reader_running = false;
  client->connected = false;

  if (client->fd >= 0) {
    shutdown(client->fd, SHUT_RDWR);
    close(client->fd);
    client->fd = -1;
  }

  pthread_join(client->reader_thread, NULL);

  /* Free pending requests */
  pending_request_t *req = client->pending_requests;
  while (req) {
    pending_request_t *next = req->next;
    free(req->id);
    free(req->response);
    pthread_mutex_destroy(&req->mutex);
    pthread_cond_destroy(&req->cond);
    free(req);
    req = next;
  }

  /* Free subscriptions */
  subscription_entry_t *sub = client->subscriptions;
  while (sub) {
    subscription_entry_t *next = sub->next;
    free(sub->id);
    free(sub);
    sub = next;
  }

  pthread_mutex_destroy(&client->write_mutex);
  pthread_mutex_destroy(&client->pending_mutex);
  pthread_mutex_destroy(&client->subs_mutex);

  free(client->session_id);
  free(client);
}

const char *sqrl_session_id(const sqrl_client_t *client) {
  return client ? client->session_id : NULL;
}

bool sqrl_is_connected(const sqrl_client_t *client) {
  return client && client->connected;
}

sqrl_error_t sqrl_ping(sqrl_client_t *client) {
  if (!client || !client->connected) return SQRL_ERR_CLOSED;

  char *id = next_request_id(client);
  char json[256];
  snprintf(json, sizeof(json), "{\"type\":\"ping\",\"id\":\"%s\"}", id);

  char *response = NULL;
  sqrl_error_t err = send_request(client, json, id, &response);
  free(id);

  if (err == SQRL_OK) {
    char *resp_type = json_get_string(response, "type");
    if (!resp_type || strcmp(resp_type, "pong") != 0) {
      err = SQRL_ERR_SERVER;
    }
    free(resp_type);
    free(response);
  }

  return err;
}

sqrl_error_t sqrl_query(
  sqrl_client_t *client,
  const char *query,
  char **result_out
) {
  if (!client || !query || !result_out) return SQRL_ERR_INVALID_ARG;
  if (!client->connected) return SQRL_ERR_CLOSED;

  char *id = next_request_id(client);

  /* Build JSON - escape query string */
  size_t json_size = strlen(query) * 2 + 256;
  char *json = malloc(json_size);
  if (!json) {
    free(id);
    return SQRL_ERR_MEMORY;
  }

  /* Simple escaping for now */
  snprintf(json, json_size, "{\"type\":\"query\",\"id\":\"%s\",\"query\":\"%s\"}", id, query);

  char *response = NULL;
  sqrl_error_t err = send_request(client, json, id, &response);
  free(json);
  free(id);

  if (err != SQRL_OK) return err;

  char *resp_type = json_get_string(response, "type");
  if (resp_type && strcmp(resp_type, "error") == 0) {
    char *error = json_get_string(response, "error");
    free(resp_type);
    free(response);
    free(error);
    return SQRL_ERR_SERVER;
  }
  free(resp_type);

  /* Extract data field */
  char *data = json_get_object(response, "data");
  if (!data) {
    /* Try as string/array */
    const char *data_start = strstr(response, "\"data\":");
    if (data_start) {
      data_start += 7;
      while (*data_start == ' ') data_start++;
      data = strdup_safe(data_start);
      /* Remove trailing } */
      if (data) {
        size_t len = strlen(data);
        while (len > 0 && (data[len-1] == '}' || data[len-1] == '\n')) {
          data[--len] = '\0';
        }
      }
    }
  }

  free(response);
  *result_out = data ? data : strdup_safe("null");
  return SQRL_OK;
}

static sqrl_document_t *parse_document(const char *json) {
  sqrl_document_t *doc = calloc(1, sizeof(sqrl_document_t));
  if (!doc) return NULL;

  doc->id = json_get_string(json, "id");
  doc->collection = json_get_string(json, "collection");
  doc->data = json_get_object(json, "data");
  doc->created_at = json_get_string(json, "created_at");
  doc->updated_at = json_get_string(json, "updated_at");

  return doc;
}

sqrl_error_t sqrl_insert(
  sqrl_client_t *client,
  const char *collection,
  const char *data,
  sqrl_document_t **doc_out
) {
  if (!client || !collection || !data || !doc_out) return SQRL_ERR_INVALID_ARG;
  if (!client->connected) return SQRL_ERR_CLOSED;

  char *id = next_request_id(client);

  size_t json_size = strlen(collection) + strlen(data) + 256;
  char *json = malloc(json_size);
  if (!json) {
    free(id);
    return SQRL_ERR_MEMORY;
  }

  snprintf(json, json_size,
    "{\"type\":\"insert\",\"id\":\"%s\",\"collection\":\"%s\",\"data\":%s}",
    id, collection, data);

  char *response = NULL;
  sqrl_error_t err = send_request(client, json, id, &response);
  free(json);
  free(id);

  if (err != SQRL_OK) return err;

  char *resp_type = json_get_string(response, "type");
  if (resp_type && strcmp(resp_type, "error") == 0) {
    free(resp_type);
    free(response);
    return SQRL_ERR_SERVER;
  }
  free(resp_type);

  char *doc_json = json_get_object(response, "data");
  free(response);

  if (!doc_json) return SQRL_ERR_DECODE;

  *doc_out = parse_document(doc_json);
  free(doc_json);

  return *doc_out ? SQRL_OK : SQRL_ERR_DECODE;
}

sqrl_error_t sqrl_update(
  sqrl_client_t *client,
  const char *collection,
  const char *document_id,
  const char *data,
  sqrl_document_t **doc_out
) {
  if (!client || !collection || !document_id || !data || !doc_out)
    return SQRL_ERR_INVALID_ARG;
  if (!client->connected) return SQRL_ERR_CLOSED;

  char *id = next_request_id(client);

  size_t json_size = strlen(collection) + strlen(document_id) + strlen(data) + 256;
  char *json = malloc(json_size);
  if (!json) {
    free(id);
    return SQRL_ERR_MEMORY;
  }

  snprintf(json, json_size,
    "{\"type\":\"update\",\"id\":\"%s\",\"collection\":\"%s\",\"document_id\":\"%s\",\"data\":%s}",
    id, collection, document_id, data);

  char *response = NULL;
  sqrl_error_t err = send_request(client, json, id, &response);
  free(json);
  free(id);

  if (err != SQRL_OK) return err;

  char *resp_type = json_get_string(response, "type");
  if (resp_type && strcmp(resp_type, "error") == 0) {
    free(resp_type);
    free(response);
    return SQRL_ERR_SERVER;
  }
  free(resp_type);

  char *doc_json = json_get_object(response, "data");
  free(response);

  if (!doc_json) return SQRL_ERR_DECODE;

  *doc_out = parse_document(doc_json);
  free(doc_json);

  return *doc_out ? SQRL_OK : SQRL_ERR_DECODE;
}

sqrl_error_t sqrl_delete(
  sqrl_client_t *client,
  const char *collection,
  const char *document_id,
  sqrl_document_t **doc_out
) {
  if (!client || !collection || !document_id) return SQRL_ERR_INVALID_ARG;
  if (!client->connected) return SQRL_ERR_CLOSED;

  char *id = next_request_id(client);

  size_t json_size = strlen(collection) + strlen(document_id) + 256;
  char *json = malloc(json_size);
  if (!json) {
    free(id);
    return SQRL_ERR_MEMORY;
  }

  snprintf(json, json_size,
    "{\"type\":\"delete\",\"id\":\"%s\",\"collection\":\"%s\",\"document_id\":\"%s\"}",
    id, collection, document_id);

  char *response = NULL;
  sqrl_error_t err = send_request(client, json, id, &response);
  free(json);
  free(id);

  if (err != SQRL_OK) return err;

  char *resp_type = json_get_string(response, "type");
  if (resp_type && strcmp(resp_type, "error") == 0) {
    free(resp_type);
    free(response);
    return SQRL_ERR_SERVER;
  }
  free(resp_type);

  if (doc_out) {
    char *doc_json = json_get_object(response, "data");
    if (doc_json) {
      *doc_out = parse_document(doc_json);
      free(doc_json);
    } else {
      *doc_out = NULL;
    }
  }

  free(response);
  return SQRL_OK;
}

sqrl_error_t sqrl_list_collections(
  sqrl_client_t *client,
  char ***names_out,
  size_t *count_out
) {
  if (!client || !names_out || !count_out) return SQRL_ERR_INVALID_ARG;
  if (!client->connected) return SQRL_ERR_CLOSED;

  char *id = next_request_id(client);
  char json[256];
  snprintf(json, sizeof(json), "{\"type\":\"listcollections\",\"id\":\"%s\"}", id);

  char *response = NULL;
  sqrl_error_t err = send_request(client, json, id, &response);
  free(id);

  if (err != SQRL_OK) return err;

  char *resp_type = json_get_string(response, "type");
  if (resp_type && strcmp(resp_type, "error") == 0) {
    free(resp_type);
    free(response);
    return SQRL_ERR_SERVER;
  }
  free(resp_type);

  /* Parse array - simple implementation */
  *names_out = NULL;
  *count_out = 0;

  const char *data = strstr(response, "\"data\":");
  if (data) {
    data += 7;
    while (*data == ' ' || *data == '[') data++;

    /* Count entries */
    size_t count = 0;
    const char *p = data;
    while (*p && *p != ']') {
      if (*p == '"') {
        count++;
        p++;
        while (*p && *p != '"') p++;
      }
      if (*p) p++;
    }

    if (count > 0) {
      char **names = calloc(count, sizeof(char*));
      if (names) {
        size_t i = 0;
        p = data;
        while (*p && *p != ']' && i < count) {
          if (*p == '"') {
            p++;
            const char *end = strchr(p, '"');
            if (end) {
              names[i] = strndup(p, end - p);
              i++;
              p = end;
            }
          }
          if (*p) p++;
        }
        *names_out = names;
        *count_out = i;
      }
    }
  }

  free(response);
  return SQRL_OK;
}

sqrl_error_t sqrl_subscribe(
  sqrl_client_t *client,
  const char *query,
  sqrl_change_callback_t callback,
  void *user_data,
  sqrl_subscription_t **sub_out
) {
  if (!client || !query || !callback || !sub_out) return SQRL_ERR_INVALID_ARG;
  if (!client->connected) return SQRL_ERR_CLOSED;

  char *id = next_request_id(client);

  size_t json_size = strlen(query) * 2 + 256;
  char *json = malloc(json_size);
  if (!json) {
    free(id);
    return SQRL_ERR_MEMORY;
  }

  snprintf(json, json_size, "{\"type\":\"subscribe\",\"id\":\"%s\",\"query\":\"%s\"}", id, query);

  char *response = NULL;
  sqrl_error_t err = send_request(client, json, id, &response);
  free(json);

  if (err != SQRL_OK) {
    free(id);
    return err;
  }

  char *resp_type = json_get_string(response, "type");
  if (resp_type && strcmp(resp_type, "error") == 0) {
    free(resp_type);
    free(response);
    free(id);
    return SQRL_ERR_SERVER;
  }
  free(resp_type);
  free(response);

  /* Add subscription entry */
  subscription_entry_t *entry = calloc(1, sizeof(subscription_entry_t));
  if (!entry) {
    free(id);
    return SQRL_ERR_MEMORY;
  }
  entry->id = strdup_safe(id);
  entry->callback = callback;
  entry->user_data = user_data;

  pthread_mutex_lock(&client->subs_mutex);
  entry->next = client->subscriptions;
  client->subscriptions = entry;
  pthread_mutex_unlock(&client->subs_mutex);

  /* Create subscription handle */
  sqrl_subscription_t *sub = calloc(1, sizeof(sqrl_subscription_t));
  if (!sub) {
    free(id);
    return SQRL_ERR_MEMORY;
  }
  sub->id = id;
  sub->client = client;

  *sub_out = sub;
  return SQRL_OK;
}

sqrl_error_t sqrl_unsubscribe(sqrl_subscription_t *sub) {
  if (!sub || !sub->client) return SQRL_ERR_INVALID_ARG;

  sqrl_client_t *client = sub->client;

  /* Remove from subscriptions list */
  pthread_mutex_lock(&client->subs_mutex);
  subscription_entry_t *entry = client->subscriptions;
  subscription_entry_t *prev = NULL;
  while (entry) {
    if (strcmp(entry->id, sub->id) == 0) {
      if (prev) prev->next = entry->next;
      else client->subscriptions = entry->next;
      free(entry->id);
      free(entry);
      break;
    }
    prev = entry;
    entry = entry->next;
  }
  pthread_mutex_unlock(&client->subs_mutex);

  /* Send unsubscribe message */
  if (client->connected) {
    char json[256];
    snprintf(json, sizeof(json), "{\"type\":\"unsubscribe\",\"id\":\"%s\"}", sub->id);
    send_frame(client, json);
  }

  free(sub->id);
  free(sub);
  return SQRL_OK;
}

const char *sqrl_subscription_id(const sqrl_subscription_t *sub) {
  return sub ? sub->id : NULL;
}

void sqrl_document_free(sqrl_document_t *doc) {
  if (!doc) return;
  free(doc->id);
  free(doc->collection);
  free(doc->data);
  free(doc->created_at);
  free(doc->updated_at);
  free(doc);
}

void sqrl_change_event_free(sqrl_change_event_t *event) {
  if (!event) return;
  sqrl_document_free(event->document);
  sqrl_document_free(event->new_doc);
  free(event->old_data);
  free(event);
}

void sqrl_string_free(char *str) {
  free(str);
}

void sqrl_string_array_free(char **arr, size_t count) {
  if (!arr) return;
  for (size_t i = 0; i < count; i++) {
    free(arr[i]);
  }
  free(arr);
}
