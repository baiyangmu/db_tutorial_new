#include "../include/util.h"
#include "../include/btree.h"
#include "../include/schema.h"
#include "../include/sql_executor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>
#include <stdarg.h>

/* Global sort context */
SortContext g_sort_ctx;

/* Parse integer from string */
int parse_int(const char* s, int* out) {
  char* end = NULL;
  long v = strtol(s, &end, 10);
  if (end == s || *end != '\0') {
    return -1;
  }
  if (v < INT32_MIN || v > INT32_MAX) {
    return -1;
  }
  *out = (int)v;
  return 0;
}

/* Parse 64-bit integer from string */
int parse_int64(const char* s, int64_t* out) {
  char* end = NULL;
  long long v = strtoll(s, &end, 10);
  if (end == s || *end != '\0') {
    return -1;
  }
  *out = (int64_t)v;
  return 0;
}

/* Get integer value from row */
int row_get_int(Table* t, const void* row, int col_idx) {
  uint32_t off = schema_col_offset(&t->active_schema, col_idx);
  int v;
  memcpy(&v, (const uint8_t*)row + off, 4);
  return v;
}

/* Get timestamp value from row */
int64_t row_get_timestamp(Table* t, const void* row, int col_idx) {
  uint32_t off = schema_col_offset(&t->active_schema, col_idx);
  int64_t v;
  memcpy(&v, (const uint8_t*)row + off, 8);
  return v;
}

/* Get string value from row */
void row_get_string(Table* t, const void* row, int col_idx, char* out, size_t cap) {
  uint32_t off = schema_col_offset(&t->active_schema, col_idx);
  size_t sz = t->active_schema.columns[col_idx].size;
  size_t n = (sz < cap - 1) ? sz : (cap - 1);
  memcpy(out, (const uint8_t*)row + off, n);
  out[n] = 0;
  while (n > 0 && out[n - 1] == 0) {
    n--;
  }
  out[n] = 0;
}

/* Check if row matches WHERE clause */
bool row_matches_where(Table* t, const void* row, const Statement* st) {
  if (!st->has_where) {
    return true;
  }
  if (st->where_is_string) {
    char buf[512];
    row_get_string(t, row, st->where_col_index, buf, sizeof(buf));
    return strcmp(buf, st->where_str) == 0;
  } else {
    int v = row_get_int(t, row, st->where_col_index);
    return v == st->where_int;
  }
}

/* Print row with all columns */
void print_row_dynamic(Table* t, const void* src) {
  const uint8_t* p = (const uint8_t*)src;
  printf("(");
  for (uint32_t i = 0; i < t->active_schema.num_columns; i++) {
    const ColumnDef* c = &t->active_schema.columns[i];
    if (i) {
      printf(", ");
    }

    if (c->type == COL_TYPE_INT) {
      int v;
      memcpy(&v, p, 4);
      p += 4;
      printf("%d", v);
    } else if (c->type == COL_TYPE_TIMESTAMP) {
      int64_t tv;
      memcpy(&tv, p, 8);
      p += 8;
      printf("%lld", (long long)tv);
    } else {
      char buf[512];
      size_t m = c->size < sizeof(buf) - 1 ? c->size : sizeof(buf) - 1;
      memcpy(buf, p, m);
      buf[m] = 0;
      size_t r = m;
      while (r > 0 && buf[r - 1] == 0) {
        r--;
      }
      buf[r] = 0;
      printf("%s", buf);
      p += c->size;
    }
  }
  printf(")\n");
}

/* Print row with selected columns */
void print_row_projected(Table* t, const void* row, const int* idxs, uint32_t n) {
  if (n == 0) {
    print_row_dynamic(t, row);
    return;
  }
  printf("(");
  for (uint32_t i = 0; i < n; i++) {
    if (i) {
      printf(",");
    }
    const ColumnDef* c = &t->active_schema.columns[idxs[i]];
    if (c->type == COL_TYPE_INT) {
      int v = row_get_int(t, row, idxs[i]);
      printf("%d", v);
    } else if (c->type == COL_TYPE_TIMESTAMP) {
      int64_t timestamp = row_get_timestamp(t, row, idxs[i]);
      printf("%lld", (long long)timestamp);
    } else {
      char s[512];
      row_get_string(t, row, idxs[i], s, sizeof(s));
      printf("%s", s);
    }
  }
  printf(")\n");
}

/* Comparator for sorting rows */
int rowref_comparator(const void* a, const void* b) {
  const RowRef* ra = (const RowRef*)a;
  const RowRef* rb = (const RowRef*)b;
  Table* t = g_sort_ctx.table;
  int idx = g_sort_ctx.col_idx;
  if (t->active_schema.columns[idx].type == COL_TYPE_INT) {
    int va = row_get_int(t, ra->row, idx);
    int vb = row_get_int(t, rb->row, idx);
    if (va < vb) {
      return g_sort_ctx.desc ? 1 : -1;
    }
    if (va > vb) {
      return g_sort_ctx.desc ? -1 : 1;
    }
    return 0;
  } else {
    char sa[512], sb[512];
    row_get_string(t, ra->row, idx, sa, sizeof(sa));
    row_get_string(t, rb->row, idx, sb, sizeof(sb));
    int cmp = strcmp(sa, sb);
    return g_sort_ctx.desc ? -cmp : cmp;
  }
}

/* String buffer operations */
void sb_init(StrBuf* s) {
  s->cap = 1024;
  s->len = 0;
  s->buf = malloc(s->cap);
  if (s->buf) {
    s->buf[0] = '\0';
  }
}

void sb_free(StrBuf* s) {
  free(s->buf);
  s->buf = NULL;
  s->cap = s->len = 0;
}

void sb_ensure(StrBuf* s, size_t need) {
  if (s->len + need + 1 > s->cap) {
    size_t nc = s->cap ? s->cap * 2 : 1024;
    while (nc < s->len + need + 1) {
      nc *= 2;
    }
    s->buf = realloc(s->buf, nc);
    s->cap = nc;
  }
}

void sb_append(StrBuf* s, const char* t) {
  size_t n = strlen(t);
  sb_ensure(s, n);
  memcpy(s->buf + s->len, t, n);
  s->len += n;
  s->buf[s->len] = '\0';
}

void sb_appendf(StrBuf* s, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  char tmp[512];
  int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
  va_end(ap);
  if (n < 0) {
    return;
  }
  if ((size_t)n < sizeof(tmp)) {
    sb_append(s, tmp);
    return;
  }
  char* big = malloc(n + 1);
  va_start(ap, fmt);
  vsnprintf(big, n + 1, fmt, ap);
  va_end(ap);
  sb_append(s, big);
  free(big);
}

void json_escape_append(StrBuf* sb, const char* src) {
  sb_append(sb, "\"");
  for (const unsigned char* p = (const unsigned char*)src; *p; ++p) {
    if (*p == '\\' || *p == '"') {
      sb_appendf(sb, "\\%c", *p);
    } else if (*p >= 0 && *p < 0x20) {
      sb_appendf(sb, "\\u%04x", *p);
    } else {
      sb_appendf(sb, "%c", *p);
    }
  }
  sb_append(sb, "\"");
}

