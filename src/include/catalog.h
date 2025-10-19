#ifndef MYDB_CATALOG_H
#define MYDB_CATALOG_H

#include <stdint.h>
#include "pager.h"
#include "schema.h"

/* Database magic number and constants */
#define DB_MAGIC 0x44544231  /* "DTB1" */
#define CATALOG_MAX_TABLES 32

/* Catalog header */
typedef struct {
  uint32_t magic;
  uint32_t version;      /* Initial 1, >=2 means embedded schema blob support */
  uint32_t num_tables;
  /* Schema blob pointer info (page-relative) */
  uint32_t schemas_start_page;
  uint32_t schemas_alloc_pages;
  uint32_t schemas_byte_len;
  uint32_t schemas_checksum;
} CatalogHeader;

/* Catalog entry */
typedef struct {
  char name[MAX_TABLE_NAME_LEN];
  uint32_t root_page_num;
  uint32_t schema_index;
} CatalogEntry;

/* Catalog operations */
void catalog_init(Pager* pager);
CatalogHeader* catalog_header(Pager* pager);
CatalogEntry* catalog_entries(Pager* pager);
int catalog_find(Pager* pager, const char* name);
int catalog_add_table(Pager* pager, const TableSchema* schema, uint32_t root_page_num);
int lookup_table_schema(Pager* pager, const char* name, TableSchema* out_schema);

/* Schema persistence */
void load_schemas_for_db(const char* dbfile);
int save_schemas_for_db(const char* dbfile);

/* CREATE TABLE handler */
int handle_create_table_ex(Table* runtime_table, const char* sql);

#endif /* MYDB_CATALOG_H */

