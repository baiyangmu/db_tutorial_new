#ifndef MYDB_BTREE_H
#define MYDB_BTREE_H

#include <stdint.h>
#include <stdbool.h>
#include "pager.h"
#include "schema.h"

/* Node types */
typedef enum { NODE_INTERNAL, NODE_LEAF } NodeType;

/* Table structure */
typedef struct Table {
  Pager* pager;
  uint32_t root_page_num;
  TableSchema active_schema;
  uint32_t row_size;
} Table;

/* Cursor for table traversal */
typedef struct {
  Table* table;
  uint32_t page_num;
  uint32_t cell_num;
  bool end_of_table;
} Cursor;

/* Common Node Header Layout */
extern const uint32_t NODE_TYPE_SIZE;
extern const uint32_t NODE_TYPE_OFFSET;
extern const uint32_t IS_ROOT_SIZE;
extern const uint32_t IS_ROOT_OFFSET;
extern const uint32_t PARENT_POINTER_SIZE;
extern const uint32_t PARENT_POINTER_OFFSET;
extern const uint8_t COMMON_NODE_HEADER_SIZE;

/* Internal Node Header Layout */
extern const uint32_t INTERNAL_NODE_NUM_KEYS_SIZE;
extern const uint32_t INTERNAL_NODE_NUM_KEYS_OFFSET;
extern const uint32_t INTERNAL_NODE_RIGHT_CHILD_SIZE;
extern const uint32_t INTERNAL_NODE_RIGHT_CHILD_OFFSET;
extern const uint32_t INTERNAL_NODE_HEADER_SIZE;

/* Internal Node Body Layout */
extern const uint32_t INTERNAL_NODE_KEY_SIZE;
extern const uint32_t INTERNAL_NODE_CHILD_SIZE;
extern const uint32_t INTERNAL_NODE_CELL_SIZE;
extern const uint32_t INTERNAL_NODE_MAX_KEYS;

/* Leaf Node Header Layout */
extern const uint32_t LEAF_NODE_NUM_CELLS_SIZE;
extern const uint32_t LEAF_NODE_NUM_CELLS_OFFSET;
extern const uint32_t LEAF_NODE_NEXT_LEAF_SIZE;
extern const uint32_t LEAF_NODE_NEXT_LEAF_OFFSET;
extern const uint32_t LEAF_NODE_HEADER_SIZE;

/* Node accessor functions */
NodeType get_node_type(void* node);
void set_node_type(void* node, NodeType type);
bool is_node_root(void* node);
void set_node_root(void* node, bool is_root);
uint32_t* node_parent(void* node);

/* Internal node functions */
uint32_t* internal_node_num_keys(void* node);
uint32_t* internal_node_right_child(void* node);
uint32_t* internal_node_cell(void* node, uint32_t cell_num);
uint32_t* internal_node_child(void* node, uint32_t child_num);
uint32_t* internal_node_key(void* node, uint32_t key_num);
uint32_t internal_node_find_child(void* node, uint32_t key);
void initialize_internal_node(void* node);
void internal_node_insert(Table* table, uint32_t parent_page_num, uint32_t child_page_num);
void internal_node_split_and_insert(Table* table, uint32_t parent_page_num, uint32_t child_page_num);
void update_internal_node_key(void* node, uint32_t old_key, uint32_t new_key);

/* Leaf node functions */
uint32_t* leaf_node_num_cells(void* node);
uint32_t* leaf_node_next_leaf(void* node);
void initialize_leaf_node(void* node);

/* Table-aware leaf node helpers (dynamic row size) */
uint32_t leaf_value_size(Table* t);
uint32_t leaf_cell_size(Table* t);
int32_t leaf_space_for_cells(void);
uint32_t leaf_max_cells(Table* t);
uint32_t leaf_right_split_count(Table* t);
uint32_t leaf_left_split_count(Table* t);
void* leaf_cell_t(Table* t, void* node, uint32_t cell_num);
uint32_t* leaf_key_t(Table* t, void* node, uint32_t cell_num);
void* leaf_value_t(Table* t, void* node, uint32_t cell_num);

/* Search and traversal */
Cursor* table_find(Table* table, uint32_t key);
Cursor* table_start(Table* table);
Cursor* leaf_node_find(Table* table, uint32_t page_num, uint32_t key);
Cursor* internal_node_find(Table* table, uint32_t page_num, uint32_t key);
void* cursor_value(Cursor* cursor);
void cursor_advance(Cursor* cursor);

/* Insert operations */
void leaf_node_insert(Cursor* cursor, uint32_t key, char* const* values, uint32_t nvals);
void leaf_node_split_and_insert(Cursor* cursor, uint32_t key, char* const* values, uint32_t nvals);

/* Tree structure operations */
void create_new_root(Table* table, uint32_t right_child_page_num);
uint32_t get_node_max_key(Table* table, void* node);

/* Delete and merge operations */
void handle_underflow(Table* table, uint32_t page_num);
void leaf_node_merge_with_right(Table* table, uint32_t left_page_num, uint32_t right_page_num);
void leaf_node_merge_with_left(Table* table, uint32_t left_page_num, uint32_t right_page_num);
void internal_node_remove_child(Table* table, void* parent, uint32_t child_page_num);
void find_siblings(Table* table, uint32_t page_num, uint32_t* left_sibling, uint32_t* right_sibling);
int find_child_index_in_parent(void* parent, uint32_t child_page_num);

/* Debug functions */
void print_constants(Table* t);
void print_tree(Table* table, uint32_t page_num, uint32_t indentation_level);
uint32_t get_leaf_node_cell_count_debug(void* node, const char* context);

/* Database operations */
Table* db_open(const char* filename);
void db_close(Table* table);

/* Row serialization (dynamic) */
void serialize_row_dynamic(Table* t, char* const* values, uint32_t n, void* dest);

#endif /* MYDB_BTREE_H */

