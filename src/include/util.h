#ifndef MYDB_UTIL_H
#define MYDB_UTIL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Common constants */
#define MYDB_PAGE_SIZE 4096
#define INVALID_PAGE_NUM UINT32_MAX
#define TABLE_MAX_PAGES 400

/* Forward declarations */
typedef struct Table Table;
typedef struct Statement Statement;

/* Parse utilities */
int parse_int(const char* s, int* out);
int parse_int64(const char* s, int64_t* out);

/* Row printing utilities */
void print_row_dynamic(Table* t, const void* src);
void print_row_projected(Table* t, const void* row, const int* idxs, uint32_t n);

/* Row data access helpers */
int row_get_int(Table* t, const void* row, int col_idx);
int64_t row_get_timestamp(Table* t, const void* row, int col_idx);
void row_get_string(Table* t, const void* row, int col_idx, char* out, size_t cap);

/* Row matching */
bool row_matches_where(Table* t, const void* row, const Statement* st);

/* Sorting support */
typedef struct {
  const void* row;
} RowRef;

typedef struct {
  Table* table;
  int col_idx;
  int desc;
} SortContext;

extern SortContext g_sort_ctx;

int rowref_comparator(const void* a, const void* b);

/* String buffer for JSON output */
typedef struct {
  char* buf;
  size_t len;
  size_t cap;
} StrBuf;

void sb_init(StrBuf* s);
void sb_free(StrBuf* s);
void sb_ensure(StrBuf* s, size_t need);
void sb_append(StrBuf* s, const char* t);
void sb_appendf(StrBuf* s, const char* fmt, ...);
void json_escape_append(StrBuf* sb, const char* src);

#endif /* MYDB_UTIL_H */

