#include "../include/mydb.h"
#include "../include/btree.h"
#include "../include/sql_executor.h"
#include "../include/util.h"
#include "../include/catalog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
static int emsfs_mounted = 0;
static void ems_sync_from_idb(void) {
  EM_ASM({ FS.syncfs(true, function(err) { if (err) { console.log('FS.syncfs(true) error', err); } }); });
}

static void ems_sync_to_idb(void) {
  EM_ASM({
    try {
      FS.syncfs(false, function(err) {
        if (err) { console.log('FS.syncfs(false) error', err); }
        else { console.log('FS.syncfs(false) complete'); }
      });
    } catch (e) { console.log('ems_sync_to_idb EM_ASM failed', e); }
  });
}

static void ems_fs_init(void) {
  if (!emsfs_mounted) {
    EM_ASM({ try { FS.mkdir('/persistent'); } catch(e) {} try { FS.mount(IDBFS, {}, '/persistent'); } catch(e) {} });
    ems_sync_from_idb();
    emsfs_mounted = 1;
  }
}

static char* ems_path_for(const char* filename) {
  size_t need = strlen(filename) + sizeof("/persistent/");
  char* path = malloc(need);
  if (!path) return NULL;
  snprintf(path, need, "/persistent/%s", filename);
  return path;
}

static void ems_persist_flush(Table* table) {
  if (!table || !table->pager) return;
  for (uint32_t i = 0; i < table->pager->num_pages; i++) {
    if (table->pager->pages[i]) {
      pager_flush(table->pager, i);
    }
  }
}
#else
/* Non-Emscripten stubs */
static void ems_fs_init(void) { }
static void ems_sync_from_idb(void) { (void)0; }
static void ems_sync_to_idb(void) { (void)0; }
static char* ems_path_for(const char* filename) {
  return strdup(filename);
}
static void ems_persist_flush(Table* table) { (void)table; }
#endif

/* Append row as JSON */
static void append_row_json(StrBuf* sb, Table* t, const void* row) {
  sb_append(sb, "{");
  for (uint32_t i = 0; i < t->active_schema.num_columns; ++i) {
    ColumnDef* c = &t->active_schema.columns[i];
    if (i) {
      sb_append(sb, ",");
    }
    sb_appendf(sb, "\"%s\":", c->name);
    if (c->type == COL_TYPE_INT) {
      int v = row_get_int(t, row, i);
      sb_appendf(sb, "%d", v);
    } else if (c->type == COL_TYPE_TIMESTAMP) {
      int64_t tv = row_get_timestamp(t, row, i);
      sb_appendf(sb, "%lld", (long long)tv);
    } else {
      char buf[1024];
      row_get_string(t, row, i, buf, sizeof(buf));
      json_escape_append(sb, buf);
    }
  }
  sb_append(sb, "}");
}

/* Append projected row as JSON */
static void append_row_json_projected(StrBuf* sb, Table* t, const void* row, const int* idxs, uint32_t n) {
  if (n == 0) {
    append_row_json(sb, t, row);
    return;
  }
  sb_append(sb, "{");
  for (uint32_t i = 0; i < n; ++i) {
    if (i) sb_append(sb, ",");
    int col = idxs[i];
    ColumnDef* c = &t->active_schema.columns[col];
    sb_appendf(sb, "\"%s\":", c->name);
    if (c->type == COL_TYPE_INT) {
      int v = row_get_int(t, row, col);
      sb_appendf(sb, "%d", v);
    } else if (c->type == COL_TYPE_TIMESTAMP) {
      int64_t tv = row_get_timestamp(t, row, col);
      sb_appendf(sb, "%lld", (long long)tv);
    } else {
      char buf[1024];
      row_get_string(t, row, col, buf, sizeof(buf));
      json_escape_append(sb, buf);
    }
  }
  sb_append(sb, "}");
}

/* JSON row handler context */
typedef struct {
  StrBuf* sb;
  int first;
} JsonCtx;

/* Row handler for JSON output */
static void json_row_handler(Table* t, const void* row, const Statement* st, void* ctx) {
  JsonCtx* jc = (JsonCtx*)ctx;
  if (!jc->first) {
    sb_append(jc->sb, ",");
  }
  if (st->proj_count == 0) {
    append_row_json(jc->sb, t, row);
  } else {
    append_row_json_projected(jc->sb, t, row, st->proj_indices, st->proj_count);
  }
  jc->first = 0;
}

/* Public API */
MYDB_Handle mydb_open(const char* filename) {
  if (!filename) {
    return NULL;
  }
  return (MYDB_Handle)db_open(filename);
}

MYDB_Handle mydb_open_with_ems(const char* filename) {
  if (!filename) {
    return NULL;
  }
  ems_fs_init();
  char* pathbuf = ems_path_for(filename);
  if (!pathbuf) return NULL;
  Table* t = db_open(pathbuf);
  free(pathbuf);
  return (MYDB_Handle)t;
}

void mydb_close(MYDB_Handle h) {
  if (!h) {
    return;
  }
  db_close((Table*)h);
}

void mydb_close_with_ems(MYDB_Handle h) {
  if (!h) return;
  db_close((Table*)h);
  if (((Table*)h)->pager && ((Table*)h)->pager->filename) {
    save_schemas_for_db(((Table*)h)->pager->filename);
  }
  ems_sync_to_idb();
}

int mydb_execute_json(MYDB_Handle h, const char* sql, char** out_json) {
  if (!out_json) {
    return -1;
  }
  *out_json = NULL;
  if (!h || !sql) {
    return -2;
  }
  Table* table = (Table*)h;

  InputBuffer ib;
  ib.buffer = strdup(sql);
  ib.buffer_length = strlen(ib.buffer) + 1;
  ib.input_length = (ssize_t)strlen(ib.buffer);

  Statement st;
  memset(&st, 0, sizeof(st));
  st.where_ast = NULL;
  PrepareResult pr = prepare_statement(&ib, &st, table);
  if (pr == PREPARE_SYNTAX_ERROR) {
    free(ib.buffer);
    return -3;
  }
  if (pr == PREPARE_CREATE_TABLE_DONE) {
    StrBuf sb;
    sb_init(&sb);
    sb_append(&sb, "{\"ok\":true,\"message\":\"Executed.\"}");
    *out_json = sb.buf;
    free(ib.buffer);
    return 0;
  }

  if (pr != PREPARE_SUCCESS) {
    free(ib.buffer);
    return -4;
  }

  if (st.type == STATEMENT_INSERT || st.type == STATEMENT_DELETE) {
    ExecuteResult er = execute_statement(&st, table);
    StrBuf sb;
    sb_init(&sb);
    if (er == EXECUTE_DUPLICATE_KEY) {
      sb_append(&sb, "{\"ok\":false,\"error\":\"duplicate_key\"}");
    } else {
      sb_append(&sb, "{\"ok\":true}");
    }
    *out_json = sb.buf;
    free(ib.buffer);
    return 0;
  }

  if (st.type == STATEMENT_SELECT) {
    StrBuf sb;
    sb_init(&sb);
    sb_append(&sb, "{\"ok\":true,\"rows\":[");
    JsonCtx jctx = {.sb = &sb, .first = 1};
    execute_select_core(&st, table, json_row_handler, &jctx);

    sb_append(&sb, "]}");
    *out_json = sb.buf;
    free(ib.buffer);
    return 0;
  }
  free(ib.buffer);
  return -5;
}

int mydb_execute_json_with_ems(MYDB_Handle h, const char* sql, char** out_json) {
  fprintf(stderr, "[DEBUG-WASM] mydb_execute_json_with_ems called with SQL: %s\n", sql ? sql : "NULL");
  fflush(stderr);

  if (!out_json) {
    fprintf(stderr, "[DEBUG-WASM] out_json parameter is NULL\n");
    fflush(stderr);
    return -1;
  }
  *out_json = NULL;
  if (!h || !sql) {
    fprintf(stderr, "[DEBUG-WASM] Invalid parameters: h=%p, sql=%p\n", (void*)h, (void*)sql);
    fflush(stderr);
    return -2;
  }
  Table* table = (Table*)h;
  fprintf(stderr, "[DEBUG-WASM] Table handle: root_page_num=%u\n", table->root_page_num);
  fflush(stderr);

  InputBuffer ib;
  ib.buffer = strdup(sql);
  ib.buffer_length = strlen(ib.buffer) + 1;
  ib.input_length = (ssize_t)strlen(ib.buffer);

  Statement st;
  memset(&st, 0, sizeof(st));
  st.where_ast = NULL;
  fprintf(stderr, "[DEBUG-WASM] About to prepare statement\n");
  fflush(stderr);
  PrepareResult pr = prepare_statement(&ib, &st, table);
  fprintf(stderr, "[DEBUG-WASM] prepare_statement returned: %d\n", pr);
  fflush(stderr);

  if (pr == PREPARE_SYNTAX_ERROR) {
    fprintf(stderr, "[DEBUG-WASM] Syntax error in SQL\n");
    fflush(stderr);
    free(ib.buffer);
    return -3;
  }
  if (pr == PREPARE_CREATE_TABLE_DONE) {
    fprintf(stderr, "[DEBUG-WASM] CREATE TABLE executed\n");
    fflush(stderr);
    StrBuf sb;
    sb_init(&sb);
    sb_append(&sb, "{\"ok\":true,\"message\":\"Executed.\"}");
    *out_json = sb.buf;
    if (table && table->pager && table->pager->filename) {
      fprintf(stderr, "[DEBUG-WASM] Persisting with filename: %s\n", table->pager->filename);
      fflush(stderr);
      ems_persist_flush(table);
      save_schemas_for_db(table->pager->filename);
      ems_sync_to_idb();
    } else {
      fprintf(stderr, "[DEBUG-WASM] Persisting without filename\n");
      fflush(stderr);
      ems_persist_flush(table);
      ems_sync_to_idb();
    }
    free(ib.buffer);
    return 0;
  }

  if (pr != PREPARE_SUCCESS) {
    fprintf(stderr, "[DEBUG-WASM] Prepare failed with code: %d\n", pr);
    fflush(stderr);
    free(ib.buffer);
    return -4;
  }

  if (st.type == STATEMENT_INSERT || st.type == STATEMENT_DELETE) {
    fprintf(stderr, "[DEBUG-WASM] About to execute %s statement\n",
            st.type == STATEMENT_INSERT ? "INSERT" : "DELETE");
    fflush(stderr);
    fprintf(stderr, "[DEBUG-WASM] Table state before execute: root_page_num=%u\n", table->root_page_num);
    fflush(stderr);

    ExecuteResult er = execute_statement(&st, table);
    fprintf(stderr, "[DEBUG-WASM] execute_statement returned: %d\n", er);
    fflush(stderr);

    StrBuf sb;
    sb_init(&sb);
    if (er == EXECUTE_DUPLICATE_KEY) {
      fprintf(stderr, "[DEBUG-WASM] Duplicate key error\n");
      fflush(stderr);
      sb_append(&sb, "{\"ok\":false,\"error\":\"duplicate_key\"}");
    } else {
      fprintf(stderr, "[DEBUG-WASM] Execution successful\n");
      fflush(stderr);
      sb_append(&sb, "{\"ok\":true}");
    }
    *out_json = sb.buf;
    if (er != EXECUTE_DUPLICATE_KEY) {
      fprintf(stderr, "[DEBUG-WASM] Starting persistence...\n");
      fflush(stderr);
      if (table && table->pager && table->pager->filename) {
        fprintf(stderr, "[DEBUG-WASM] Persisting to file: %s\n", table->pager->filename);
        fflush(stderr);
        ems_persist_flush(table);
        save_schemas_for_db(table->pager->filename);
        ems_sync_to_idb();
      } else {
        fprintf(stderr, "[DEBUG-WASM] Persisting without filename\n");
        fflush(stderr);
        ems_persist_flush(table);
        ems_sync_to_idb();
      }
      fprintf(stderr, "[DEBUG-WASM] Persistence completed\n");
      fflush(stderr);
    } else {
      fprintf(stderr, "[DEBUG-WASM] Skipping persistence due to duplicate key\n");
      fflush(stderr);
    }
    free(ib.buffer);
    return 0;
  }

  if (st.type == STATEMENT_SELECT) {
    StrBuf sb;
    sb_init(&sb);
    sb_append(&sb, "{\"ok\":true,\"rows\":[");
    JsonCtx jctx = {.sb = &sb, .first = 1};
    execute_select_core(&st, table, json_row_handler, &jctx);

    sb_append(&sb, "]}");
    *out_json = sb.buf;
    free(ib.buffer);
    return 0;
  }
  free(ib.buffer);
  return -5;
}

