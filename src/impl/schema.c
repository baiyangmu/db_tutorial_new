#include "../include/schema.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Global schema storage */
TableSchema g_table_schemas[MAX_TABLES];
uint32_t g_num_tables = 0;

/* Legacy row size constants */
const uint32_t ID_SIZE = sizeof(uint32_t);
const uint32_t USERNAME_SIZE = COLUMN_USERNAME_SIZE + 1;
const uint32_t EMAIL_SIZE = COLUMN_EMAIL_SIZE + 1;
const uint32_t ID_OFFSET = 0;
const uint32_t USERNAME_OFFSET = sizeof(uint32_t);
const uint32_t EMAIL_OFFSET = sizeof(uint32_t) + COLUMN_USERNAME_SIZE + 1;
const uint32_t ROW_SIZE = sizeof(uint32_t) + COLUMN_USERNAME_SIZE + 1 + COLUMN_EMAIL_SIZE + 1;

/* Parse column type from string */
ColumType parse_column_type(const char* type_str) {
  if (strcmp(type_str, "int") == 0) return COL_TYPE_INT;
  if (strcmp(type_str, "string") == 0) return COL_TYPE_STRING;
  if (strcmp(type_str, "timestamp") == 0) return COL_TYPE_TIMESTAMP;
  return COL_TYPE_INT; /* Default */
}

/* Get column index by name */
int schema_col_index(const TableSchema* s, const char* name) {
  for (uint32_t i = 0; i < s->num_columns; i++) {
    if (strncmp(s->columns[i].name, name, MAX_COLUMN_NAME_LEN) == 0) {
      return (int)i;
    }
  }
  return -1;
}

/* Get column offset in row */
uint32_t schema_col_offset(const TableSchema* s, int col_idx) {
  uint32_t off = 0;
  for (int i = 0; i < col_idx; i++) {
    off += (s->columns[i].type == COL_TYPE_INT) ? 4 : s->columns[i].size;
  }
  return off;
}

/* Compute total row size for schema */
uint32_t compute_row_size(const TableSchema* s) {
  uint32_t sz = 0;
  for (uint32_t i = 0; i < s->num_columns; i++) {
    switch (s->columns[i].type) {
      case COL_TYPE_INT:
        sz += 4;
        break;
      case COL_TYPE_STRING:
        sz += s->columns[i].size;
        break;
      case COL_TYPE_TIMESTAMP:
        sz += 8;
        break;
    }
  }
  return sz;
}

/* Parse schemas from serialized string */
void parse_schemas_from_str(char* loaded) {
  if (!loaded) return;
  char* p = loaded;
  unsigned int count = 0;
  if (sscanf(p, "%u", &count) == 1) {
    char* nl = strchr(p, '\n');
    if (nl)
      p = nl + 1;
    else
      p = NULL;
  }
  if (!p) return;
  g_num_tables = 0;
  for (unsigned int i = 0; i < MAX_TABLES && p && *p; ++i) {
    char line[1024];
    char* nl = strchr(p, '\n');
    if (!nl) break;
    size_t len = nl - p;
    if (len >= sizeof(line)) len = sizeof(line) - 1;
    memcpy(line, p, len);
    line[len] = '\0';
    p = nl + 1;
    strncpy(g_table_schemas[i].name, line, MAX_TABLE_NAME_LEN - 1);

    nl = strchr(p, '\n');
    if (!nl) break;
    len = nl - p;
    if (len >= sizeof(line)) len = sizeof(line) - 1;
    memcpy(line, p, len);
    line[len] = '\0';
    p = nl + 1;
    unsigned int num_cols = (unsigned int)atoi(line);
    if (num_cols > MAX_COLUMNS) num_cols = MAX_COLUMNS;
    g_table_schemas[i].num_columns = num_cols;

    for (unsigned int j = 0; j < num_cols; ++j) {
      nl = strchr(p, '\n');
      if (!nl) break;
      len = nl - p;
      if (len >= sizeof(line)) len = sizeof(line) - 1;
      memcpy(line, p, len);
      line[len] = '\0';
      p = nl + 1;
      char colname[MAX_COLUMN_NAME_LEN];
      unsigned int t = 0, sz = 0;
      sscanf(line, "%[^\t]\t%u\t%u", colname, &t, &sz);
      strncpy(g_table_schemas[i].columns[j].name, colname, MAX_COLUMN_NAME_LEN - 1);
      g_table_schemas[i].columns[j].type = (ColumType)t;
      g_table_schemas[i].columns[j].size = (uint32_t)sz;
    }
    g_num_tables++;
  }
}

/* Legacy row serialization */
void serialize_row(Row* source, void* destination) {
  memcpy(destination + ID_OFFSET, &(source->id), ID_SIZE);
  memcpy(destination + USERNAME_OFFSET, &(source->username), USERNAME_SIZE);
  memcpy(destination + EMAIL_OFFSET, &(source->email), EMAIL_SIZE);
}

void deserialize_row(void* source, Row* destination) {
  memcpy(&(destination->id), source + ID_OFFSET, ID_SIZE);
  memcpy(&(destination->username), source + USERNAME_OFFSET, USERNAME_SIZE);
  memcpy(&(destination->email), source + EMAIL_OFFSET, EMAIL_SIZE);
}

