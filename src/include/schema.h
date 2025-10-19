#ifndef MYDB_SCHEMA_H
#define MYDB_SCHEMA_H

#include <stdint.h>
#include <stdbool.h>

/* Schema constants */
#define MAX_TABLES 32
#define MAX_COLUMNS 100
#define MAX_COLUMN_NAME_LEN 32
#define MAX_TABLE_NAME_LEN 32
#define MAX_VALUES MAX_COLUMNS
#define MAX_SELECT_COLS MAX_COLUMNS

/* Legacy row structure (for compatibility) */
#define COLUMN_USERNAME_SIZE 32
#define COLUMN_EMAIL_SIZE 255

typedef struct {
  uint32_t id;
  char username[COLUMN_USERNAME_SIZE + 1];
  char email[COLUMN_EMAIL_SIZE + 1];
} Row;

/* Column types */
typedef enum {
  COL_TYPE_INT,
  COL_TYPE_STRING,
  COL_TYPE_TIMESTAMP
} ColumType;

/* Column definition */
typedef struct {
  char name[MAX_COLUMN_NAME_LEN];
  ColumType type;
  uint32_t size; /* Only meaningful for strings */
} ColumnDef;

/* Table schema */
typedef struct {
  char name[MAX_TABLE_NAME_LEN];
  uint32_t num_columns;
  ColumnDef columns[MAX_COLUMNS];
} TableSchema;

/* Global schema storage */
extern TableSchema g_table_schemas[MAX_TABLES];
extern uint32_t g_num_tables;

/* Schema operations */
ColumType parse_column_type(const char* type_str);
int schema_col_index(const TableSchema* s, const char* name);
uint32_t schema_col_offset(const TableSchema* s, int col_idx);
uint32_t compute_row_size(const TableSchema* s);

/* Schema serialization */
void parse_schemas_from_str(char* loaded);

/* Legacy row serialization (for compatibility) */
void serialize_row(Row* source, void* destination);
void deserialize_row(void* source, Row* destination);

/* Legacy row size constants */
extern const uint32_t ID_SIZE;
extern const uint32_t USERNAME_SIZE;
extern const uint32_t EMAIL_SIZE;
extern const uint32_t ID_OFFSET;
extern const uint32_t USERNAME_OFFSET;
extern const uint32_t EMAIL_OFFSET;
extern const uint32_t ROW_SIZE;

#endif /* MYDB_SCHEMA_H */

