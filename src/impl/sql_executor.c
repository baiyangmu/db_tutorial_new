#include "../include/sql_executor.h"
#include "../include/btree.h"
#include "../include/catalog.h"
#include "../include/util.h"
#include "../sql_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Helper to collect values from statement */
static void collect_values(char* start, Statement* st) {
  st->num_values = 0;
  char* tok = strtok(start, " ");
  while (tok && st->num_values < MAX_VALUES) {
    st->values[st->num_values++] = tok;
    tok = strtok(NULL, " ");
  }
}

/* Prepare INSERT statement */
PrepareResult prepare_insert(InputBuffer* input_buffer, Statement* st) {
  st->type = STATEMENT_INSERT;

  char* s = input_buffer->buffer;
  while (*s == ' ' || *s == '\t') {
    s++;
  }

  st->target_table[0] = '\0';
  if (strncmp(s, "insert into ", 12) == 0) {
    s += 12;
    while (*s == ' ' || *s == '\t') {
      s++;
    }
    char* tbl = strtok(s, " ");
    if (!tbl) {
      return PREPARE_SYNTAX_ERROR;
    }

    strncpy(st->target_table, tbl, MAX_TABLE_NAME_LEN - 1);
    char* vals = strtok(NULL, "");
    if (!vals) {
      st->num_values = 0;
    } else {
      collect_values(vals, st);
    }
    return PREPARE_SUCCESS;
  }
  return PREPARE_UNRECOGNIZED_STATEMENT;
}

/* Prepare SELECT statement (simple parser) */
PrepareResult prepare_select(InputBuffer* in, Statement* st, Table* table) {
  st->type = STATEMENT_SELECT;
  st->target_table[0] = '\0';
  st->proj_count = 0;
  st->has_where = false;

  char* s = in->buffer;
  while (*s == ' ' || *s == '\t') {
    s++;
  }

  s += 6; /* Skip "select" */
  while (*s == ' ' || *s == '\t') {
    s++;
  }

  char* proj_start = s;
  char* from_kw = strstr(proj_start, " from ");
  char* where_kw = strstr(proj_start, " where ");

  const TableSchema* schema_for_select = &table->active_schema;
  char from_table_buf[MAX_TABLE_NAME_LEN] = {0};
  if (from_kw != NULL) {
    char* p = from_kw + 6;
    while (*p == ' ' || *p == '\t') {
      p++;
    }
    char* name_end = p;
    while (*name_end && *name_end != ' ' && *name_end != '\t') {
      name_end++;
    }
    size_t name_len = (size_t)(name_end - p);
    if (name_len >= MAX_TABLE_NAME_LEN) name_len = MAX_TABLE_NAME_LEN - 1;
    strncpy(from_table_buf, p, name_len);
    from_table_buf[name_len] = '\0';

    TableSchema tmp;
    if (lookup_table_schema(table->pager, from_table_buf, &tmp) != 0) {
      printf("Table not found: %s\n", from_table_buf);
      return PREPARE_SYNTAX_ERROR;
    }
    schema_for_select = &tmp;
    strncpy(st->target_table, from_table_buf, MAX_TABLE_NAME_LEN - 1);
    st->target_table[MAX_TABLE_NAME_LEN - 1] = '\0';
  }

  if (*s == '*') {
    s++;
  } else {
    char* proj_end = NULL;
    if (from_kw != NULL) {
      proj_end = from_kw;
    } else if (where_kw != NULL) {
      proj_end = where_kw;
    } else {
      proj_end = proj_start + strlen(proj_start);
    }

    size_t proj_len = (size_t)(proj_end - proj_start);
    char proj_buf[512];
    if (proj_len >= sizeof(proj_buf)) {
      proj_len = sizeof(proj_buf) - 1;
    }
    strncpy(proj_buf, proj_start, proj_len);
    proj_buf[proj_len] = '\0';

    char* tok = strtok(proj_buf, ",");
    while (tok != NULL) {
      while (*tok == ' ' || *tok == '\t') {
        tok++;
      }
      int col_index = schema_col_index(schema_for_select, tok);
      if (col_index < 0) {
        printf("Unknown column: %s\n", tok);
        return PREPARE_SYNTAX_ERROR;
      }
      st->proj_indices[st->proj_count++] = col_index;
      tok = strtok(NULL, ",");
    }

    s = (where_kw != NULL) ? where_kw : proj_end;
  }

  while (*s == ' ' || *s == '\t') {
    s++;
  }
  if (strncmp(s, "where ", 6) == 0) {
    s += 6;
    while (*s == ' ' || *s == '\t') {
      s++;
    }

    char col_name[64];
    int ci = 0;
    while (*s && *s != ' ' && *s != '\t' && *s != '=') {
      if (ci < (int)sizeof(col_name) - 1) {
        col_name[ci++] = *s;
      }
      s++;
    }
    col_name[ci] = '\0';

    while (*s == ' ' || *s == '\t') {
      s++;
    }
    if (*s != '=') {
      return PREPARE_SYNTAX_ERROR;
    }
    s++;
    while (*s == ' ' || *s == '\t') {
      s++;
    }

    char val[256];
    int vi = 0;
    while (*s && *s != ' ' && *s != '\t') {
      if (vi < (int)sizeof(val) - 1) {
        val[vi++] = *s;
      }
      s++;
    }
    val[vi] = '\0';

    int idx = schema_col_index(schema_for_select, col_name);
    if (idx < 0) {
      printf("Unknown column: %s\n", col_name);
      return PREPARE_SYNTAX_ERROR;
    }

    st->has_where = true;
    st->where_col_index = idx;

    if (schema_for_select->columns[idx].type == COL_TYPE_INT) {
      int v = 0;
      if (parse_int(val, &v) != 0) {
        printf("Invalid int: %s\n", val);
        return PREPARE_SYNTAX_ERROR;
      }
      st->where_is_string = false;
      st->where_int = v;
    } else {
      st->where_is_string = true;
      strncpy(st->where_str, val, sizeof(st->where_str) - 1);
      st->where_str[sizeof(st->where_str) - 1] = '\0';
    }
  }

  return PREPARE_SUCCESS;
}

/* Prepare statement */
PrepareResult prepare_statement(InputBuffer* input_buffer, Statement* statement, Table* table) {
  const char* s = input_buffer->buffer;
  statement->where_ast = NULL;
  while (*s == ' ' || *s == '\t') {
    s++;
  }

  if (strncmp(s, "use ", 4) == 0) {
    const char* name = s + 4;
    while (*name == ' ' || *name == '\t') {
      name++;
    }
    int idx = catalog_find(table->pager, name);
    if (idx < 0) {
      printf("Table not found: %s\n", name);
      return PREPARE_UNRECOGNIZED_STATEMENT;
    }
    CatalogEntry* ents = catalog_entries(table->pager);
    table->root_page_num = ents[idx].root_page_num;

    uint32_t sidx = ents[idx].schema_index;
    if (sidx < g_num_tables) {
      table->active_schema = g_table_schemas[sidx];
    } else {
      memset(&table->active_schema, 0, sizeof(TableSchema));
    }
    table->row_size = compute_row_size(&table->active_schema);

    printf("Using table '%s'.\n", ents[idx].name);
    return PREPARE_CREATE_TABLE_DONE;
  }

  if (strncmp(s, "insert", 6) == 0) {
    return prepare_insert(input_buffer, statement);
  }
  if (strncmp(s, "create table", 12) == 0) {
    int ret = handle_create_table_ex(table, input_buffer->buffer);
    if (ret == 0) {
      return PREPARE_CREATE_TABLE_DONE;
    } else {
      printf("Create table failed: %d\n", ret);
      return PREPARE_UNRECOGNIZED_STATEMENT;
    }
  }
  if (strncmp(s, "select", 6) == 0) {
    ParsedStmt ps;
    statement->type = STATEMENT_SELECT;
    if (parse_sql_to_parsed_stmt(input_buffer->buffer, &ps) != 0) {
      return PREPARE_SYNTAX_ERROR;
    }
    if (ps.kind != PARSED_SELECT) {
      parsed_stmt_free(&ps);
      return PREPARE_SYNTAX_ERROR;
    }

    if (ps.table_name[0]) {
      strncpy(statement->target_table, ps.table_name, MAX_TABLE_NAME_LEN - 1);
      statement->target_table[MAX_TABLE_NAME_LEN - 1] = '\0';
    } else {
      statement->target_table[0] = '\0';
    }

    statement->proj_count = 0;
    if (ps.select_all) {
      statement->proj_count = 0;
    } else {
      for (uint32_t i = 0; i < ps.proj_count; i++) {
        int idx = schema_col_index(&table->active_schema, ps.proj_list[i]);
        if (idx < 0) {
          printf("Unknown column: %s\n", ps.proj_list[i]);
          parsed_stmt_free(&ps);
          return PREPARE_SYNTAX_ERROR;
        }
        statement->proj_indices[statement->proj_count++] = idx;
      }
    }

    statement->has_limit = ps.has_limit;
    statement->limit = ps.limit;
    statement->has_offset = ps.has_offset;
    statement->offset = ps.offset;

    if (ps.order_by[0]) {
      int ob_idx = schema_col_index(&table->active_schema, ps.order_by);
      if (ob_idx < 0) {
        printf("Unknown column in ORDER BY: %s\n", ps.order_by);
        parsed_stmt_free(&ps);
        return PREPARE_SYNTAX_ERROR;
      }
      statement->order_by_index = ob_idx;
      statement->order_desc = ps.order_desc ? true : false;
    } else {
      statement->order_by_index = -1;
      statement->order_desc = false;
    }

    statement->where_ast = ps.where;
    statement->has_where = (ps.where != NULL);
    ps.where = NULL;

    parsed_stmt_free(&ps);
    return PREPARE_SUCCESS;
  }
  if (strncmp(s, "delete", 6) == 0) {
    ParsedStmt ps;
    if (parse_sql_to_parsed_stmt(input_buffer->buffer, &ps) != 0) {
      return PREPARE_SYNTAX_ERROR;
    }
    if (ps.kind != PARSED_DELETE) {
      parsed_stmt_free(&ps);
      return PREPARE_SYNTAX_ERROR;
    }
    statement->type = STATEMENT_DELETE;
    if (ps.table_name[0]) {
      strncpy(statement->target_table, ps.table_name, MAX_TABLE_NAME_LEN - 1);
      statement->target_table[MAX_TABLE_NAME_LEN - 1] = '\0';
    } else {
      statement->target_table[0] = '\0';
    }
    statement->where_ast = ps.where;
    ps.where = NULL;

    parsed_stmt_free(&ps);
    return PREPARE_SUCCESS;
  }

  return PREPARE_UNRECOGNIZED_STATEMENT;
}

/* Execute INSERT */
ExecuteResult execute_insert(Statement* st, Table* table) {
  fprintf(stderr, "[DEBUG-INSERT] Starting INSERT operation\n");
  fflush(stderr);

  if (table->root_page_num == INVALID_PAGE_NUM) {
    fprintf(stderr, "[DEBUG-INSERT] No active table (root_page_num=INVALID)\n");
    fflush(stderr);
    printf("No active table. Use 'use <table>' or 'insert into <table> ...' first.\n");
    return EXECUTE_SUCCESS;
  }

  fprintf(stderr, "[DEBUG-INSERT] Current table root_page_num=%u\n", table->root_page_num);
  fflush(stderr);

  if (st->target_table[0]) {
    fprintf(stderr, "[DEBUG-INSERT] Switching to target table: %s\n", st->target_table);
    fflush(stderr);
    int idx = catalog_find(table->pager, st->target_table);
    if (idx < 0) {
      fprintf(stderr, "[DEBUG-INSERT] Table not found: %s\n", st->target_table);
      fflush(stderr);
      printf("Table not found: %s\n", st->target_table);
      return EXECUTE_SUCCESS;
    }
    CatalogEntry* ents = catalog_entries(table->pager);
    table->root_page_num = ents[idx].root_page_num;
    uint32_t sidx = ents[idx].schema_index;
    fprintf(stderr, "[DEBUG-INSERT] Switched to table, new root_page_num=%u, schema_index=%u\n", table->root_page_num, sidx);
    fflush(stderr);
    if (sidx < g_num_tables) {
      table->active_schema = g_table_schemas[sidx];
    } else {
      memset(&table->active_schema, 0, sizeof(TableSchema));
    }
    table->row_size = compute_row_size(&table->active_schema);
  }

  if (table->active_schema.num_columns == 0 || table->active_schema.columns[0].type != COL_TYPE_INT) {
    fprintf(stderr, "[DEBUG-INSERT] Schema validation failed: num_columns=%u, first_col_type=%d\n",
            table->active_schema.num_columns,
            table->active_schema.num_columns > 0 ? table->active_schema.columns[0].type : -1);
    fflush(stderr);
    printf("First column must be int primary key.\n");
    return EXECUTE_SUCCESS;
  }
  int key_int = 0;
  if (st->num_values == 0 || parse_int(st->values[0], &key_int) != 0) {
    fprintf(stderr, "[DEBUG-INSERT] Key parsing failed: num_values=%u, first_value=%s\n",
            st->num_values, st->num_values > 0 ? st->values[0] : "NULL");
    fflush(stderr);
    printf("Invalid key.\n");
    return EXECUTE_SUCCESS;
  }
  uint32_t key = (uint32_t)key_int;
  fprintf(stderr, "[DEBUG-INSERT] Attempting to insert key=%u\n", key);
  fflush(stderr);

  Cursor* cursor = table_find(table, key);
  fprintf(stderr, "[DEBUG-INSERT] table_find returned: page_num=%u, cell_num=%u\n",
          cursor->page_num, cursor->cell_num);
  fflush(stderr);

  void* node = get_page(table->pager, cursor->page_num);
  uint32_t num_cells = *leaf_node_num_cells(node);
  fprintf(stderr, "[DEBUG-INSERT] Node has %u cells\n", num_cells);
  fflush(stderr);

  if (cursor->cell_num < num_cells) {
    uint32_t key_at_index = *leaf_key_t(table, node, cursor->cell_num);
    fprintf(stderr, "[DEBUG-INSERT] Key at cursor position: %u (searching for %u)\n", key_at_index, key);
    fflush(stderr);
    if (key_at_index == key) {
      fprintf(stderr, "[DEBUG-INSERT] Duplicate key detected: %u\n", key);
      fflush(stderr);
      free(cursor);
      return EXECUTE_DUPLICATE_KEY;
    }
  } else {
    fprintf(stderr, "[DEBUG-INSERT] Cursor at end position (cell_num=%u >= num_cells=%u)\n", cursor->cell_num, num_cells);
    fflush(stderr);
  }

  fprintf(stderr, "[DEBUG-INSERT] Proceeding with leaf_node_insert for key=%u\n", key);
  fflush(stderr);
  leaf_node_insert(cursor, key, st->values, st->num_values);
  fprintf(stderr, "[DEBUG-INSERT] leaf_node_insert completed successfully\n");
  fflush(stderr);
  free(cursor);
  return EXECUTE_SUCCESS;
}

/* Execute DELETE */
ExecuteResult execute_delete(Statement* st, Table* table) {
  fprintf(stderr, "[DEBUG-DELETE] Starting DELETE operation\n");
  fflush(stderr);

  if (st->target_table[0]) {
    fprintf(stderr, "[DEBUG-DELETE] Switching to target table: %s\n", st->target_table);
    fflush(stderr);
    int idx = catalog_find(table->pager, st->target_table);
    if (idx < 0) {
      fprintf(stderr, "[DEBUG-DELETE] Table not found: %s\n", st->target_table);
      fflush(stderr);
      printf("Table not found: %s\n", st->target_table);
      return EXECUTE_SUCCESS;
    }
    CatalogEntry* ents = catalog_entries(table->pager);
    table->root_page_num = ents[idx].root_page_num;
    uint32_t sidx = ents[idx].schema_index;
    fprintf(stderr, "[DEBUG-DELETE] Switched to table, root_page_num=%u, schema_index=%u\n", table->root_page_num, sidx);
    fflush(stderr);
    if (sidx < g_num_tables) {
      table->active_schema = g_table_schemas[sidx];
    } else {
      memset(&table->active_schema, 0, sizeof(TableSchema));
    }
    table->row_size = compute_row_size(&table->active_schema);
  }

  if (table->root_page_num == INVALID_PAGE_NUM) {
    fprintf(stderr, "[DEBUG-DELETE] No active table (root_page_num=INVALID)\n");
    fflush(stderr);
    printf("No active table. Use 'use <table>' or 'create table..' first.\n");
    return EXECUTE_SUCCESS;
  }

  fprintf(stderr, "[DEBUG-DELETE] Current table root_page_num=%u\n", table->root_page_num);
  fflush(stderr);

  uint32_t key = 0;
  int have_key = 0;

  if (st->where_ast) {
    fprintf(stderr, "[DEBUG-DELETE] Processing WHERE AST\n");
    fflush(stderr);
    Expr* ast = st->where_ast;
    if (ast && ast->kind == EXPR_BINARY && strcmp(ast->op, "=") == 0) {
      Expr* left = ast->left;
      Expr* right = ast->right;
      Expr* colExpr = NULL;
      Expr* litExpr = NULL;

      if (left && left->kind == EXPR_COLUMN && right && right->kind == EXPR_LITERAL) {
        colExpr = left;
        litExpr = right;
      } else if (right && right->kind == EXPR_COLUMN && left && left->kind == EXPR_LITERAL) {
        colExpr = right;
        litExpr = left;
      }

      if (colExpr && litExpr) {
        int col_idx = schema_col_index(&table->active_schema, colExpr->text);
        fprintf(stderr, "[DEBUG-DELETE] WHERE clause: column=%s (idx=%d), value=%s\n", colExpr->text, col_idx, litExpr->text);
        fflush(stderr);
        if (col_idx == 0 && table->active_schema.columns[0].type == COL_TYPE_INT) {
          int v = 0;
          if (parse_int(litExpr->text, &v) == 0) {
            key = (uint32_t)v;
            have_key = 1;
            fprintf(stderr, "[DEBUG-DELETE] Extracted key from WHERE: %u\n", key);
            fflush(stderr);
          }
        }
      }
    }
  }

  if (!have_key) {
    fprintf(stderr, "[DEBUG-DELETE] Trying legacy WHERE format\n");
    fflush(stderr);
    if (st->has_where && st->where_col_index == 0 && !st->where_is_string &&
        table->active_schema.columns[0].type == COL_TYPE_INT) {
      key = (uint32_t)st->where_int;
      have_key = 1;
      fprintf(stderr, "[DEBUG-DELETE] Using legacy WHERE key: %u\n", key);
      fflush(stderr);
    }
  }

  if (!have_key) {
    fprintf(stderr, "[DEBUG-DELETE] No valid key found in WHERE clause\n");
    fflush(stderr);
    printf("Only DELETE by integer primary key is supported.\n");
    return EXECUTE_SUCCESS;
  }

  fprintf(stderr, "[DEBUG-DELETE] Searching for key to delete: %u\n", key);
  fflush(stderr);
  Cursor* cursor = table_find(table, key);
  fprintf(stderr, "[DEBUG-DELETE] table_find returned: page_num=%u, cell_num=%u\n", cursor->page_num, cursor->cell_num);
  fflush(stderr);

  void* node = get_page(table->pager, cursor->page_num);
  uint32_t num_cells = *leaf_node_num_cells(node);
  fprintf(stderr, "[DEBUG-DELETE] Node has %u cells before deletion\n", num_cells);
  fflush(stderr);

  if (cursor->cell_num >= num_cells) {
    fprintf(stderr, "[DEBUG-DELETE] Key not found: cursor position (%u) >= num_cells (%u)\n", cursor->cell_num, num_cells);
    fflush(stderr);
    free(cursor);
    return EXECUTE_SUCCESS;
  }
  uint32_t key_at_index = *leaf_key_t(table, node, cursor->cell_num);
  fprintf(stderr, "[DEBUG-DELETE] Key at cursor position: %u (looking for %u)\n", key_at_index, key);
  fflush(stderr);
  if (key_at_index != key) {
    fprintf(stderr, "[DEBUG-DELETE] Key mismatch: found %u but looking for %u\n", key_at_index, key);
    fflush(stderr);
    free(cursor);
    return EXECUTE_SUCCESS;
  }

  fprintf(stderr, "[DEBUG-DELETE] Found key %u, proceeding with deletion\n", key);
  fflush(stderr);
  uint32_t old_leaf_max = get_node_max_key(table, node);
  fprintf(stderr, "[DEBUG-DELETE] Node max key before deletion: %u\n", old_leaf_max);
  fflush(stderr);

  fprintf(stderr, "[DEBUG-DELETE] Shifting cells: from position %u to %u\n", cursor->cell_num, num_cells - 1);
  fflush(stderr);
  for (uint32_t i = cursor->cell_num; i < num_cells - 1; i++) {
    void* dest = leaf_cell_t(table, node, i);
    void* src = leaf_cell_t(table, node, i + 1);
    memcpy(dest, src, leaf_cell_size(table));
  }
  void* last_cell = leaf_cell_t(table, node, num_cells - 1);
  memset(last_cell, 0, leaf_cell_size(table));
  *(leaf_node_num_cells(node)) = num_cells - 1;

  uint32_t new_num = *leaf_node_num_cells(node);
  fprintf(stderr, "[DEBUG-DELETE] Node has %u cells after deletion\n", new_num);
  fflush(stderr);

  uint32_t new_max = 0;
  if (new_num > 0) {
    new_max = *leaf_key_t(table, node, new_num - 1);
    fprintf(stderr, "[DEBUG-DELETE] New node max key: %u\n", new_max);
    fflush(stderr);
  } else {
    new_max = 0;
    fprintf(stderr, "[DEBUG-DELETE] Node is now empty (new_max=0)\n");
    fflush(stderr);
  }

  if (old_leaf_max != new_max) {
    fprintf(stderr, "[DEBUG-DELETE] Node max key changed from %u to %u\n", old_leaf_max, new_max);
    fflush(stderr);
    if (!is_node_root(node)) {
      uint32_t parent_page = *node_parent(node);
      fprintf(stderr, "[DEBUG-DELETE] Updating parent node (page %u) key: %u -> %u\n", parent_page, old_leaf_max, new_max);
      fflush(stderr);
      void* parent = get_page(table->pager, parent_page);
      update_internal_node_key(parent, old_leaf_max, new_max);
    } else {
      fprintf(stderr, "[DEBUG-DELETE] Node is root, no parent to update\n");
      fflush(stderr);
    }
  } else {
    fprintf(stderr, "[DEBUG-DELETE] Node max key unchanged (%u)\n", old_leaf_max);
    fflush(stderr);
  }

  fprintf(stderr, "[DEBUG-DELETE] Delete operation completed successfully\n");
  fflush(stderr);

  uint32_t current_cells = *leaf_node_num_cells(node);
  if (current_cells == 0) {
    fprintf(stderr, "[DEBUG-DELETE] Node is empty, triggering merge\n");
    fflush(stderr);
    handle_underflow(table, cursor->page_num);
  }

  free(cursor);
  return EXECUTE_SUCCESS;
}

/* Expression evaluation */
int eval_expr_to_bool(Table* t, const void* row, Expr* e) {
  if (!e) {
    return 1;
  }

  switch (e->kind) {
    case EXPR_LITERAL: {
      if (strlen(e->text) == 0) return 0;
      if (strcmp(e->text, "0") == 0) return 0;
      return 1;
    }

    case EXPR_COLUMN: {
      int idx = schema_col_index(&t->active_schema, e->text);
      if (idx < 0) {
        return 0;
      }
      if (t->active_schema.columns[idx].type == COL_TYPE_INT) {
        int v = row_get_int(t, row, idx);
        return v != 0;
      } else {
        char buf[512];
        row_get_string(t, row, idx, buf, sizeof(buf));
        return buf[0] != '\0';
      }
    }

    case EXPR_UNARY: {
      if (strcmp(e->op, "NOT") == 0) {
        return !eval_expr_to_bool(t, row, e->left);
      }
      return 0;
    }

    case EXPR_BINARY: {
      if (strcmp(e->op, "AND") == 0) {
        return eval_expr_to_bool(t, row, e->left) && eval_expr_to_bool(t, row, e->right);
      }
      if (strcmp(e->op, "OR") == 0) {
        return eval_expr_to_bool(t, row, e->left) || eval_expr_to_bool(t, row, e->right);
      }

      int lhs_int = 0;
      int rhs_int = 0;
      char lhs_s[512] = {0};
      char rhs_s[512] = {0};
      int is_num = 0;

      if (e->left->kind == EXPR_COLUMN) {
        int idx = schema_col_index(&t->active_schema, e->left->text);
        if (idx >= 0 && t->active_schema.columns[idx].type == COL_TYPE_INT) {
          lhs_int = row_get_int(t, row, idx);
          is_num = 1;
        } else if (idx >= 0) {
          row_get_string(t, row, idx, lhs_s, sizeof(lhs_s));
        }
      } else if (e->left->kind == EXPR_LITERAL) {
        if (parse_int(e->left->text, &lhs_int) == 0) {
          is_num = 1;
        } else {
          strncpy(lhs_s, e->left->text, sizeof(lhs_s) - 1);
          lhs_s[sizeof(lhs_s) - 1] = '\0';
        }
      }

      if (e->right->kind == EXPR_COLUMN) {
        int idx = schema_col_index(&t->active_schema, e->right->text);
        if (idx >= 0 && t->active_schema.columns[idx].type == COL_TYPE_INT) {
          rhs_int = row_get_int(t, row, idx);
          is_num = 1;
        } else if (idx >= 0) {
          row_get_string(t, row, idx, rhs_s, sizeof(rhs_s));
        }
      } else if (e->right->kind == EXPR_LITERAL) {
        if (parse_int(e->right->text, &rhs_int) == 0) {
          is_num = 1;
        } else {
          strncpy(rhs_s, e->right->text, sizeof(rhs_s) - 1);
          rhs_s[sizeof(rhs_s) - 1] = '\0';
        }
      }

      if (is_num) {
        if (strcmp(e->op, "=") == 0) return lhs_int == rhs_int;
        if (strcmp(e->op, "!=") == 0) return lhs_int != rhs_int;
        if (strcmp(e->op, "<") == 0) return lhs_int < rhs_int;
        if (strcmp(e->op, "<=") == 0) return lhs_int <= rhs_int;
        if (strcmp(e->op, ">") == 0) return lhs_int > rhs_int;
        if (strcmp(e->op, ">=") == 0) return lhs_int >= rhs_int;
      } else {
        int result = strcmp(lhs_s, rhs_s);
        if (strcmp(e->op, "=") == 0) {
          return (result == 0);
        }
        if (strcmp(e->op, "!=") == 0) return strcmp(lhs_s, rhs_s) != 0;
        if (strcmp(e->op, "<") == 0) return strcmp(lhs_s, rhs_s) < 0;
        if (strcmp(e->op, ">") == 0) return strcmp(lhs_s, rhs_s) > 0;
        if (strcmp(e->op, "<=") == 0) return strcmp(lhs_s, rhs_s) <= 0;
        if (strcmp(e->op, ">=") == 0) return strcmp(lhs_s, rhs_s) >= 0;
      }
      return 0;
    }

    case EXPR_BETWEEN:
    case EXPR_ISNULL:
    case EXPR_IN:
      /* Additional expression types can be implemented here */
      return 0;
  }
  return 0;
}

/* Row handler for printing */
static void print_row_handler(Table* t, const void* row, const Statement* st, void* ctx) {
  (void)ctx;
  print_row_projected(t, row, st->proj_indices, st->proj_count);
}

/* Execute SELECT with custom handler */
ExecuteResult execute_select_core(Statement* st, Table* table, RowHandler handler, void* ctx) {
  if (st->target_table[0]) {
    int idx = catalog_find(table->pager, st->target_table);
    if (idx < 0) {
      printf("Table not found: %s\n", st->target_table);
      return EXECUTE_SUCCESS;
    }
    CatalogEntry* ents = catalog_entries(table->pager);
    table->root_page_num = ents[idx].root_page_num;
    uint32_t sidx = ents[idx].schema_index;
    if (sidx < g_num_tables) {
      table->active_schema = g_table_schemas[sidx];
    } else {
      memset(&table->active_schema, 0, sizeof(TableSchema));
    }
    table->row_size = compute_row_size(&table->active_schema);
  }

  if (table->root_page_num == INVALID_PAGE_NUM) {
    printf("No active table. Use 'use <table>' or 'create table..' first.\n");
    return EXECUTE_SUCCESS;
  }

  bool can_point_lookup = false;
  uint32_t lookup_key = 0;

  Expr* ast = st->where_ast;

  if (ast && ast->kind == EXPR_BINARY && strcmp(ast->op, "=") == 0) {
    Expr* left = ast->left;
    Expr* right = ast->right;
    Expr* colExpr = NULL;
    Expr* litExpr = NULL;

    if (left && left->kind == EXPR_COLUMN && right && right->kind == EXPR_LITERAL) {
      colExpr = left;
      litExpr = right;
    } else if (right && right->kind == EXPR_COLUMN && left && left->kind == EXPR_LITERAL) {
      colExpr = right;
      litExpr = left;
    }

    if (colExpr && litExpr) {
      int col_idx = schema_col_index(&table->active_schema, colExpr->text);
      if (col_idx == 0 && table->active_schema.columns[0].type == COL_TYPE_INT) {
        int v = 0;
        if (parse_int(litExpr->text, &v) == 0) {
          can_point_lookup = true;
          lookup_key = (uint32_t)v;
        }
      }
    }
  }

  if (!can_point_lookup) {
    if (st->has_where &&
        (st->where_col_index == 0) &&
        !st->where_is_string &&
        (table->active_schema.columns[0].type == COL_TYPE_INT)) {
      can_point_lookup = true;
      lookup_key = (uint32_t)st->where_int;
    }
  }

  if (can_point_lookup) {
    Cursor* cursor = table_find(table, lookup_key);
    void* node = get_page(table->pager, cursor->page_num);
    uint32_t num_cells = *leaf_node_num_cells(node);

    if (cursor->cell_num < num_cells) {
      uint32_t key_at_index = *leaf_key_t(table, node, cursor->cell_num);
      if (key_at_index == lookup_key) {
        void* row = leaf_value_t(table, node, cursor->cell_num);
        int pass = 1;
        if (ast) {
          pass = eval_expr_to_bool(table, row, ast);
        } else if (st->has_where) {
          pass = row_matches_where(table, row, st);
        }
        if (pass && handler) {
          handler(table, row, st, ctx);
        }
      }
    }
    free(cursor);
    return EXECUTE_SUCCESS;
  }

  /* Full table scan */
  RowRef* rows = NULL;
  size_t nrows = 0;
  size_t capacity = 0;

  Cursor* cursor = table_start(table);
  while (!cursor->end_of_table) {
    void* row = cursor_value(cursor);
    int pass = 1;
    if (ast) {
      pass = eval_expr_to_bool(table, row, ast);
    } else if (st->has_where) {
      pass = row_matches_where(table, row, st);
    }

    if (pass) {
      if (nrows == capacity) {
        size_t newcap = capacity ? capacity * 2 : 256;
        RowRef* tmp = realloc(rows, newcap * sizeof(RowRef));
        if (!tmp) {
          printf("Out of memory\n");
          free(rows);
          free(cursor);
          return EXECUTE_SUCCESS;
        }
        rows = tmp;
        capacity = newcap;
      }
      rows[nrows++].row = row;
    }
    cursor_advance(cursor);
  }
  free(cursor);

  /* Optional sort */
  if (st->order_by_index >= 0 && nrows > 1) {
    g_sort_ctx.table = table;
    g_sort_ctx.col_idx = st->order_by_index;
    g_sort_ctx.desc = st->order_desc ? 1 : 0;
    qsort(rows, nrows, sizeof(RowRef), rowref_comparator);
  }

  /* Apply offset/limit */
  size_t start = st->has_offset ? st->offset : 0;
  size_t end = st->has_limit ? (start + st->limit) : nrows;
  if (start > nrows) start = nrows;
  if (end > nrows) end = nrows;
  for (size_t i = start; i < end; ++i) {
    if (handler) {
      handler(table, rows[i].row, st, ctx);
    }
  }

  free(rows);
  return EXECUTE_SUCCESS;
}

/* Execute SELECT (with default printing) */
ExecuteResult execute_select(Statement* st, Table* table) {
  return execute_select_core(st, table, print_row_handler, NULL);
}

/* Execute any statement */
ExecuteResult execute_statement(Statement* statement, Table* table) {
  ExecuteResult result = EXECUTE_SUCCESS;
  switch (statement->type) {
    case (STATEMENT_INSERT):
      result = execute_insert(statement, table);
      break;
    case (STATEMENT_SELECT):
      result = execute_select(statement, table);
      break;
    case (STATEMENT_DELETE):
      result = execute_delete(statement, table);
      break;
    default:
      result = EXECUTE_SUCCESS;
  }

  if (statement->where_ast) {
    expr_free(statement->where_ast);
    statement->where_ast = NULL;
  }
  return result;
}

