#ifndef MYDB_SQL_EXECUTOR_H
#define MYDB_SQL_EXECUTOR_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include "btree.h"
#include "schema.h"
#include "../sql_ast.h"

/* Statement types */
typedef enum { 
  STATEMENT_INSERT, 
  STATEMENT_SELECT, 
  STATEMENT_DELETE 
} StatementType;

/* Prepare results */
typedef enum {
  PREPARE_SUCCESS,
  PREPARE_NEGATIVE_ID,
  PREPARE_STRING_TOO_LONG,
  PREPARE_SYNTAX_ERROR,
  PREPARE_UNRECOGNIZED_STATEMENT,
  PREPARE_CREATE_TABLE_DONE
} PrepareResult;

/* Execute results */
typedef enum {
  EXECUTE_SUCCESS,
  EXECUTE_DUPLICATE_KEY,
} ExecuteResult;

/* Statement structure */
typedef struct Statement {
  StatementType type;
  uint32_t num_values;
  char* values[MAX_VALUES];
  
  char target_table[MAX_TABLE_NAME_LEN];
  
  /* For compatibility (legacy) */
  Row row_to_insert;
  
  /* SELECT projection */
  uint32_t proj_count; /* 0 means * */
  int proj_indices[MAX_SELECT_COLS];
  
  /* WHERE clause (legacy) */
  bool has_where;
  int where_col_index;
  bool where_is_string;
  int where_int;
  char where_str[256];
  
  /* WHERE AST */
  Expr* where_ast;
  
  /* LIMIT/OFFSET */
  bool has_limit;
  uint32_t limit;
  bool has_offset;
  uint32_t offset;
  
  /* ORDER BY */
  int order_by_index; /* -1 means not specified */
  bool order_desc;
} Statement;

/* Input buffer for REPL */
typedef struct {
  char* buffer;
  size_t buffer_length;
  ssize_t input_length;
} InputBuffer;

/* Prepare functions */
PrepareResult prepare_statement(InputBuffer* input_buffer, Statement* statement, Table* table);
PrepareResult prepare_insert(InputBuffer* input_buffer, Statement* st);
PrepareResult prepare_select(InputBuffer* in, Statement* st, Table* table);

/* Execute functions */
ExecuteResult execute_statement(Statement* statement, Table* table);
ExecuteResult execute_insert(Statement* st, Table* table);
ExecuteResult execute_delete(Statement* st, Table* table);
ExecuteResult execute_select(Statement* st, Table* table);

/* Row handler callback for flexible output */
typedef void (*RowHandler)(Table* t, const void* row, const Statement* st, void* ctx);
ExecuteResult execute_select_core(Statement* st, Table* table, RowHandler handler, void* ctx);

/* Expression evaluation */
int eval_expr_to_bool(Table* t, const void* row, Expr* e);

#endif /* MYDB_SQL_EXECUTOR_H */

