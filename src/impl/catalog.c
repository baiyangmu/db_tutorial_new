#include "../include/catalog.h"
#include "../include/btree.h"
#include "../include/util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Forward declarations */
extern void initialize_leaf_node(void* node);
extern void set_node_root(void* node, bool is_root);

/* Initialize catalog in page 0 */
void catalog_init(Pager* pager) {
  void* page0 = get_page(pager, 0);
  memset(page0, 0, MYDB_PAGE_SIZE);
  CatalogHeader* hdr = (CatalogHeader*)page0;
  hdr->magic = DB_MAGIC;
  hdr->version = 2; /* Version 2 supports embedded schemas */
  hdr->num_tables = 0;
  hdr->schemas_start_page = INVALID_PAGE_NUM;
  hdr->schemas_alloc_pages = 0;
  hdr->schemas_byte_len = 0;
  hdr->schemas_checksum = 0;
}

/* Get catalog header from page 0 */
CatalogHeader* catalog_header(Pager* pager) {
  return (CatalogHeader*)get_page(pager, 0);
}

/* Get catalog entries array */
CatalogEntry* catalog_entries(Pager* pager) {
  return (CatalogEntry*)((uint8_t*)get_page(pager, 0) + sizeof(CatalogHeader));
}

/* Find table by name in catalog */
int catalog_find(Pager* pager, const char* name) {
  CatalogHeader* hdr = catalog_header(pager);
  CatalogEntry* ents = catalog_entries(pager);
  for (uint32_t i = 0; i < hdr->num_tables; i++) {
    if (strncmp(ents[i].name, name, MAX_TABLE_NAME_LEN) == 0) {
      return i;
    }
  }
  return -1;
}

/* Lookup table schema by name */
int lookup_table_schema(Pager* pager, const char* name, TableSchema* out_schema) {
  int idx = catalog_find(pager, name);
  if (idx < 0) {
    return -1;
  }
  CatalogEntry* ents = catalog_entries(pager);
  uint32_t sidx = ents[idx].schema_index;
  if (sidx >= g_num_tables) {
    return -1;
  }
  *out_schema = g_table_schemas[sidx];
  return 0;
}

/* Add a table to the catalog */
int catalog_add_table(Pager* pager, const TableSchema* schema, uint32_t root_page_num) {
  CatalogHeader* hdr = catalog_header(pager);
  if (hdr->num_tables >= CATALOG_MAX_TABLES) {
    return -1;
  }
  CatalogEntry* ents = catalog_entries(pager);
  CatalogEntry* e = &ents[hdr->num_tables];

  memset(e, 0, sizeof(*e));
  strncpy(e->name, schema->name, MAX_TABLE_NAME_LEN - 1);
  e->root_page_num = root_page_num;
  e->schema_index = 0; /* Will be set by caller */
  hdr->num_tables++;
  return 0;
}

/* Load schemas from database file */
void load_schemas_for_db(const char* dbfile) {
  char* loaded = NULL;
  if (dbfile) {
    Pager* tmp_pager = pager_open(dbfile);
    if (tmp_pager) {
      CatalogHeader* hdr = (CatalogHeader*)get_page(tmp_pager, 0);
      if (hdr && hdr->version >= 2 && hdr->schemas_start_page != INVALID_PAGE_NUM && hdr->schemas_byte_len > 0) {
        /* Read blob from pages */
        uint32_t start = hdr->schemas_start_page;
        uint32_t bytes = hdr->schemas_byte_len;
        uint32_t pages = (bytes + MYDB_PAGE_SIZE - 1) / MYDB_PAGE_SIZE;
        char* buf = malloc(bytes + 1);
        if (buf) {
          uint32_t have = 0;
          for (uint32_t i = 0; i < pages; ++i) {
            void* p = get_page(tmp_pager, start + i);
            uint32_t want = (bytes - have) > MYDB_PAGE_SIZE ? MYDB_PAGE_SIZE : (bytes - have);
            memcpy(buf + have, p, want);
            have += want;
          }
          buf[bytes] = '\0';
          loaded = buf;
        }
      }
      /* Close pager */
      close(tmp_pager->file_descriptor);
      if (tmp_pager->filename) free(tmp_pager->filename);
      free(tmp_pager);
    }
  }
  if (!loaded) return;
  parse_schemas_from_str(loaded);
  free(loaded);
}

/* Serialize schemas to string */
static char* serialize_schemas(void) {
  size_t cap = 4096;
  char* buf = malloc(cap);
  if (!buf) return NULL;
  size_t off = 0;
  off += snprintf(buf + off, cap - off, "%u\n", (unsigned int)g_num_tables);
  for (uint32_t i = 0; i < g_num_tables && i < MAX_TABLES; ++i) {
    TableSchema* sc = &g_table_schemas[i];
    off += snprintf(buf + off, cap - off, "%s\n", sc->name);
    off += snprintf(buf + off, cap - off, "%u\n", (unsigned int)sc->num_columns);
    for (uint32_t j = 0; j < sc->num_columns; ++j) {
      ColumnDef* c = &sc->columns[j];
      off += snprintf(buf + off, cap - off, "%s\t%u\t%u\n", c->name, (unsigned int)c->type, (unsigned int)c->size);
      if (off + 256 > cap) {
        cap *= 2;
        char* nb = realloc(buf, cap);
        if (!nb) {
          free(buf);
          return NULL;
        }
        buf = nb;
      }
    }
  }
  return buf;
}

/* Save schemas to database file */
int save_schemas_for_db(const char* dbfile) {
  char* s = serialize_schemas();
  if (!s) return -1;
  int rc = -1;
  if (dbfile) {
    Pager* pager = pager_open(dbfile);
    if (!pager) {
      free(s);
      return -1;
    }
    CatalogHeader* hdr = (CatalogHeader*)get_page(pager, 0);
    uint32_t old_start = hdr->schemas_start_page;
    uint32_t old_alloc = hdr->schemas_alloc_pages;
    uint32_t bytes = (uint32_t)strlen(s);
    uint32_t needed = (bytes + MYDB_PAGE_SIZE - 1) / MYDB_PAGE_SIZE;
    uint32_t start = INVALID_PAGE_NUM;
    if (old_start != INVALID_PAGE_NUM && needed <= old_alloc) {
      start = old_start; /* Overwrite in-place */
    } else {
      start = get_unused_page_num(pager); /* Append */
    }
    for (uint32_t i = 0; i < needed; ++i) {
      void* p = get_page(pager, start + i);
      uint32_t off = i * MYDB_PAGE_SIZE;
      uint32_t to_copy = (bytes > off) ? (uint32_t)((bytes - off) < MYDB_PAGE_SIZE ? (bytes - off) : MYDB_PAGE_SIZE) : 0;
      memset(p, 0, MYDB_PAGE_SIZE);
      if (to_copy) memcpy(p, s + off, to_copy);
      pager_flush(pager, start + i);
    }
    hdr->schemas_start_page = start;
    hdr->schemas_alloc_pages = needed;
    hdr->schemas_byte_len = bytes;
    hdr->schemas_checksum = 0;
    hdr->version = 2;
    pager_flush(pager, 0);
    /* Close pager and free pages */
    for (uint32_t i = 0; i < pager->num_pages; ++i) {
      if (pager->pages[i]) {
        free(pager->pages[i]);
        pager->pages[i] = NULL;
      }
    }
    int res = close(pager->file_descriptor);
    (void)res;
    if (pager->filename) free(pager->filename);
    free(pager);
    rc = 0;
  }
  free(s);
  return rc;
}

/* Handle CREATE TABLE statement */
int handle_create_table_ex(Table* runtime_table, const char* sql) {
  const char* p = strstr(sql, "table");
  if (!p) {
    return -1;
  }

  p += 5;
  while (*p == ' ') p++;

  char table_name[MAX_TABLE_NAME_LEN] = {0};
  sscanf(p, "%s", table_name);

  p = strchr(p, '(');
  if (!p) {
    return -1;
  }
  p++;

  TableSchema schema = (TableSchema){0};
  strncpy(schema.name, table_name, MAX_TABLE_NAME_LEN - 1);

  char coldef[256];
  int col = 0;

  while (sscanf(p, " %[^,)]", coldef) == 1 && col < MAX_COLUMNS) {
    char colname[MAX_COLUMN_NAME_LEN], coltype[16];

    if (sscanf(coldef, "%s %s", colname, coltype) != 2) {
      break;
    }

    strncpy(schema.columns[col].name, colname, MAX_COLUMN_NAME_LEN - 1);
    schema.columns[col].type = parse_column_type(coltype);
    if (schema.columns[col].type == COL_TYPE_STRING) {
      schema.columns[col].size = 255;
    } else if (schema.columns[col].type == COL_TYPE_TIMESTAMP) {
      schema.columns[col].size = 8;
    } else {
      schema.columns[col].size = 4;
    }

    schema.num_columns++;
    col++;

    p = strchr(p, ',');
    if (!p) break;
    p++;
  }

  if (schema.num_columns == 0) {
    return -2;
  }

  /* Check duplicate */
  if (catalog_find(runtime_table->pager, schema.name) >= 0) {
    return -4;
  }

  /* Allocate root page */
  uint32_t root = get_unused_page_num(runtime_table->pager);
  void* root_node = get_page(runtime_table->pager, root);
  initialize_leaf_node(root_node);
  set_node_root(root_node, true);

  /* Store schema globally */
  if (g_num_tables >= MAX_TABLES) {
    return -5;
  }
  g_table_schemas[g_num_tables] = schema;
  uint32_t schema_idx = g_num_tables;
  g_num_tables++;

  if (catalog_add_table(runtime_table->pager, &schema, root) != 0) {
    return -5;
  }
  /* Update schema index */
  CatalogHeader* hdr = catalog_header(runtime_table->pager);
  CatalogEntry* ents = catalog_entries(runtime_table->pager);
  if (hdr && hdr->num_tables > 0) {
    uint32_t last = hdr->num_tables - 1;
    ents[last].schema_index = schema_idx;
  }

  printf("Table '%s' created with %d columns.\n", schema.name, schema.num_columns);
  /* Persist schemas */
  if (runtime_table && runtime_table->pager && runtime_table->pager->filename) {
    pager_flush(runtime_table->pager, 0);
    save_schemas_for_db(runtime_table->pager->filename);
  }
  return 0;
}

