#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <limits.h>
#include "sql_parser.h"
#include "sql_ast.h"

static Expr* g_last_parsed_expr = NULL;

static void set_last_parsed_expr(Expr* e){
  if(g_last_parsed_expr){
    expr_free(g_last_parsed_expr);
  }
  g_last_parsed_expr = e;
}

typedef struct {
  char* buffer;
  size_t buffer_length;
  ssize_t input_length;
} InputBuffer;

typedef enum {
  EXECUTE_SUCCESS,
  EXECUTE_DUPLICATE_KEY,
} ExecuteResult;

typedef enum {
  META_COMMAND_SUCCESS,
  META_COMMAND_UNRECOGNIZED_COMMAND
} MetaCommandResult;

typedef enum {
  PREPARE_SUCCESS,
  PREPARE_NEGATIVE_ID,
  PREPARE_STRING_TOO_LONG,
  PREPARE_SYNTAX_ERROR,
  PREPARE_UNRECOGNIZED_STATEMENT,
  PREPARE_CREATE_TABLE_DONE
} PrepareResult;

typedef enum { STATEMENT_INSERT, STATEMENT_SELECT } StatementType;

#define COLUMN_USERNAME_SIZE 32
#define COLUMN_EMAIL_SIZE 255
typedef struct {
  uint32_t id;
  char username[COLUMN_USERNAME_SIZE + 1];
  char email[COLUMN_EMAIL_SIZE + 1];
} Row;


/* define MAX_VALUES after schema constants */
#define MAX_VALUES MAX_COLUMNS
/* schema constants and typedefs MUST appear before Table/Statement */
#define MAX_TABLES 32
#define MAX_COLUMNS 16
#define MAX_COLUMN_NAME_LEN 32
#define MAX_TABLE_NAME_LEN 32

typedef enum {
  COL_TYPE_INT,
  COL_TYPE_STRING
} ColumType;

typedef struct {
  char name[MAX_COLUMN_NAME_LEN];
  ColumType type;
  uint32_t size; // 仅对string有用
} ColumnDef;

typedef struct {
  char name[MAX_TABLE_NAME_LEN];
  uint32_t num_columns;
  ColumnDef columns[MAX_COLUMNS];
} TableSchema;


#define TABLE_MAX_PAGES 400

typedef struct {
  int file_descriptor;
  uint32_t file_length;
  uint32_t num_pages;
  void* pages[TABLE_MAX_PAGES];
} Pager;


typedef struct {
  Pager* pager;
  uint32_t root_page_num;

  TableSchema active_schema;
  uint32_t row_size;
} Table;

/* define MAX_VALUES after schema constants */
#define MAX_VALUES MAX_COLUMNS
#define MAX_SELECT_COLS MAX_COLUMNS

typedef struct {
  StatementType type;
  uint32_t num_values;
  char* values[MAX_VALUES];

  char target_table[MAX_TABLE_NAME_LEN];

  //兼容旧实现（可保留不用）
  Row row_to_insert;  // only used by insert statement

  uint32_t proj_count; //0 表示*
  int proj_indices[MAX_SELECT_COLS];

  bool has_where;
  int where_col_index;
  bool where_is_string;
  int where_int;
  char where_str[256]; // 拷贝一份用于比较

} Statement;

// Forward decls needed by prepare_select
int lookup_table_schema(Pager* pager, const char* name, TableSchema* out_schema);

// helper 
static int schema_col_index(const TableSchema* s,const char* name){
  for (uint32_t i = 0 ; i < s->num_columns ; i++){
    if(strncmp(s->columns[i].name , name , MAX_COLUMN_NAME_LEN) == 0){
      return (int)i;
    }
  }
  return -1;
}


static uint32_t schema_col_offset(const TableSchema* s, int col_idx){
  uint32_t off = 0;
  for(int i = 0 ; i < col_idx ; i++){
    off += (s->columns[i].type == COL_TYPE_INT) ? 4 : s->columns[i].size;
  }
  return off;
}


static int row_get_int(Table* t,const void* row,int col_idx){
  uint32_t off = schema_col_offset(&t->active_schema,col_idx);
  int v;
  memcpy(&v,(const uint8_t*)row + off,4);
  return v;
}


static void row_get_string(Table* t,const void* row, int col_idx, char* out,size_t cap){
  uint32_t off = schema_col_offset(&t->active_schema,col_idx);
  size_t sz = t->active_schema.columns[col_idx].size;
  size_t n = (sz < cap - 1) ? sz : (cap - 1);
  memcpy(out, (const uint8_t*)row + off, n);
  out[n] = 0;
  while (n > 0 && out[n-1] == 0){
    n--;
  }
  out[n] = 0;
}


static bool row_matches_where(Table* t,const void* row, const Statement* st){
  if (!st->has_where) {
    return true;
  }
  if(st->where_is_string){
    char buf[512];
    row_get_string(t,row,st->where_col_index,buf,sizeof(buf));
    return strcmp(buf,st->where_str) == 0;
  } else {
    int v = row_get_int(t,row,st->where_col_index);
    return v == st->where_int;
  }
}


static void print_row_dynamic(Table* t,const void* src){
  const uint8_t* p = (const uint8_t*)src;
  printf("(");
  for(uint32_t i = 0 ; i < t->active_schema.num_columns ; i++){
    const ColumnDef* c = &t->active_schema.columns[i];
    if(i){
      printf(", ");
    }

    if(c->type == COL_TYPE_INT){
      int v;
      memcpy(&v,p,4);
      p+=4;
      printf("%d",v);
    } else {
      char buf[512];
      size_t m = c->size < sizeof(buf)-1? c->size : sizeof(buf)-1;
      memcpy(buf,p,m);
      buf[m]=0;
      size_t r = m;
      while (r > 0 && buf[r - 1] == 0) {
        r--;
      }
      buf[r] = 0;
      printf("%s",buf);
      p += c->size;
    }
  }
  printf(")\n");
}



static void print_row_projected(Table* t, const void* row, const int* idxs, uint32_t n){
  if(n == 0){
    print_row_dynamic(t,row);
    return;
  }
  printf("(");
  for(uint32_t i = 0 ; i < n ; i++){
    if(i){
      printf(",");
    }
    const ColumnDef* c = &t->active_schema.columns[idxs[i]];
    if(c->type == COL_TYPE_INT){
      int v = row_get_int(t,row,idxs[i]);
      printf("%d",v);
    }else{
      char s[512];
      row_get_string(t,row,idxs[i],s,sizeof(s));
      printf("%s",s);
    }
  }
  printf(")\n");
}



static int parse_int(const char* s,int* out){
  char* end = NULL;
  long v = strtol(s,&end,10);
  if(end == s || *end != '\0'){
    return -1;
  }
  if(v < INT32_MIN || v > INT32_MAX){
    return -1;
  }
  *out = (int)v;
  return 0;
}


//在不使用 ast 解析的情况下，对特定语句进行直接解析
PrepareResult prepare_select(InputBuffer* in, Statement* st, Table* table) {
  // Initialize statement
  st->type = STATEMENT_SELECT;
  st->target_table[0] = '\0';
  st->proj_count = 0;
  st->has_where = false;

  // Trim leading spaces
  char* s = in->buffer;
  while (*s == ' ' || *s == '\t') {
    s++;
  }

  // Skip keyword "select"
  s += 6;
  while (*s == ' ' || *s == '\t') {
    s++;
  }

  // Decide schema for SELECT (FROM table overrides current active_schema)
  char* proj_start = s;
  char* from_kw = strstr(proj_start, " from ");
  char* where_kw = strstr(proj_start, " where ");

  const TableSchema* schema_for_select = &table->active_schema;
  char from_table_buf[MAX_TABLE_NAME_LEN] = {0};
  if (from_kw != NULL) {
    // Parse table name after " from " to choose schema for projection/where
    char* p = from_kw + 6; // skip leading space + 'from '
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
    schema_for_select = &tmp; // use temp schema for validation
    // Record target table for execution time switch
    strncpy(st->target_table, from_table_buf, MAX_TABLE_NAME_LEN - 1);
    st->target_table[MAX_TABLE_NAME_LEN - 1] = '\0';
  }

  // Parse projection: "*" or "col1,col2,..." (validated against schema_for_select)
  if (*s == '*') {
    // "*" means proj_count = 0 (print all columns)
    s++;
  } else {
    // Determine the slice of projection list before FROM/WHERE/end
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

    // Split by commas
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

    // After projection slice, move s to WHERE if present, otherwise to proj_end
    s = (where_kw != NULL) ? where_kw : proj_end;
  }

  // Optional: WHERE <col> = <val>
  while (*s == ' ' || *s == '\t') {
    s++;
  }
  if (strncmp(s, "where ", 6) == 0) {
    s += 6;
    while (*s == ' ' || *s == '\t') {
      s++;
    }

    // Parse column name up to '='
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

    // Parse value token (no spaces or quotes supported)
    char val[256];
    int vi = 0;
    while (*s && *s != ' ' && *s != '\t') {
      if (vi < (int)sizeof(val) - 1) {
        val[vi++] = *s;
      }
      s++;
    }
    val[vi] = '\0';

    // Map column and store predicate (use schema_for_select)
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




#define size_of_attribute(Struct, Attribute) sizeof(((Struct*)0)->Attribute)

const uint32_t ID_SIZE = size_of_attribute(Row, id);
const uint32_t USERNAME_SIZE = size_of_attribute(Row, username);
const uint32_t EMAIL_SIZE = size_of_attribute(Row, email);
const uint32_t ID_OFFSET = 0;
const uint32_t USERNAME_OFFSET = ID_OFFSET + ID_SIZE;
const uint32_t EMAIL_OFFSET = USERNAME_OFFSET + USERNAME_SIZE;
const uint32_t ROW_SIZE = ID_SIZE + USERNAME_SIZE + EMAIL_SIZE;

const uint32_t PAGE_SIZE = 4096;

#define INVALID_PAGE_NUM UINT32_MAX

/* schema types already defined above */



static inline uint32_t compute_row_size(const TableSchema* s){
  uint32_t sz = 0;
  for(uint32_t i = 0 ; i < s->num_columns ; i++){
    switch(s->columns[i].type){
      case COL_TYPE_INT: sz += 4;break;
      case COL_TYPE_STRING: sz += s->columns[i].size; break;
    }
  }
  return sz;
}


// 叶子节点按表计算的尺寸
static inline uint32_t leaf_value_size(Table* t){
  return t->row_size;
}

static inline uint32_t leaf_cell_size(Table* t){
  return sizeof(uint32_t) + leaf_value_size(t);
}

// Forward declare constant defined later in file
extern const uint32_t LEAF_NODE_HEADER_SIZE;
static inline int32_t leaf_space_for_cells(void){
  return PAGE_SIZE - LEAF_NODE_HEADER_SIZE;
}

static inline uint32_t leaf_max_cells(Table* t){
  return leaf_space_for_cells() / leaf_cell_size(t);
}

static inline uint32_t leaf_right_split_count(Table* t){
  return (leaf_max_cells(t) + 1) /2;
}

static inline uint32_t leaf_left_split_count(Table* t){
  return (leaf_max_cells(t) + 1) - leaf_right_split_count(t);
}


static inline void* leaf_cell_t(Table* t,void* node,uint32_t cell_num){
  return (uint8_t*)node + LEAF_NODE_HEADER_SIZE + cell_num * leaf_cell_size(t);
}


static inline uint32_t* leaf_key_t(Table* t,void* node,uint32_t cell_num){
  return (uint32_t*)leaf_cell_t(t,node,cell_num);
}

static inline void* leaf_value_t(Table* t,void* node,uint32_t cell_num){
  return leaf_cell_t(t,node,cell_num) + sizeof(uint32_t);
}






void* get_page(Pager* pager, uint32_t page_num);
void initialize_leaf_node(void* node);
void set_node_root(void* node, bool is_root);
uint32_t get_unused_page_num(Pager* pager);
static void serialize_row_dynamic(Table* t, char* const* values, uint32_t n, void* dest);
static void print_row_dynamic(Table* t, const void* src);
// lookup helper used by prepare_select; implemented after catalog types
int lookup_table_schema(Pager* pager, const char* name, TableSchema* out_schema);


/* duplicated defines removed */


#define DB_MAGIC 0x44544231  // "DTB1"
#define CATALOG_MAX_TABLES 32

typedef struct {
  uint32_t magic;
  uint32_t version;      // 初始 1
  uint32_t num_tables;
} CatalogHeader;



/* duplicated typedefs removed */

typedef struct {
  char name[MAX_TABLE_NAME_LEN];  // 32
  uint32_t root_page_num;
  TableSchema schema;             // 现有结构，可先保留
} CatalogEntry;


static void catalog_init(Pager* pager){
   void* page0 = get_page(pager,0);
   memset(page0,0,PAGE_SIZE);
   CatalogHeader* hdr = (CatalogHeader*)page0;
   hdr->magic = DB_MAGIC;
   hdr->version = 1;
   hdr->num_tables = 0;
}

static CatalogHeader* catalog_header(Pager* pager){
  return (CatalogHeader*)get_page(pager,0);
}


static CatalogEntry* catalog_entries(Pager* pager){
  return (CatalogEntry*)((uint8_t*)get_page(pager,0) + sizeof(CatalogHeader));
}


static int catalog_find(Pager* pager, const char* name){
  CatalogHeader* hdr = catalog_header(pager);
  CatalogEntry* ents = catalog_entries(pager);
  for(uint32_t i = 0 ; i < hdr->num_tables; i++){
    if(strncmp(ents[i].name,name,MAX_TABLE_NAME_LEN) == 0){
      return i;
    }
  }
  return -1;
}

// non-static helper for early use (we declared a prototype earlier)
int lookup_table_schema(Pager* pager, const char* name, TableSchema* out_schema){
  int idx = catalog_find(pager, name);
  if (idx < 0) {
    return -1;
  }
  CatalogEntry* ents = catalog_entries(pager);
  *out_schema = ents[idx].schema;
  return 0;
}

static int catalog_add_table(Pager* pager, const TableSchema* schema,uint32_t root_page_num){
  CatalogHeader* hdr = catalog_header(pager);
  if(hdr->num_tables >= CATALOG_MAX_TABLES){
    return -1;
  }
  CatalogEntry* ents = catalog_entries(pager);
  CatalogEntry* e = &ents[hdr->num_tables];

  memset(e,0,sizeof(*e));
  strncpy(e->name,schema->name,MAX_TABLE_NAME_LEN - 1);
  e->root_page_num = root_page_num;
  e->schema = *schema;
  hdr->num_tables++;
  return 0;
}


// 全局表结构数组
TableSchema g_table_schemas[MAX_TABLES];
uint32_t g_num_tables = 0;


ColumType parse_column_type(const char* type_str){
   if(strcmp(type_str,"int") == 0) return COL_TYPE_INT;
    if(strcmp(type_str,"string") == 0) return COL_TYPE_STRING;
    return COL_TYPE_INT; // 默认返回int类型
}

// ADD this new function (keep your parser logic; just append the persistence part at the end)
int handle_create_table_ex(Table* runtime_table, const char* sql) {
  // parse with your existing code (you can call handle_create_table's parsing body)
  // Below is your original body until schema.num_columns check..
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
      schema.columns[col].size = (schema.columns[col].type == COL_TYPE_STRING) ? 255 : 4;

      schema.num_columns++;
      col++;

      p = strchr(p, ',');
      if (!p) break;
      p++;
  }

  if (schema.num_columns == 0) {
    return -2;
  }

  // Optional: ensure schema matches fixed Row layout for now
  if (schema.num_columns != 3) {
      printf("Only 3 columns (id, username, email) supported now.\n");
      return -6;
  }

  // check duplicate
  if (catalog_find(runtime_table->pager, schema.name) >= 0) {
      return -4;
  }

  // allocate root page for this table
  uint32_t root = get_unused_page_num(runtime_table->pager);
  void* root_node = get_page(runtime_table->pager, root);
  initialize_leaf_node(root_node);
  set_node_root(root_node, true);

  // persist into catalog
  if (catalog_add_table(runtime_table->pager, &schema, root) != 0) {
      return -5;
  }

  printf("Table '%s' created with %d columns.\n", schema.name, schema.num_columns);
  return 0;
}


int handle_create_table(const char* sql){
  // 1.解析表名
  const char* p = strstr(sql,"table");
  if(!p){
    return -1;
  }
  p += 5;
  while(*p == ' '){
    p++;
  }
  char table_name[MAX_TABLE_NAME_LEN] = {0};
  sscanf(p,"%s",table_name);

  // 2.找到 '(' 的位置
  p = strchr(p,'(');
  if(!p){
    return -1;
  }

  p++;

  //3.解析字段
  TableSchema schema = {0};
  strncpy(schema.name,table_name,MAX_TABLE_NAME_LEN - 1);

  char coldef[256];
  int col = 0 ;
  while(sscanf(p," %[^,)]",coldef) == 1 && col < MAX_COLUMNS){
    char colname[MAX_COLUMN_NAME_LEN] , coltype[16];
    if(sscanf(coldef,"%s %s", colname,coltype) != 2){
      break;
    }
    strncpy(schema.columns[col].name, colname, MAX_COLUMN_NAME_LEN - 1);
    schema.columns[col].type = parse_column_type(coltype);
    schema.columns[col].size = (schema.columns[col].type == COL_TYPE_STRING) ? 255:4;
    schema.num_columns++;
    col++;
    p = strchr(p,',');
    if(!p){
      break;
    }
    p++;
  }

  if(schema.num_columns == 0){
    return -2;
  }

  //4.保存到全局
  if(g_num_tables >= MAX_TABLES){
    return -3;
  }

  g_table_schemas[g_num_tables++] = schema;
  printf("Table '%s' created with %d columns.\n", schema.name, schema.num_columns);
  return 0;
}



typedef struct {
  Table* table;
  uint32_t page_num;
  uint32_t cell_num;
  bool end_of_table;  // Indicates a position one past the last element
} Cursor;

void print_row(Row* row) {
  printf("(%d, %s, %s)\n", row->id, row->username, row->email);
}

typedef enum { NODE_INTERNAL, NODE_LEAF } NodeType;

/*
 * Common Node Header Layout
 */
const uint32_t NODE_TYPE_SIZE = sizeof(uint8_t);
const uint32_t NODE_TYPE_OFFSET = 0;
const uint32_t IS_ROOT_SIZE = sizeof(uint8_t);
const uint32_t IS_ROOT_OFFSET = NODE_TYPE_SIZE;
const uint32_t PARENT_POINTER_SIZE = sizeof(uint32_t);
const uint32_t PARENT_POINTER_OFFSET = IS_ROOT_OFFSET + IS_ROOT_SIZE;
const uint8_t COMMON_NODE_HEADER_SIZE =
    NODE_TYPE_SIZE + IS_ROOT_SIZE + PARENT_POINTER_SIZE;

/*
 * Internal Node Header Layout
 */
const uint32_t INTERNAL_NODE_NUM_KEYS_SIZE = sizeof(uint32_t);
const uint32_t INTERNAL_NODE_NUM_KEYS_OFFSET = COMMON_NODE_HEADER_SIZE;
const uint32_t INTERNAL_NODE_RIGHT_CHILD_SIZE = sizeof(uint32_t);
const uint32_t INTERNAL_NODE_RIGHT_CHILD_OFFSET =
    INTERNAL_NODE_NUM_KEYS_OFFSET + INTERNAL_NODE_NUM_KEYS_SIZE;
const uint32_t INTERNAL_NODE_HEADER_SIZE = COMMON_NODE_HEADER_SIZE +
                                           INTERNAL_NODE_NUM_KEYS_SIZE +
                                           INTERNAL_NODE_RIGHT_CHILD_SIZE;

/*
 * Internal Node Body Layout
 */
const uint32_t INTERNAL_NODE_KEY_SIZE = sizeof(uint32_t);
const uint32_t INTERNAL_NODE_CHILD_SIZE = sizeof(uint32_t);
const uint32_t INTERNAL_NODE_CELL_SIZE =
    INTERNAL_NODE_CHILD_SIZE + INTERNAL_NODE_KEY_SIZE;
/* Keep this small for testing */
const uint32_t INTERNAL_NODE_MAX_KEYS = 3;

/*
 * Leaf Node Header Layout
 */
const uint32_t LEAF_NODE_NUM_CELLS_SIZE = sizeof(uint32_t);
const uint32_t LEAF_NODE_NUM_CELLS_OFFSET = COMMON_NODE_HEADER_SIZE;
const uint32_t LEAF_NODE_NEXT_LEAF_SIZE = sizeof(uint32_t);
const uint32_t LEAF_NODE_NEXT_LEAF_OFFSET =
    LEAF_NODE_NUM_CELLS_OFFSET + LEAF_NODE_NUM_CELLS_SIZE;
const uint32_t LEAF_NODE_HEADER_SIZE = COMMON_NODE_HEADER_SIZE +
                                       LEAF_NODE_NUM_CELLS_SIZE +
                                       LEAF_NODE_NEXT_LEAF_SIZE;

/*
 * Leaf Node Body Layout
 */
const uint32_t LEAF_NODE_KEY_SIZE = sizeof(uint32_t);
const uint32_t LEAF_NODE_KEY_OFFSET = 0;
const uint32_t LEAF_NODE_VALUE_SIZE = ROW_SIZE;
const uint32_t LEAF_NODE_VALUE_OFFSET =
    LEAF_NODE_KEY_OFFSET + LEAF_NODE_KEY_SIZE;
const uint32_t LEAF_NODE_CELL_SIZE = LEAF_NODE_KEY_SIZE + LEAF_NODE_VALUE_SIZE;
const uint32_t LEAF_NODE_SPACE_FOR_CELLS = PAGE_SIZE - LEAF_NODE_HEADER_SIZE;
const uint32_t LEAF_NODE_MAX_CELLS =
    LEAF_NODE_SPACE_FOR_CELLS / LEAF_NODE_CELL_SIZE;
const uint32_t LEAF_NODE_RIGHT_SPLIT_COUNT = (LEAF_NODE_MAX_CELLS + 1) / 2;
const uint32_t LEAF_NODE_LEFT_SPLIT_COUNT =
    (LEAF_NODE_MAX_CELLS + 1) - LEAF_NODE_RIGHT_SPLIT_COUNT;

NodeType get_node_type(void* node) {
  uint8_t value = *((uint8_t*)(node + NODE_TYPE_OFFSET));
  return (NodeType)value;
}

void set_node_type(void* node, NodeType type) {
  uint8_t value = type;
  *((uint8_t*)(node + NODE_TYPE_OFFSET)) = value;
}

bool is_node_root(void* node) {
  uint8_t value = *((uint8_t*)(node + IS_ROOT_OFFSET));
  return (bool)value;
}

void set_node_root(void* node, bool is_root) {
  uint8_t value = is_root;
  *((uint8_t*)(node + IS_ROOT_OFFSET)) = value;
}

uint32_t* node_parent(void* node) { return node + PARENT_POINTER_OFFSET; }

uint32_t* internal_node_num_keys(void* node) {
  return node + INTERNAL_NODE_NUM_KEYS_OFFSET;
}

uint32_t* internal_node_right_child(void* node) {
  return node + INTERNAL_NODE_RIGHT_CHILD_OFFSET;
}

uint32_t* internal_node_cell(void* node, uint32_t cell_num) {
  return node + INTERNAL_NODE_HEADER_SIZE + cell_num * INTERNAL_NODE_CELL_SIZE;
}

uint32_t* internal_node_child(void* node, uint32_t child_num) {
  uint32_t num_keys = *internal_node_num_keys(node);
  if (child_num > num_keys) {
    printf("Tried to access child_num %d > num_keys %d\n", child_num, num_keys);
    exit(EXIT_FAILURE);
  } else if (child_num == num_keys) {
    uint32_t* right_child = internal_node_right_child(node);
    if (*right_child == INVALID_PAGE_NUM) {
      printf("Tried to access right child of node, but was invalid page\n");
      exit(EXIT_FAILURE);
    }
    return right_child;
  } else {
    uint32_t* child = internal_node_cell(node, child_num);
    if (*child == INVALID_PAGE_NUM) {
      printf("Tried to access child %d of node, but was invalid page\n", child_num);
      exit(EXIT_FAILURE);
    }
    return child;
  }
}

uint32_t* internal_node_key(void* node, uint32_t key_num) {
  return (void*)internal_node_cell(node, key_num) + INTERNAL_NODE_CHILD_SIZE;
}

uint32_t* leaf_node_num_cells(void* node) {
  return node + LEAF_NODE_NUM_CELLS_OFFSET;
}

uint32_t* leaf_node_next_leaf(void* node) {
  return node + LEAF_NODE_NEXT_LEAF_OFFSET;
}

void* leaf_node_cell(void* node, uint32_t cell_num) {
  return node + LEAF_NODE_HEADER_SIZE + cell_num * LEAF_NODE_CELL_SIZE;
}

uint32_t* leaf_node_key(void* node, uint32_t cell_num) {
  return leaf_node_cell(node, cell_num);
}

void* leaf_node_value(void* node, uint32_t cell_num) {
  return leaf_node_cell(node, cell_num) + LEAF_NODE_KEY_SIZE;
}

void* get_page(Pager* pager, uint32_t page_num) {
  if (page_num > TABLE_MAX_PAGES) {
    printf("Tried to fetch page number out of bounds. %d > %d\n", page_num,
           TABLE_MAX_PAGES);
    exit(EXIT_FAILURE);
  }

  if (pager->pages[page_num] == NULL) {
    // Cache miss. Allocate memory and load from file.
    void* page = malloc(PAGE_SIZE);
    uint32_t num_pages = pager->file_length / PAGE_SIZE;

    // We might save a partial page at the end of the file
    if (pager->file_length % PAGE_SIZE) {
      num_pages += 1;
    }

    if (page_num <= num_pages) {
      lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
      ssize_t bytes_read = read(pager->file_descriptor, page, PAGE_SIZE);
      if (bytes_read == -1) {
        printf("Error reading file: %d\n", errno);
        exit(EXIT_FAILURE);
      }
    }

    pager->pages[page_num] = page;

    if (page_num >= pager->num_pages) {
      pager->num_pages = page_num + 1;
    }
  }

  return pager->pages[page_num];
}

uint32_t get_node_max_key(Pager* pager, void* node) {
  if (get_node_type(node) == NODE_LEAF) {
    return *leaf_node_key(node, *leaf_node_num_cells(node) - 1);
  }
  void* right_child = get_page(pager,*internal_node_right_child(node));
  return get_node_max_key(pager, right_child);
}

void print_constants(Table* t) {
  printf("ROW_SIZE(table): %u\n", t->row_size);
  printf("LEAF_NODE_CELL_SIZE(table): %u\n", leaf_cell_size(t));
  printf("LEAF_NODE_MAX_CELLS(table): %u\n", leaf_max_cells(t));
}

void indent(uint32_t level) {
  for (uint32_t i = 0; i < level; i++) {
    printf("  ");
  }
}

void print_tree(Pager* pager, uint32_t page_num, uint32_t indentation_level) {
  void* node = get_page(pager, page_num);
  uint32_t num_keys, child;

  switch (get_node_type(node)) {
    case (NODE_LEAF):
      num_keys = *leaf_node_num_cells(node);
      indent(indentation_level);
      printf("- leaf (size %d)\n", num_keys);
      for (uint32_t i = 0; i < num_keys; i++) {
        indent(indentation_level + 1);
        printf("- %d\n", *leaf_node_key(node, i));
      }
      break;
    case (NODE_INTERNAL):
      num_keys = *internal_node_num_keys(node);
      indent(indentation_level);
      printf("- internal (size %d)\n", num_keys);
      if (num_keys > 0) {
        for (uint32_t i = 0; i < num_keys; i++) {
          child = *internal_node_child(node, i);
          print_tree(pager, child, indentation_level + 1);

          indent(indentation_level + 1);
          printf("- key %d\n", *internal_node_key(node, i));
        }
        child = *internal_node_right_child(node);
        print_tree(pager, child, indentation_level + 1);
      }
      break;
  }
}

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

void initialize_leaf_node(void* node) {
  set_node_type(node, NODE_LEAF);
  set_node_root(node, false);
  *leaf_node_num_cells(node) = 0;
  *leaf_node_next_leaf(node) = 0;  // 0 represents no sibling
}

void initialize_internal_node(void* node) {
  set_node_type(node, NODE_INTERNAL);
  set_node_root(node, false);
  *internal_node_num_keys(node) = 0;
  /*
  Necessary because the root page number is 0; by not initializing an internal 
  node's right child to an invalid page number when initializing the node, we may
  end up with 0 as the node's right child, which makes the node a parent of the root
  */
  *internal_node_right_child(node) = INVALID_PAGE_NUM;
}

Cursor* leaf_node_find(Table* table, uint32_t page_num, uint32_t key) {
  void* node = get_page(table->pager, page_num);
  uint32_t num_cells = *leaf_node_num_cells(node);

  Cursor* cursor = malloc(sizeof(Cursor));
  cursor->table = table;
  cursor->page_num = page_num;
  cursor->end_of_table = false;

  // Binary search
  uint32_t min_index = 0;
  uint32_t one_past_max_index = num_cells;
  while (one_past_max_index != min_index) {
    uint32_t index = (min_index + one_past_max_index) / 2;
    uint32_t key_at_index = *leaf_key_t(table, node, index);
    if (key == key_at_index) {
      cursor->cell_num = index;
      return cursor;
    }
    if (key < key_at_index) {
      one_past_max_index = index;
    } else {
      min_index = index + 1;
    }
  }

  cursor->cell_num = min_index;
  return cursor;
}

uint32_t internal_node_find_child(void* node, uint32_t key) {
  /*
  Return the index of the child which should contain
  the given key.
  */

  uint32_t num_keys = *internal_node_num_keys(node);

  /* Binary search */
  uint32_t min_index = 0;
  uint32_t max_index = num_keys; /* there is one more child than key */

  while (min_index != max_index) {
    uint32_t index = (min_index + max_index) / 2;
    uint32_t key_to_right = *internal_node_key(node, index);
    if (key_to_right >= key) {
      max_index = index;
    } else {
      min_index = index + 1;
    }
  }

  return min_index;
}

Cursor* internal_node_find(Table* table, uint32_t page_num, uint32_t key) {
  void* node = get_page(table->pager, page_num);

  uint32_t child_index = internal_node_find_child(node, key);
  uint32_t child_num = *internal_node_child(node, child_index);
  void* child = get_page(table->pager, child_num);
  switch (get_node_type(child)) {
    case NODE_LEAF:
      return leaf_node_find(table, child_num, key);
    case NODE_INTERNAL:
      return internal_node_find(table, child_num, key);
  }
}

/*
Return the position of the given key.
If the key is not present, return the position
where it should be inserted
*/
Cursor* table_find(Table* table, uint32_t key) {
  uint32_t root_page_num = table->root_page_num;
  void* root_node = get_page(table->pager, root_page_num);

  if (get_node_type(root_node) == NODE_LEAF) {
    return leaf_node_find(table, root_page_num, key);
  } else {
    return internal_node_find(table, root_page_num, key);
  }
}

Cursor* table_start(Table* table) {
  Cursor* cursor = table_find(table, 0);

  void* node = get_page(table->pager, cursor->page_num);
  uint32_t num_cells = *leaf_node_num_cells(node);
  cursor->end_of_table = (num_cells == 0);

  return cursor;
}

void* cursor_value(Cursor* cursor) {
  uint32_t page_num = cursor->page_num;
  void* page = get_page(cursor->table->pager, page_num);
  return leaf_value_t(cursor->table,page, cursor->cell_num);
}

void cursor_advance(Cursor* cursor) {
  uint32_t page_num = cursor->page_num;
  void* node = get_page(cursor->table->pager, page_num);

  cursor->cell_num += 1;
  if (cursor->cell_num >= (*leaf_node_num_cells(node))) {
    /* Advance to next leaf node */
    uint32_t next_page_num = *leaf_node_next_leaf(node);
    if (next_page_num == 0) {
      /* This was rightmost leaf */
      cursor->end_of_table = true;
    } else {
      cursor->page_num = next_page_num;
      cursor->cell_num = 0;
    }
  }
}

Pager* pager_open(const char* filename) {
  int fd = open(filename,
                O_RDWR |      // Read/Write mode
                    O_CREAT,  // Create file if it does not exist
                S_IWUSR |     // User write permission
                    S_IRUSR   // User read permission
                );

  if (fd == -1) {
    printf("Unable to open file\n");
    exit(EXIT_FAILURE);
  }

  off_t file_length = lseek(fd, 0, SEEK_END);

  Pager* pager = malloc(sizeof(Pager));
  pager->file_descriptor = fd;
  pager->file_length = file_length;
  pager->num_pages = (file_length / PAGE_SIZE);

  if (file_length % PAGE_SIZE != 0) {
    printf("Db file is not a whole number of pages. Corrupt file.\n");
    exit(EXIT_FAILURE);
  }

  for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++) {
    pager->pages[i] = NULL;
  }

  return pager;
}

Table* db_open(const char* filename) {
  Pager* pager = pager_open(filename);

  if(pager->num_pages == 0){
    get_page(pager,0);
    catalog_init(pager);
  }else{
    CatalogHeader* hdr = (CatalogHeader*)get_page(pager,0);
    if(hdr->magic != DB_MAGIC){
      printf("Invalid DB file (bad magic).\\n");
      exit(EXIT_FAILURE);
    }
  }

  Table* table = malloc(sizeof(Table));
  table->pager = pager;
  table->root_page_num = INVALID_PAGE_NUM;

  return table;
}

InputBuffer* new_input_buffer() {
  InputBuffer* input_buffer = malloc(sizeof(InputBuffer));
  input_buffer->buffer = NULL;
  input_buffer->buffer_length = 0;
  input_buffer->input_length = 0;

  return input_buffer;
}

void print_prompt() { printf("db > "); }

void read_input(InputBuffer* input_buffer) {
  ssize_t bytes_read =
      getline(&(input_buffer->buffer), &(input_buffer->buffer_length), stdin);

  if (bytes_read <= 0) {
    printf("Error reading input\n");
    exit(EXIT_FAILURE);
  }

  // Ignore trailing newline
  input_buffer->input_length = bytes_read - 1;
  input_buffer->buffer[bytes_read - 1] = 0;
}

void close_input_buffer(InputBuffer* input_buffer) {
  free(input_buffer->buffer);
  free(input_buffer);
}

void pager_flush(Pager* pager, uint32_t page_num) {
  if (pager->pages[page_num] == NULL) {
    printf("Tried to flush null page\n");
    exit(EXIT_FAILURE);
  }

  off_t offset = lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);

  if (offset == -1) {
    printf("Error seeking: %d\n", errno);
    exit(EXIT_FAILURE);
  }

  ssize_t bytes_written =
      write(pager->file_descriptor, pager->pages[page_num], PAGE_SIZE);

  if (bytes_written == -1) {
    printf("Error writing: %d\n", errno);
    exit(EXIT_FAILURE);
  }
}

void db_close(Table* table) {
  Pager* pager = table->pager;

  for (uint32_t i = 0; i < pager->num_pages; i++) {
    if (pager->pages[i] == NULL) {
      continue;
    }
    pager_flush(pager, i);
    free(pager->pages[i]);
    pager->pages[i] = NULL;
  }

  int result = close(pager->file_descriptor);
  if (result == -1) {
    printf("Error closing db file.\n");
    exit(EXIT_FAILURE);
  }
  for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++) {
    void* page = pager->pages[i];
    if (page) {
      free(page);
      pager->pages[i] = NULL;
    }
  }
  free(pager);
  free(table);
  if (g_last_parsed_expr) {
    expr_free(g_last_parsed_expr);
    g_last_parsed_expr = NULL;
  }
}

MetaCommandResult do_meta_command(InputBuffer* input_buffer, Table* table) {
  if (strcmp(input_buffer->buffer, ".exit") == 0) {
    close_input_buffer(input_buffer);
    db_close(table);
    exit(EXIT_SUCCESS);
  } else if (strcmp(input_buffer->buffer, ".btree") == 0) {
    if(table->root_page_num == INVALID_PAGE_NUM){
      printf("No active table. Use 'use <table>' first.\n");
    }else{
      printf("Tree:\n");
      print_tree(table->pager,table->root_page_num,0);
    }
    return META_COMMAND_SUCCESS;
  } else if (strcmp(input_buffer->buffer, ".constants") == 0) {
    printf("Constants:\n");
    print_constants(table);
    return META_COMMAND_SUCCESS;
  } else {
    return META_COMMAND_UNRECOGNIZED_COMMAND;
  }
}


static void collect_values(char* start,Statement* st){
  st->num_values = 0;
  char* tok = strtok(start," ");
  while(tok && st->num_values < MAX_VALUES){
    st->values[st->num_values++] = tok;
    tok = strtok(NULL," ");
  }
}


PrepareResult prepare_insert(InputBuffer* input_buffer, Statement* st,Table* table){
  st->type = STATEMENT_INSERT;

  char* s = input_buffer->buffer;
  while(*s == ' ' || *s == '\t'){
    s++;
  }

  st->target_table[0] = '\0';
  if(strncmp(s,"insert into ",12) == 0){
    s += 12;
    while(*s == ' ' || *s == '\t'){
      s++;
    }
    char* tbl = strtok(s," ");
    if(!tbl){
      return PREPARE_SYNTAX_ERROR;
    }

    strncpy(st->target_table,tbl,MAX_TABLE_NAME_LEN-1);
    char* vals = strtok(NULL,"");
    if(!vals){
      st->num_values = 0;
    }else{
      collect_values(vals,st);
    }
    return PREPARE_SUCCESS;
  }
  return PREPARE_UNRECOGNIZED_STATEMENT;
}

PrepareResult prepare_statement(InputBuffer* input_buffer,
                                Statement* statement,Table* table) {

  
  const char* s = input_buffer->buffer;
  while(*s == ' ' || *s == '\t'){
    s++;
  }

  if(strncmp(s,"use ",4)==0){
    const char* name = s + 4;
    while(*name == ' ' || *name == '\t'){
      name++;
    }
    int idx = catalog_find(table->pager,name);
    if(idx < 0){
      printf("Table not found: %s\n",name);
      return PREPARE_UNRECOGNIZED_STATEMENT;
    }
    CatalogEntry* ents = catalog_entries(table->pager);
    table->root_page_num = ents[idx].root_page_num;
    
    table->active_schema = ents[idx].schema;
    table->row_size = compute_row_size(&table->active_schema);

    printf("Using table '%s'.\n",ents[idx].name);
    return PREPARE_CREATE_TABLE_DONE;
  }

  if (strncmp(s, "insert", 6) == 0) {
    return prepare_insert(input_buffer, statement,table);
  }
  if (strncmp(s, "create table", 12) == 0) {
    int ret = handle_create_table_ex(table,input_buffer->buffer);
    if (ret == 0) {
      return PREPARE_CREATE_TABLE_DONE;
    } else {
      printf("Create table failed: %d\n", ret);
      return PREPARE_UNRECOGNIZED_STATEMENT;
    }
  }
  if (strncmp(s, "select" , 6) == 0) {
    ParsedStmt ps;
    statement->type = STATEMENT_SELECT;
    if(parse_sql_to_parsed_stmt(input_buffer->buffer,&ps) != 0){
      return PREPARE_SYNTAX_ERROR;
    }
    if(ps.kind != PARSED_SELECT){
      parsed_stmt_free(&ps);
      return PREPARE_SYNTAX_ERROR;
    }

    if(ps.table_name[0]){
      strncpy(statement->target_table,ps.table_name,MAX_TABLE_NAME_LEN  -1);
      statement->target_table[MAX_TABLE_NAME_LEN-1] = '\0';
    }else{
      statement->target_table[0] = '\0';
    }

    statement->proj_count = 0;
    if(ps.select_all){
      statement->proj_count = 0;
    }else{
      for(uint32_t i = 0 ; i < ps.proj_count ; i++ ){
        int idx = schema_col_index(&table->active_schema,ps.proj_list[i]);
        if(idx < 0){
          printf("Unknown column: %s\n", ps.proj_list[i]);
          parsed_stmt_free(&ps);
          return PREPARE_SYNTAX_ERROR;
        }
        statement->proj_indices[statement->proj_count++] = idx;
      }
    }

    set_last_parsed_expr(ps.where);
    ps.where = NULL;

    parsed_stmt_free(&ps);
    return PREPARE_SUCCESS;

  }

  return PREPARE_UNRECOGNIZED_STATEMENT;
}

/*
Until we start recycling free pages, new pages will always
go onto the end of the database file
*/
uint32_t get_unused_page_num(Pager* pager) { return pager->num_pages; }

void create_new_root(Table* table, uint32_t right_child_page_num) {
  /*
  Handle splitting the root.
  Old root copied to new page, becomes left child.
  Address of right child passed in.
  Re-initialize root page to contain the new root node.
  New root node points to two children.
  */

  void* root = get_page(table->pager, table->root_page_num);
  void* right_child = get_page(table->pager, right_child_page_num);
  uint32_t left_child_page_num = get_unused_page_num(table->pager);
  void* left_child = get_page(table->pager, left_child_page_num);

  if (get_node_type(root) == NODE_INTERNAL) {
    initialize_internal_node(right_child);
    initialize_internal_node(left_child);
  }

  /* Left child has data copied from old root */
  memcpy(left_child, root, PAGE_SIZE);
  set_node_root(left_child, false);

  if (get_node_type(left_child) == NODE_INTERNAL) {
    void* child;
    for (int i = 0; i < *internal_node_num_keys(left_child); i++) {
      child = get_page(table->pager, *internal_node_child(left_child,i));
      *node_parent(child) = left_child_page_num;
    }
    child = get_page(table->pager, *internal_node_right_child(left_child));
    *node_parent(child) = left_child_page_num;
  }

  /* Root node is a new internal node with one key and two children */
  initialize_internal_node(root);
  set_node_root(root, true);
  *internal_node_num_keys(root) = 1;
  *internal_node_child(root, 0) = left_child_page_num;
  uint32_t left_child_max_key = get_node_max_key(table->pager, left_child);
  *internal_node_key(root, 0) = left_child_max_key;
  *internal_node_right_child(root) = right_child_page_num;
  *node_parent(left_child) = table->root_page_num;
  *node_parent(right_child) = table->root_page_num;
}

void internal_node_split_and_insert(Table* table, uint32_t parent_page_num,
                          uint32_t child_page_num);

void internal_node_insert(Table* table, uint32_t parent_page_num,
                          uint32_t child_page_num) {
  /*
  Add a new child/key pair to parent that corresponds to child
  */

  void* parent = get_page(table->pager, parent_page_num);
  void* child = get_page(table->pager, child_page_num);
  uint32_t child_max_key = get_node_max_key(table->pager, child);
  uint32_t index = internal_node_find_child(parent, child_max_key);

  uint32_t original_num_keys = *internal_node_num_keys(parent);

  if (original_num_keys >= INTERNAL_NODE_MAX_KEYS) {
    internal_node_split_and_insert(table, parent_page_num, child_page_num);
    return;
  }

  uint32_t right_child_page_num = *internal_node_right_child(parent);
  /*
  An internal node with a right child of INVALID_PAGE_NUM is empty
  */
  if (right_child_page_num == INVALID_PAGE_NUM) {
    *internal_node_right_child(parent) = child_page_num;
    return;
  }

  void* right_child = get_page(table->pager, right_child_page_num);
  /*
  If we are already at the max number of cells for a node, we cannot increment
  before splitting. Incrementing without inserting a new key/child pair
  and immediately calling internal_node_split_and_insert has the effect
  of creating a new key at (max_cells + 1) with an uninitialized value
  */
  *internal_node_num_keys(parent) = original_num_keys + 1;

  if (child_max_key > get_node_max_key(table->pager, right_child)) {
    /* Replace right child */
    *internal_node_child(parent, original_num_keys) = right_child_page_num;
    *internal_node_key(parent, original_num_keys) =
        get_node_max_key(table->pager, right_child);
    *internal_node_right_child(parent) = child_page_num;
  } else {
    /* Make room for the new cell */
    for (uint32_t i = original_num_keys; i > index; i--) {
      void* destination = internal_node_cell(parent, i);
      void* source = internal_node_cell(parent, i - 1);
      memcpy(destination, source, INTERNAL_NODE_CELL_SIZE);
    }
    *internal_node_child(parent, index) = child_page_num;
    *internal_node_key(parent, index) = child_max_key;
  }
}

void update_internal_node_key(void* node, uint32_t old_key, uint32_t new_key) {
  uint32_t old_child_index = internal_node_find_child(node, old_key);
  *internal_node_key(node, old_child_index) = new_key;
}

void internal_node_split_and_insert(Table* table, uint32_t parent_page_num,
                          uint32_t child_page_num) {
  uint32_t old_page_num = parent_page_num;
  void* old_node = get_page(table->pager,parent_page_num);
  uint32_t old_max = get_node_max_key(table->pager, old_node);

  void* child = get_page(table->pager, child_page_num); 
  uint32_t child_max = get_node_max_key(table->pager, child);

  uint32_t new_page_num = get_unused_page_num(table->pager);

  uint32_t splitting_root = is_node_root(old_node);

  void* parent;
  void* new_node;
  if (splitting_root) {
    create_new_root(table, new_page_num);
    parent = get_page(table->pager,table->root_page_num);
    /*
    If we are splitting the root, we need to update old_node to point
    to the new root's left child, new_page_num will already point to
    the new root's right child
    */
    old_page_num = *internal_node_child(parent,0);
    old_node = get_page(table->pager, old_page_num);
  } else {
    parent = get_page(table->pager,*node_parent(old_node));
    new_node = get_page(table->pager, new_page_num);
    initialize_internal_node(new_node);
  }
  
  uint32_t* old_num_keys = internal_node_num_keys(old_node);

  uint32_t cur_page_num = *internal_node_right_child(old_node);
  void* cur = get_page(table->pager, cur_page_num);

  /*
  First put right child into new node and set right child of old node to invalid page number
  */
  internal_node_insert(table, new_page_num, cur_page_num);
  *node_parent(cur) = new_page_num;
  *internal_node_right_child(old_node) = INVALID_PAGE_NUM;
  /*
  For each key until you get to the middle key, move the key and the child to the new node
  */
  for (int i = INTERNAL_NODE_MAX_KEYS - 1; i > INTERNAL_NODE_MAX_KEYS / 2; i--) {
    cur_page_num = *internal_node_child(old_node, i);
    cur = get_page(table->pager, cur_page_num);

    internal_node_insert(table, new_page_num, cur_page_num);
    *node_parent(cur) = new_page_num;

    (*old_num_keys)--;
  }

  /*
  Set child before middle key, which is now the highest key, to be node's right child,
  and decrement number of keys
  */
  *internal_node_right_child(old_node) = *internal_node_child(old_node,*old_num_keys - 1);
  (*old_num_keys)--;

  /*
  Determine which of the two nodes after the split should contain the child to be inserted,
  and insert the child
  */
  uint32_t max_after_split = get_node_max_key(table->pager, old_node);

  uint32_t destination_page_num = child_max < max_after_split ? old_page_num : new_page_num;

  internal_node_insert(table, destination_page_num, child_page_num);
  *node_parent(child) = destination_page_num;

  update_internal_node_key(parent, old_max, get_node_max_key(table->pager, old_node));

  if (!splitting_root) {
    internal_node_insert(table,*node_parent(old_node),new_page_num);
    *node_parent(new_node) = *node_parent(old_node);
  }
}

void leaf_node_split_and_insert(Cursor* cursor, uint32_t key, char* const* values, uint32_t nvals) {
  /*
  Create a new node and move half the cells over.
  Insert the new value in one of the two nodes.
  Update parent or create a new parent.
  */

  void* old_node = get_page(cursor->table->pager, cursor->page_num);
  uint32_t old_max = get_node_max_key(cursor->table->pager, old_node);
  uint32_t new_page_num = get_unused_page_num(cursor->table->pager);
  void* new_node = get_page(cursor->table->pager, new_page_num);
  initialize_leaf_node(new_node);
  *node_parent(new_node) = *node_parent(old_node);
  *leaf_node_next_leaf(new_node) = *leaf_node_next_leaf(old_node);
  *leaf_node_next_leaf(old_node) = new_page_num;

  /*
  All existing keys plus new key should should be divided
  evenly between old (left) and new (right) nodes.
  Starting from the right, move each key to correct position.
  */
  for (int32_t i = (int32_t)leaf_max_cells(cursor->table); i >= 0; i--) {
    void* destination_node;
    if (i >= (int32_t)leaf_left_split_count(cursor->table)) {
      destination_node = new_node;
    } else {
      destination_node = old_node;
    }
    uint32_t index_within_node = (uint32_t)i % leaf_left_split_count(cursor->table);
    void* destination = leaf_cell_t(cursor->table, destination_node, index_within_node);

    if (i == cursor->cell_num) {
      serialize_row_dynamic(cursor->table, values, nvals, 
                            leaf_value_t(cursor->table, destination_node, index_within_node));
      *leaf_key_t(cursor->table, destination_node, index_within_node) = key;
    } else if (i > cursor->cell_num) {
      memcpy(destination, leaf_cell_t(cursor->table, old_node, (uint32_t)i - 1), leaf_cell_size(cursor->table));
    } else {
      memcpy(destination, leaf_cell_t(cursor->table, old_node, (uint32_t)i), leaf_cell_size(cursor->table));
    }
  }

  /* Update cell count on both leaf nodes */
  *(leaf_node_num_cells(old_node)) = leaf_left_split_count(cursor->table);
  *(leaf_node_num_cells(new_node)) = leaf_right_split_count(cursor->table);

  if (is_node_root(old_node)) {
    return create_new_root(cursor->table, new_page_num);
  } else {
    uint32_t parent_page_num = *node_parent(old_node);
    uint32_t new_max = get_node_max_key(cursor->table->pager, old_node);
    void* parent = get_page(cursor->table->pager, parent_page_num);

    update_internal_node_key(parent, old_max, new_max);
    internal_node_insert(cursor->table, parent_page_num, new_page_num);
    return;
  }
}

void leaf_node_insert(Cursor* cursor, uint32_t key, char* const* values, uint32_t nvals) {
  void* node = get_page(cursor->table->pager, cursor->page_num);

  uint32_t num_cells = *leaf_node_num_cells(node);
  if (num_cells >= leaf_max_cells(cursor->table)) {
    // Node full
    leaf_node_split_and_insert(cursor, key, values, nvals);
    return;
  }

  if (cursor->cell_num < num_cells) {
    // Make room for new cell
    for (uint32_t i = num_cells; i > cursor->cell_num; i--) {
      memcpy(leaf_cell_t(cursor->table, node, i),
             leaf_cell_t(cursor->table, node, i - 1),
             leaf_cell_size(cursor->table));
    }
  }

  *(leaf_node_num_cells(node)) += 1;
  *(leaf_key_t(cursor->table, node, cursor->cell_num)) = key;
  serialize_row_dynamic(cursor->table, values, nvals, 
                        leaf_value_t(cursor->table, node, cursor->cell_num));
}



static void serialize_row_dynamic(Table* t,char* const* values,uint32_t n, void* dest){
  uint8_t* p = (uint8_t*)dest;
  for(uint32_t i = 0 ; i < t->active_schema.num_columns ; i++){
    ColumnDef* c = &t->active_schema.columns[i];
    const char* val = (i < n) ? values[i] : "";
    if(c->type == COL_TYPE_INT){
      int v = 0;
      parse_int(val,&v);
      memcpy(p,&v,4);
      p += 4;
    }else{
      size_t len = strlen(val);
      size_t to_copy = len > c->size?c->size:len;
      memcpy(p,val,to_copy);
      if(to_copy < c->size){
        memset(p + to_copy,0,c->size - to_copy);
      }
      p += c->size;
    }
  }
}


ExecuteResult execute_insert(Statement* st, Table* table) {
  if (table->root_page_num == INVALID_PAGE_NUM) {
    printf("No active table. Use 'use <table>' or 'insert into <table> ...' first.\n");
    return EXECUTE_SUCCESS;
  }

  // 如果是 insert into <table>，切表
  if (st->target_table[0]) {
    int idx = catalog_find(table->pager, st->target_table);
    if (idx < 0) { printf("Table not found: %s\n", st->target_table); return EXECUTE_SUCCESS; }
    CatalogEntry* ents = catalog_entries(table->pager);
    table->root_page_num = ents[idx].root_page_num;
    table->active_schema = ents[idx].schema;
    table->row_size = compute_row_size(&table->active_schema);
  }

  // 主键取第一列，要求为 int
  if (table->active_schema.num_columns == 0 || table->active_schema.columns[0].type != COL_TYPE_INT) {
    printf("First column must be int primary key.\n");
    return EXECUTE_SUCCESS;
  }
  int key_int = 0;
  if (st->num_values == 0 || parse_int(st->values[0], &key_int) != 0) {
    printf("Invalid key.\n");
    return EXECUTE_SUCCESS;
  }
  uint32_t key = (uint32_t)key_int;

  Cursor* cursor = table_find(table, key);
  void* node = get_page(table->pager, cursor->page_num);
  uint32_t num_cells = *leaf_node_num_cells(node);
  if (cursor->cell_num < num_cells) {
    uint32_t key_at_index = *leaf_key_t(table, node, cursor->cell_num);
    if (key_at_index == key) {
      free(cursor);
      return EXECUTE_DUPLICATE_KEY;
    }
  }

  leaf_node_insert(cursor, key, st->values, st->num_values);
  free(cursor);
  return EXECUTE_SUCCESS;
}

static int eval_expr_to_bool(Table* t, const void* row, Expr* e) {
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
      } else {
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
      } else {
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
      if (strcmp(e->op, "=") == 0) return strcmp(lhs_s, rhs_s) == 0;
      if (strcmp(e->op, "!=") == 0) return strcmp(lhs_s, rhs_s) != 0;
      if (strcmp(e->op, "<") == 0) return strcmp(lhs_s, rhs_s) < 0;
      if (strcmp(e->op, ">") == 0) return strcmp(lhs_s, rhs_s) > 0;
      if (strcmp(e->op, "<=") == 0) return strcmp(lhs_s, rhs_s) <= 0;
      if (strcmp(e->op, ">=") == 0) return strcmp(lhs_s, rhs_s) >= 0;
    }
    return 0;
  }

  case EXPR_BETWEEN: {
    Expr* val = e->left;
    Expr* low = e->right->left;
    Expr* high = e->right->right;

    int v = 0, l = 0, h = 0;
    char vs[512] = {0}, ls[512] = {0}, hs[512] = {0};
    int is_num = 0;
    if (val->kind == EXPR_COLUMN) {
      int idx = schema_col_index(&t->active_schema, val->text);
      if (idx >= 0 && t->active_schema.columns[idx].type == COL_TYPE_INT) {
        v = row_get_int(t, row, idx);
        is_num = 1;
      } else {
        row_get_string(t, row, idx, vs, sizeof(vs));
      }
    }

    if (low->kind == EXPR_LITERAL) {
      if (parse_int(low->text, &l) == 0) {
        is_num = 1;
      } else {
        strncpy(ls, low->text, sizeof(ls) - 1);
        ls[sizeof(ls) - 1] = '\0';
      }
    }

    if (high->kind == EXPR_LITERAL) {
      if (parse_int(high->text, &h) == 0) {
        is_num = 1;
      } else {
        strncpy(hs, high->text, sizeof(hs) - 1);
        hs[sizeof(hs) - 1] = '\0';
      }
    }

    if (is_num) {
      return (v >= l && v <= h);
    }
    return (strcmp(vs, ls) >= 0 && strcmp(vs, hs) <= 0);
  }

  case EXPR_ISNULL: {
    Expr* target = e->left;
    if (target->kind == EXPR_COLUMN) {
      int idx = schema_col_index(&t->active_schema, target->text);
      if (idx < 0) {
        return 0;
      }
      if (t->active_schema.columns[idx].type == COL_TYPE_INT) {
        return 0;
      } else {
        char buf[512];
        row_get_string(t, row, idx, buf, sizeof(buf));
        int isnull = (buf[0] == '\0');
        if (strcmp(e->op, "IS NOT") == 0) return !isnull;
        return isnull;
      }
    }
    return 0;
  }
  }
  return 0;
}


ExecuteResult execute_select(Statement* st, Table* table) {

  if(st->target_table[0]){
    int idx = catalog_find(table->pager,st->target_table);
    if(idx < 0){
      printf("Table not found: %s\n", st->target_table);
      return EXECUTE_SUCCESS;
    }
    CatalogEntry* ents = catalog_entries(table->pager);
    table->root_page_num = ents[idx].root_page_num;
    table->active_schema = ents[idx].schema;
    table->row_size = compute_row_size(&table->active_schema);
  }

  if(table->root_page_num == INVALID_PAGE_NUM){
    printf("No active table. Use 'use <table>' or 'create table..' first.\n");
    return EXECUTE_SUCCESS;
  }

  bool can_point_lookup = false;
  uint32_t lookup_key = 0;

  extern Expr* g_last_parsed_expr;
  Expr* ast = g_last_parsed_expr;

  if(ast && ast->kind == EXPR_BINARY && strcmp(ast->op,"=") == 0){
    Expr* left = ast->left;
    Expr* right = ast->right;
    Expr* colExpr = NULL;
    Expr* litExpr = NULL;

    if(left && left->kind == EXPR_COLUMN && right && right->kind == EXPR_LITERAL){
      colExpr = left;
      litExpr = right;
    }else if(right && right->kind == EXPR_COLUMN && left && left->kind == EXPR_LITERAL){
      colExpr = right;
      litExpr = left; 
    }

    if(colExpr && litExpr){
      int col_idx = schema_col_index(&table->active_schema,colExpr->text);
      if(col_idx == 0 && table->active_schema.columns[0].type == COL_TYPE_INT){
        int v = 0;
        if (parse_int(litExpr->text, &v) == 0) {
          can_point_lookup = true;
          lookup_key = (uint32_t)v;
        }
      }
    }
  }

  /* legacy Statement where -> keep for compatibility */
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
        if (pass) {
          print_row_projected(table, row, st->proj_indices, st->proj_count);
        }
      }
    }
    free(cursor);
    return EXECUTE_SUCCESS;
  }

  /* full table scan: prefer AST evaluation, fallback to legacy row_matches_where */
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
      print_row_projected(table, row, st->proj_indices, st->proj_count);
    }
    cursor_advance(cursor);
  }
  free(cursor);
  return EXECUTE_SUCCESS;
}

ExecuteResult execute_statement(Statement* statement, Table* table) {
  switch (statement->type) {
    case (STATEMENT_INSERT):
      return execute_insert(statement, table);
    case (STATEMENT_SELECT):
      return execute_select(statement, table);
  }
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    printf("Must supply a database filename.\n");
    exit(EXIT_FAILURE);
  }

  char* filename = argv[1];
  Table* table = db_open(filename);

  InputBuffer* input_buffer = new_input_buffer();
  while (true) {
    print_prompt();
    read_input(input_buffer);

    if (input_buffer->buffer[0] == '.') {
      switch (do_meta_command(input_buffer, table)) {
        case (META_COMMAND_SUCCESS):
          continue;
        case (META_COMMAND_UNRECOGNIZED_COMMAND):
          printf("Unrecognized command '%s'\n", input_buffer->buffer);
          continue;
      }
    }

    Statement statement;
    switch (prepare_statement(input_buffer, &statement,table)) {
      case (PREPARE_SUCCESS):
        break;
      case (PREPARE_CREATE_TABLE_DONE):
        printf("Executed.\n");
        continue;
      case (PREPARE_NEGATIVE_ID):
        printf("ID must be positive.\n");
        continue;
      case (PREPARE_STRING_TOO_LONG):
        printf("String is too long.\n");
        continue;
      case (PREPARE_SYNTAX_ERROR):
        printf("Syntax error. Could not parse statement.\n");
        continue;
      case (PREPARE_UNRECOGNIZED_STATEMENT):
        printf("Unrecognized keyword at start of '%s'.\n",
               input_buffer->buffer);
        continue;
    }

    switch (execute_statement(&statement, table)) {
      case (EXECUTE_SUCCESS):
        printf("Executed.\n");
        break;
      case (EXECUTE_DUPLICATE_KEY):
        printf("Error: Duplicate key.\n");
        break;
    }
  }
}
