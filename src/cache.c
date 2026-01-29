/**
 * SquirrelDB Cache Client Implementation
 *
 * Redis-compatible cache using RESP (REdis Serialization Protocol) over TCP.
 */

#include "squirreldb/cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>

#define CACHE_RECV_BUF_SIZE 4096
#define CACHE_SEND_BUF_SIZE 4096

/* Cache client structure */
struct sqrl_cache {
  int fd;
  char recv_buf[CACHE_RECV_BUF_SIZE];
  size_t recv_len;
  size_t recv_pos;
};

/* RESP types */
#define RESP_SIMPLE_STRING '+'
#define RESP_ERROR '-'
#define RESP_INTEGER ':'
#define RESP_BULK_STRING '$'
#define RESP_ARRAY '*'

/* ----------------------------------------------------------------------------
 * Network I/O
 * --------------------------------------------------------------------------*/

static ssize_t cache_send_all(int fd, const void* buf, size_t len) {
  const char* p = buf;
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
  return (ssize_t)len;
}

static int cache_fill_buffer(sqrl_cache_t* cache) {
  if (cache->recv_pos > 0 && cache->recv_len > cache->recv_pos) {
    memmove(cache->recv_buf, cache->recv_buf + cache->recv_pos,
            cache->recv_len - cache->recv_pos);
    cache->recv_len -= cache->recv_pos;
    cache->recv_pos = 0;
  } else if (cache->recv_pos > 0) {
    cache->recv_len = 0;
    cache->recv_pos = 0;
  }

  if (cache->recv_len >= CACHE_RECV_BUF_SIZE) {
    return 0;
  }

  ssize_t n = recv(cache->fd,
                   cache->recv_buf + cache->recv_len,
                   CACHE_RECV_BUF_SIZE - cache->recv_len, 0);
  if (n <= 0) {
    return -1;
  }

  cache->recv_len += n;
  return (int)n;
}

static char* cache_read_line(sqrl_cache_t* cache) {
  while (1) {
    for (size_t i = cache->recv_pos; i + 1 < cache->recv_len; i++) {
      if (cache->recv_buf[i] == '\r' && cache->recv_buf[i + 1] == '\n') {
        size_t line_len = i - cache->recv_pos;
        char* line = malloc(line_len + 1);
        if (!line) return NULL;

        memcpy(line, cache->recv_buf + cache->recv_pos, line_len);
        line[line_len] = '\0';
        cache->recv_pos = i + 2;
        return line;
      }
    }

    if (cache_fill_buffer(cache) < 0) {
      return NULL;
    }
  }
}

static char* cache_read_bytes(sqrl_cache_t* cache, size_t count) {
  char* data = malloc(count + 1);
  if (!data) return NULL;

  size_t read = 0;
  while (read < count) {
    size_t avail = cache->recv_len - cache->recv_pos;
    if (avail > 0) {
      size_t to_copy = (avail < count - read) ? avail : (count - read);
      memcpy(data + read, cache->recv_buf + cache->recv_pos, to_copy);
      cache->recv_pos += to_copy;
      read += to_copy;
    } else {
      if (cache_fill_buffer(cache) < 0) {
        free(data);
        return NULL;
      }
    }
  }

  data[count] = '\0';

  /* Consume trailing \r\n */
  char* crlf = cache_read_line(cache);
  free(crlf);

  return data;
}

/* ----------------------------------------------------------------------------
 * RESP Protocol Encoding
 * --------------------------------------------------------------------------*/

static int resp_encode_cmd(char* buf, size_t bufsize, int argc, ...) {
  va_list args;
  va_start(args, argc);

  int written = snprintf(buf, bufsize, "*%d\r\n", argc);
  if (written < 0 || (size_t)written >= bufsize) {
    va_end(args);
    return -1;
  }

  for (int i = 0; i < argc; i++) {
    const char* arg = va_arg(args, const char*);
    size_t arg_len = strlen(arg);

    int n = snprintf(buf + written, bufsize - written, "$%zu\r\n%s\r\n", arg_len, arg);
    if (n < 0 || (size_t)(written + n) >= bufsize) {
      va_end(args);
      return -1;
    }
    written += n;
  }

  va_end(args);
  return written;
}

static int resp_encode_cmd_with_int(char* buf, size_t bufsize, const char* cmd,
                                     const char* key, long value) {
  char val_str[32];
  snprintf(val_str, sizeof(val_str), "%ld", value);

  return resp_encode_cmd(buf, bufsize, 3, cmd, key, val_str);
}

/* ----------------------------------------------------------------------------
 * RESP Protocol Decoding
 * --------------------------------------------------------------------------*/

typedef struct {
  int type;
  union {
    char* str;
    long integer;
    struct {
      char** elements;
      int count;
    } array;
  } data;
  int is_error;
  int is_null;
} resp_reply_t;

static void resp_reply_free(resp_reply_t* reply) {
  if (!reply) return;

  if (reply->type == RESP_SIMPLE_STRING || reply->type == RESP_ERROR ||
      reply->type == RESP_BULK_STRING) {
    free(reply->data.str);
  } else if (reply->type == RESP_ARRAY && reply->data.array.elements) {
    for (int i = 0; i < reply->data.array.count; i++) {
      free(reply->data.array.elements[i]);
    }
    free(reply->data.array.elements);
  }
  free(reply);
}

static resp_reply_t* resp_read_reply(sqrl_cache_t* cache);

static resp_reply_t* resp_parse_reply(sqrl_cache_t* cache, char* line) {
  if (!line || strlen(line) < 1) {
    free(line);
    return NULL;
  }

  resp_reply_t* reply = calloc(1, sizeof(resp_reply_t));
  if (!reply) {
    free(line);
    return NULL;
  }

  char type = line[0];
  char* content = line + 1;
  reply->type = type;

  switch (type) {
    case RESP_SIMPLE_STRING:
      reply->data.str = strdup(content);
      free(line);
      break;

    case RESP_ERROR:
      reply->data.str = strdup(content);
      reply->is_error = 1;
      free(line);
      break;

    case RESP_INTEGER:
      reply->data.integer = strtol(content, NULL, 10);
      free(line);
      break;

    case RESP_BULK_STRING: {
      int len = atoi(content);
      free(line);

      if (len < 0) {
        reply->is_null = 1;
        reply->data.str = NULL;
      } else {
        reply->data.str = cache_read_bytes(cache, len);
        if (!reply->data.str) {
          free(reply);
          return NULL;
        }
      }
      break;
    }

    case RESP_ARRAY: {
      int count = atoi(content);
      free(line);

      if (count < 0) {
        reply->is_null = 1;
        reply->data.array.elements = NULL;
        reply->data.array.count = 0;
      } else if (count == 0) {
        reply->data.array.elements = NULL;
        reply->data.array.count = 0;
      } else {
        reply->data.array.elements = calloc(count, sizeof(char*));
        reply->data.array.count = count;

        if (!reply->data.array.elements) {
          free(reply);
          return NULL;
        }

        for (int i = 0; i < count; i++) {
          resp_reply_t* elem = resp_read_reply(cache);
          if (!elem) {
            for (int j = 0; j < i; j++) {
              free(reply->data.array.elements[j]);
            }
            free(reply->data.array.elements);
            free(reply);
            return NULL;
          }

          if (elem->type == RESP_BULK_STRING || elem->type == RESP_SIMPLE_STRING) {
            reply->data.array.elements[i] = elem->data.str;
            elem->data.str = NULL;
          } else if (elem->type == RESP_INTEGER) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%ld", elem->data.integer);
            reply->data.array.elements[i] = strdup(buf);
          } else {
            reply->data.array.elements[i] = NULL;
          }

          resp_reply_free(elem);
        }
      }
      break;
    }

    default:
      free(line);
      free(reply);
      return NULL;
  }

  return reply;
}

static resp_reply_t* resp_read_reply(sqrl_cache_t* cache) {
  char* line = cache_read_line(cache);
  if (!line) return NULL;

  return resp_parse_reply(cache, line);
}

/* ----------------------------------------------------------------------------
 * Command Helpers
 * --------------------------------------------------------------------------*/

static resp_reply_t* cache_exec_cmd(sqrl_cache_t* cache, const char* cmd, size_t cmd_len) {
  if (!cache || cache->fd < 0) return NULL;

  if (cache_send_all(cache->fd, cmd, cmd_len) < 0) {
    return NULL;
  }

  return resp_read_reply(cache);
}

/* ----------------------------------------------------------------------------
 * Public API Implementation
 * --------------------------------------------------------------------------*/

sqrl_cache_t* sqrl_cache_connect(const char* host, int port) {
  if (!host || port <= 0) return NULL;

  sqrl_cache_t* cache = calloc(1, sizeof(sqrl_cache_t));
  if (!cache) return NULL;

  cache->fd = -1;

  /* Resolve hostname */
  struct addrinfo hints = {0}, *res = NULL;
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  char port_str[16];
  snprintf(port_str, sizeof(port_str), "%d", port);

  if (getaddrinfo(host, port_str, &hints, &res) != 0) {
    free(cache);
    return NULL;
  }

  /* Create socket and connect */
  cache->fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (cache->fd < 0) {
    freeaddrinfo(res);
    free(cache);
    return NULL;
  }

  /* Set TCP_NODELAY for low latency */
  int flag = 1;
  setsockopt(cache->fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

  if (connect(cache->fd, res->ai_addr, res->ai_addrlen) < 0) {
    freeaddrinfo(res);
    close(cache->fd);
    free(cache);
    return NULL;
  }

  freeaddrinfo(res);
  return cache;
}

void sqrl_cache_close(sqrl_cache_t* cache) {
  if (!cache) return;

  if (cache->fd >= 0) {
    close(cache->fd);
  }
  free(cache);
}

char* sqrl_cache_get(sqrl_cache_t* cache, const char* key) {
  if (!cache || !key) return NULL;

  char buf[CACHE_SEND_BUF_SIZE];
  int len = resp_encode_cmd(buf, sizeof(buf), 2, "GET", key);
  if (len < 0) return NULL;

  resp_reply_t* reply = cache_exec_cmd(cache, buf, len);
  if (!reply) return NULL;

  char* result = NULL;
  if (reply->type == RESP_BULK_STRING && !reply->is_null) {
    result = reply->data.str;
    reply->data.str = NULL;
  }

  resp_reply_free(reply);
  return result;
}

int sqrl_cache_set(sqrl_cache_t* cache, const char* key, const char* value, int ttl) {
  if (!cache || !key || !value) return -1;

  char buf[CACHE_SEND_BUF_SIZE];
  int len;

  if (ttl > 0) {
    char ttl_str[32];
    snprintf(ttl_str, sizeof(ttl_str), "%d", ttl);
    len = resp_encode_cmd(buf, sizeof(buf), 5, "SET", key, value, "EX", ttl_str);
  } else {
    len = resp_encode_cmd(buf, sizeof(buf), 3, "SET", key, value);
  }

  if (len < 0) return -1;

  resp_reply_t* reply = cache_exec_cmd(cache, buf, len);
  if (!reply) return -1;

  int result = (reply->type == RESP_SIMPLE_STRING &&
                reply->data.str && strcmp(reply->data.str, "OK") == 0) ? 0 : -1;

  resp_reply_free(reply);
  return result;
}

int sqrl_cache_del(sqrl_cache_t* cache, const char* key) {
  if (!cache || !key) return -1;

  char buf[CACHE_SEND_BUF_SIZE];
  int len = resp_encode_cmd(buf, sizeof(buf), 2, "DEL", key);
  if (len < 0) return -1;

  resp_reply_t* reply = cache_exec_cmd(cache, buf, len);
  if (!reply) return -1;

  int result = (reply->type == RESP_INTEGER) ? (int)reply->data.integer : -1;

  resp_reply_free(reply);
  return result;
}

int sqrl_cache_exists(sqrl_cache_t* cache, const char* key) {
  if (!cache || !key) return -1;

  char buf[CACHE_SEND_BUF_SIZE];
  int len = resp_encode_cmd(buf, sizeof(buf), 2, "EXISTS", key);
  if (len < 0) return -1;

  resp_reply_t* reply = cache_exec_cmd(cache, buf, len);
  if (!reply) return -1;

  int result = (reply->type == RESP_INTEGER) ? (int)reply->data.integer : -1;

  resp_reply_free(reply);
  return result;
}

int sqrl_cache_expire(sqrl_cache_t* cache, const char* key, int seconds) {
  if (!cache || !key) return -1;

  char buf[CACHE_SEND_BUF_SIZE];
  int len = resp_encode_cmd_with_int(buf, sizeof(buf), "EXPIRE", key, seconds);
  if (len < 0) return -1;

  resp_reply_t* reply = cache_exec_cmd(cache, buf, len);
  if (!reply) return -1;

  int result = (reply->type == RESP_INTEGER) ? (int)reply->data.integer : -1;

  resp_reply_free(reply);
  return result;
}

long sqrl_cache_ttl(sqrl_cache_t* cache, const char* key) {
  if (!cache || !key) return -3;

  char buf[CACHE_SEND_BUF_SIZE];
  int len = resp_encode_cmd(buf, sizeof(buf), 2, "TTL", key);
  if (len < 0) return -3;

  resp_reply_t* reply = cache_exec_cmd(cache, buf, len);
  if (!reply) return -3;

  long result = (reply->type == RESP_INTEGER) ? reply->data.integer : -3;

  resp_reply_free(reply);
  return result;
}

int sqrl_cache_persist(sqrl_cache_t* cache, const char* key) {
  if (!cache || !key) return -1;

  char buf[CACHE_SEND_BUF_SIZE];
  int len = resp_encode_cmd(buf, sizeof(buf), 2, "PERSIST", key);
  if (len < 0) return -1;

  resp_reply_t* reply = cache_exec_cmd(cache, buf, len);
  if (!reply) return -1;

  int result = (reply->type == RESP_INTEGER) ? (int)reply->data.integer : -1;

  resp_reply_free(reply);
  return result;
}

long sqrl_cache_incr(sqrl_cache_t* cache, const char* key) {
  if (!cache || !key) return LONG_MIN;

  char buf[CACHE_SEND_BUF_SIZE];
  int len = resp_encode_cmd(buf, sizeof(buf), 2, "INCR", key);
  if (len < 0) return LONG_MIN;

  resp_reply_t* reply = cache_exec_cmd(cache, buf, len);
  if (!reply) return LONG_MIN;

  long result = (reply->type == RESP_INTEGER) ? reply->data.integer : LONG_MIN;

  resp_reply_free(reply);
  return result;
}

long sqrl_cache_decr(sqrl_cache_t* cache, const char* key) {
  if (!cache || !key) return LONG_MIN;

  char buf[CACHE_SEND_BUF_SIZE];
  int len = resp_encode_cmd(buf, sizeof(buf), 2, "DECR", key);
  if (len < 0) return LONG_MIN;

  resp_reply_t* reply = cache_exec_cmd(cache, buf, len);
  if (!reply) return LONG_MIN;

  long result = (reply->type == RESP_INTEGER) ? reply->data.integer : LONG_MIN;

  resp_reply_free(reply);
  return result;
}

long sqrl_cache_incrby(sqrl_cache_t* cache, const char* key, long amount) {
  if (!cache || !key) return LONG_MIN;

  char buf[CACHE_SEND_BUF_SIZE];
  int len = resp_encode_cmd_with_int(buf, sizeof(buf), "INCRBY", key, amount);
  if (len < 0) return LONG_MIN;

  resp_reply_t* reply = cache_exec_cmd(cache, buf, len);
  if (!reply) return LONG_MIN;

  long result = (reply->type == RESP_INTEGER) ? reply->data.integer : LONG_MIN;

  resp_reply_free(reply);
  return result;
}

char** sqrl_cache_keys(sqrl_cache_t* cache, const char* pattern, int* count) {
  if (!cache || !pattern || !count) return NULL;

  *count = 0;

  char buf[CACHE_SEND_BUF_SIZE];
  int len = resp_encode_cmd(buf, sizeof(buf), 2, "KEYS", pattern);
  if (len < 0) return NULL;

  resp_reply_t* reply = cache_exec_cmd(cache, buf, len);
  if (!reply) return NULL;

  char** result = NULL;
  if (reply->type == RESP_ARRAY && reply->data.array.count > 0) {
    result = malloc(sizeof(char*) * reply->data.array.count);
    if (result) {
      for (int i = 0; i < reply->data.array.count; i++) {
        result[i] = reply->data.array.elements[i];
        reply->data.array.elements[i] = NULL;
      }
      *count = reply->data.array.count;
    }
  }

  resp_reply_free(reply);
  return result;
}

int sqrl_cache_dbsize(sqrl_cache_t* cache) {
  if (!cache) return -1;

  char buf[CACHE_SEND_BUF_SIZE];
  int len = resp_encode_cmd(buf, sizeof(buf), 1, "DBSIZE");
  if (len < 0) return -1;

  resp_reply_t* reply = cache_exec_cmd(cache, buf, len);
  if (!reply) return -1;

  int result = (reply->type == RESP_INTEGER) ? (int)reply->data.integer : -1;

  resp_reply_free(reply);
  return result;
}

int sqrl_cache_flush(sqrl_cache_t* cache) {
  if (!cache) return -1;

  char buf[CACHE_SEND_BUF_SIZE];
  int len = resp_encode_cmd(buf, sizeof(buf), 1, "FLUSHDB");
  if (len < 0) return -1;

  resp_reply_t* reply = cache_exec_cmd(cache, buf, len);
  if (!reply) return -1;

  int result = (reply->type == RESP_SIMPLE_STRING &&
                reply->data.str && strcmp(reply->data.str, "OK") == 0) ? 0 : -1;

  resp_reply_free(reply);
  return result;
}

int sqrl_cache_ping(sqrl_cache_t* cache) {
  if (!cache) return -1;

  char buf[CACHE_SEND_BUF_SIZE];
  int len = resp_encode_cmd(buf, sizeof(buf), 1, "PING");
  if (len < 0) return -1;

  resp_reply_t* reply = cache_exec_cmd(cache, buf, len);
  if (!reply) return -1;

  int result = (reply->type == RESP_SIMPLE_STRING &&
                reply->data.str && strcmp(reply->data.str, "PONG") == 0) ? 0 : -1;

  resp_reply_free(reply);
  return result;
}

void sqrl_cache_free_string(char* str) {
  free(str);
}

void sqrl_cache_free_strings(char** strs, int count) {
  if (!strs) return;

  for (int i = 0; i < count; i++) {
    free(strs[i]);
  }
  free(strs);
}
