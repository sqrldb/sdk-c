/**
 * SquirrelDB Query Builder Implementation
 */

#include "../include/squirreldb/query.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FILTERS 32
#define MAX_SORTS 8
#define BUFFER_SIZE 4096

typedef struct {
    char field[128];
    char op[16];
    char value[512];
} filter_entry_t;

typedef struct {
    char field[128];
    sqrl_sort_dir_t direction;
} sort_entry_t;

struct sqrl_query {
    char table_name[256];
    filter_entry_t filters[MAX_FILTERS];
    size_t filter_count;
    sort_entry_t sorts[MAX_SORTS];
    size_t sort_count;
    size_t limit_value;
    size_t skip_value;
    bool has_limit;
    bool has_skip;
    bool is_changes;
};

static char *escape_json_string(const char *str, char *buf, size_t buf_size) {
    size_t i = 0;
    size_t j = 0;
    while (str[i] && j < buf_size - 2) {
        if (str[i] == '"' || str[i] == '\\') {
            buf[j++] = '\\';
        }
        buf[j++] = str[i++];
    }
    buf[j] = '\0';
    return buf;
}

sqrl_query_t* sqrl_table(const char* table_name) {
    if (!table_name) return NULL;

    sqrl_query_t *q = calloc(1, sizeof(sqrl_query_t));
    if (!q) return NULL;

    strncpy(q->table_name, table_name, sizeof(q->table_name) - 1);
    return q;
}

void sqrl_query_free(sqrl_query_t* query) {
    free(query);
}

static sqrl_query_t* add_filter(sqrl_query_t* query, const char* field, const char* op, const char* value) {
    if (!query || query->filter_count >= MAX_FILTERS) return query;

    filter_entry_t *f = &query->filters[query->filter_count++];
    strncpy(f->field, field, sizeof(f->field) - 1);
    strncpy(f->op, op, sizeof(f->op) - 1);
    strncpy(f->value, value, sizeof(f->value) - 1);

    return query;
}

sqrl_query_t* sqrl_find_eq_str(sqrl_query_t* query, const char* field, const char* value) {
    char escaped[512];
    char buf[520];
    escape_json_string(value, escaped, sizeof(escaped));
    snprintf(buf, sizeof(buf), "\"%s\"", escaped);
    return add_filter(query, field, "$eq", buf);
}

sqrl_query_t* sqrl_find_eq_int(sqrl_query_t* query, const char* field, long value) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%ld", value);
    return add_filter(query, field, "$eq", buf);
}

sqrl_query_t* sqrl_find_eq_double(sqrl_query_t* query, const char* field, double value) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%g", value);
    return add_filter(query, field, "$eq", buf);
}

sqrl_query_t* sqrl_find_eq_bool(sqrl_query_t* query, const char* field, bool value) {
    return add_filter(query, field, "$eq", value ? "true" : "false");
}

sqrl_query_t* sqrl_find_ne_str(sqrl_query_t* query, const char* field, const char* value) {
    char escaped[512];
    char buf[520];
    escape_json_string(value, escaped, sizeof(escaped));
    snprintf(buf, sizeof(buf), "\"%s\"", escaped);
    return add_filter(query, field, "$ne", buf);
}

sqrl_query_t* sqrl_find_ne_int(sqrl_query_t* query, const char* field, long value) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%ld", value);
    return add_filter(query, field, "$ne", buf);
}

sqrl_query_t* sqrl_find_gt(sqrl_query_t* query, const char* field, double value) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%g", value);
    return add_filter(query, field, "$gt", buf);
}

sqrl_query_t* sqrl_find_gte(sqrl_query_t* query, const char* field, double value) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%g", value);
    return add_filter(query, field, "$gte", buf);
}

sqrl_query_t* sqrl_find_lt(sqrl_query_t* query, const char* field, double value) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%g", value);
    return add_filter(query, field, "$lt", buf);
}

sqrl_query_t* sqrl_find_lte(sqrl_query_t* query, const char* field, double value) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%g", value);
    return add_filter(query, field, "$lte", buf);
}

sqrl_query_t* sqrl_find_contains(sqrl_query_t* query, const char* field, const char* value) {
    char escaped[512];
    char buf[520];
    escape_json_string(value, escaped, sizeof(escaped));
    snprintf(buf, sizeof(buf), "\"%s\"", escaped);
    return add_filter(query, field, "$contains", buf);
}

sqrl_query_t* sqrl_find_starts_with(sqrl_query_t* query, const char* field, const char* value) {
    char escaped[512];
    char buf[520];
    escape_json_string(value, escaped, sizeof(escaped));
    snprintf(buf, sizeof(buf), "\"%s\"", escaped);
    return add_filter(query, field, "$startsWith", buf);
}

sqrl_query_t* sqrl_find_ends_with(sqrl_query_t* query, const char* field, const char* value) {
    char escaped[512];
    char buf[520];
    escape_json_string(value, escaped, sizeof(escaped));
    snprintf(buf, sizeof(buf), "\"%s\"", escaped);
    return add_filter(query, field, "$endsWith", buf);
}

sqrl_query_t* sqrl_find_exists(sqrl_query_t* query, const char* field, bool exists) {
    return add_filter(query, field, "$exists", exists ? "true" : "false");
}

sqrl_query_t* sqrl_sort(sqrl_query_t* query, const char* field, sqrl_sort_dir_t direction) {
    if (!query || query->sort_count >= MAX_SORTS) return query;

    sort_entry_t *s = &query->sorts[query->sort_count++];
    strncpy(s->field, field, sizeof(s->field) - 1);
    s->direction = direction;

    return query;
}

sqrl_query_t* sqrl_limit(sqrl_query_t* query, size_t n) {
    if (query) {
        query->limit_value = n;
        query->has_limit = true;
    }
    return query;
}

sqrl_query_t* sqrl_skip(sqrl_query_t* query, size_t n) {
    if (query) {
        query->skip_value = n;
        query->has_skip = true;
    }
    return query;
}

sqrl_query_t* sqrl_changes(sqrl_query_t* query) {
    if (query) {
        query->is_changes = true;
    }
    return query;
}

char* sqrl_query_compile(sqrl_query_t* query) {
    if (!query) return NULL;

    char *buf = malloc(BUFFER_SIZE);
    if (!buf) return NULL;

    char *p = buf;
    size_t remaining = BUFFER_SIZE;
    int n;

    n = snprintf(p, remaining, "db.table(\"%s\")", query->table_name);
    p += n;
    remaining -= n;

    /* Compile filters to JS */
    if (query->filter_count > 0) {
        n = snprintf(p, remaining, ".filter(doc => ");
        p += n;
        remaining -= n;

        for (size_t i = 0; i < query->filter_count; i++) {
            filter_entry_t *f = &query->filters[i];

            if (i > 0) {
                n = snprintf(p, remaining, " && ");
                p += n;
                remaining -= n;
            }

            /* Convert operators to JS */
            if (strcmp(f->op, "$eq") == 0) {
                n = snprintf(p, remaining, "doc.%s === %s", f->field, f->value);
            } else if (strcmp(f->op, "$ne") == 0) {
                n = snprintf(p, remaining, "doc.%s !== %s", f->field, f->value);
            } else if (strcmp(f->op, "$gt") == 0) {
                n = snprintf(p, remaining, "doc.%s > %s", f->field, f->value);
            } else if (strcmp(f->op, "$gte") == 0) {
                n = snprintf(p, remaining, "doc.%s >= %s", f->field, f->value);
            } else if (strcmp(f->op, "$lt") == 0) {
                n = snprintf(p, remaining, "doc.%s < %s", f->field, f->value);
            } else if (strcmp(f->op, "$lte") == 0) {
                n = snprintf(p, remaining, "doc.%s <= %s", f->field, f->value);
            } else if (strcmp(f->op, "$contains") == 0) {
                n = snprintf(p, remaining, "doc.%s.includes(%s)", f->field, f->value);
            } else if (strcmp(f->op, "$startsWith") == 0) {
                n = snprintf(p, remaining, "doc.%s.startsWith(%s)", f->field, f->value);
            } else if (strcmp(f->op, "$endsWith") == 0) {
                n = snprintf(p, remaining, "doc.%s.endsWith(%s)", f->field, f->value);
            } else if (strcmp(f->op, "$exists") == 0) {
                if (strcmp(f->value, "true") == 0) {
                    n = snprintf(p, remaining, "doc.%s !== undefined", f->field);
                } else {
                    n = snprintf(p, remaining, "doc.%s === undefined", f->field);
                }
            } else {
                n = snprintf(p, remaining, "true");
            }
            p += n;
            remaining -= n;
        }

        n = snprintf(p, remaining, ")");
        p += n;
        remaining -= n;
    }

    /* Sorts */
    for (size_t i = 0; i < query->sort_count; i++) {
        sort_entry_t *s = &query->sorts[i];
        if (s->direction == SQRL_DESC) {
            n = snprintf(p, remaining, ".orderBy(\"%s\", \"desc\")", s->field);
        } else {
            n = snprintf(p, remaining, ".orderBy(\"%s\")", s->field);
        }
        p += n;
        remaining -= n;
    }

    if (query->has_limit) {
        n = snprintf(p, remaining, ".limit(%zu)", query->limit_value);
        p += n;
        remaining -= n;
    }

    if (query->has_skip) {
        n = snprintf(p, remaining, ".skip(%zu)", query->skip_value);
        p += n;
        remaining -= n;
    }

    if (query->is_changes) {
        snprintf(p, remaining, ".changes()");
    } else {
        snprintf(p, remaining, ".run()");
    }

    return buf;
}

char* sqrl_query_compile_structured(sqrl_query_t* query) {
    if (!query) return NULL;

    char *buf = malloc(BUFFER_SIZE);
    if (!buf) return NULL;

    char *p = buf;
    size_t remaining = BUFFER_SIZE;
    int n;

    n = snprintf(p, remaining, "{\"table\":\"%s\"", query->table_name);
    p += n;
    remaining -= n;

    /* Compile filters to structured format */
    if (query->filter_count > 0) {
        n = snprintf(p, remaining, ",\"filter\":{");
        p += n;
        remaining -= n;

        for (size_t i = 0; i < query->filter_count; i++) {
            filter_entry_t *f = &query->filters[i];

            if (i > 0) {
                n = snprintf(p, remaining, ",");
                p += n;
                remaining -= n;
            }

            n = snprintf(p, remaining, "\"%s\":{\"%s\":%s}", f->field, f->op, f->value);
            p += n;
            remaining -= n;
        }

        n = snprintf(p, remaining, "}");
        p += n;
        remaining -= n;
    }

    /* Sorts */
    if (query->sort_count > 0) {
        n = snprintf(p, remaining, ",\"sort\":[");
        p += n;
        remaining -= n;

        for (size_t i = 0; i < query->sort_count; i++) {
            sort_entry_t *s = &query->sorts[i];
            if (i > 0) {
                n = snprintf(p, remaining, ",");
                p += n;
                remaining -= n;
            }
            n = snprintf(p, remaining, "{\"field\":\"%s\",\"direction\":\"%s\"}",
                         s->field, s->direction == SQRL_DESC ? "desc" : "asc");
            p += n;
            remaining -= n;
        }

        n = snprintf(p, remaining, "]");
        p += n;
        remaining -= n;
    }

    if (query->has_limit) {
        n = snprintf(p, remaining, ",\"limit\":%zu", query->limit_value);
        p += n;
        remaining -= n;
    }

    if (query->has_skip) {
        n = snprintf(p, remaining, ",\"skip\":%zu", query->skip_value);
        p += n;
        remaining -= n;
    }

    if (query->is_changes) {
        n = snprintf(p, remaining, ",\"changes\":{\"includeInitial\":false}");
        p += n;
        remaining -= n;
    }

    snprintf(p, remaining, "}");

    return buf;
}
