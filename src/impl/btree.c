#include "../include/btree.h"
#include "../include/catalog.h"
#include "../include/util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Node layout constants */
const uint32_t NODE_TYPE_SIZE = sizeof(uint8_t);
const uint32_t NODE_TYPE_OFFSET = 0;
const uint32_t IS_ROOT_SIZE = sizeof(uint8_t);
const uint32_t IS_ROOT_OFFSET = NODE_TYPE_SIZE;
const uint32_t PARENT_POINTER_SIZE = sizeof(uint32_t);
const uint32_t PARENT_POINTER_OFFSET = IS_ROOT_OFFSET + IS_ROOT_SIZE;
const uint8_t COMMON_NODE_HEADER_SIZE =
    NODE_TYPE_SIZE + IS_ROOT_SIZE + PARENT_POINTER_SIZE;

/* Internal node layout */
const uint32_t INTERNAL_NODE_NUM_KEYS_SIZE = sizeof(uint32_t);
const uint32_t INTERNAL_NODE_NUM_KEYS_OFFSET = COMMON_NODE_HEADER_SIZE;
const uint32_t INTERNAL_NODE_RIGHT_CHILD_SIZE = sizeof(uint32_t);
const uint32_t INTERNAL_NODE_RIGHT_CHILD_OFFSET =
    INTERNAL_NODE_NUM_KEYS_OFFSET + INTERNAL_NODE_NUM_KEYS_SIZE;
const uint32_t INTERNAL_NODE_HEADER_SIZE = COMMON_NODE_HEADER_SIZE +
                                           INTERNAL_NODE_NUM_KEYS_SIZE +
                                           INTERNAL_NODE_RIGHT_CHILD_SIZE;

/* Internal node body */
const uint32_t INTERNAL_NODE_KEY_SIZE = sizeof(uint32_t);
const uint32_t INTERNAL_NODE_CHILD_SIZE = sizeof(uint32_t);
const uint32_t INTERNAL_NODE_CELL_SIZE =
    INTERNAL_NODE_CHILD_SIZE + INTERNAL_NODE_KEY_SIZE;
const uint32_t INTERNAL_NODE_MAX_KEYS = 3;

/* Leaf node layout */
const uint32_t LEAF_NODE_NUM_CELLS_SIZE = sizeof(uint32_t);
const uint32_t LEAF_NODE_NUM_CELLS_OFFSET = COMMON_NODE_HEADER_SIZE;
const uint32_t LEAF_NODE_NEXT_LEAF_SIZE = sizeof(uint32_t);
const uint32_t LEAF_NODE_NEXT_LEAF_OFFSET =
    LEAF_NODE_NUM_CELLS_OFFSET + LEAF_NODE_NUM_CELLS_SIZE;
const uint32_t LEAF_NODE_HEADER_SIZE = COMMON_NODE_HEADER_SIZE +
                                       LEAF_NODE_NUM_CELLS_SIZE +
                                       LEAF_NODE_NEXT_LEAF_SIZE;

/* Node accessor functions */
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

uint32_t* node_parent(void* node) {
  return node + PARENT_POINTER_OFFSET;
}

/* Internal node functions */
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

void initialize_internal_node(void* node) {
  set_node_type(node, NODE_INTERNAL);
  set_node_root(node, false);
  *internal_node_num_keys(node) = 0;
  *internal_node_right_child(node) = INVALID_PAGE_NUM;
}

uint32_t internal_node_find_child(void* node, uint32_t key) {
  uint32_t num_keys = *internal_node_num_keys(node);
  uint32_t min_index = 0;
  uint32_t max_index = num_keys;

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

/* Leaf node functions */
uint32_t* leaf_node_num_cells(void* node) {
  return node + LEAF_NODE_NUM_CELLS_OFFSET;
}

uint32_t* leaf_node_next_leaf(void* node) {
  return node + LEAF_NODE_NEXT_LEAF_OFFSET;
}

void initialize_leaf_node(void* node) {
  fprintf(stderr, "[DEBUG-NODE] initialize_leaf_node: initializing new leaf node\n");
  fflush(stderr);
  set_node_type(node, NODE_LEAF);
  set_node_root(node, false);
  *leaf_node_num_cells(node) = 0;
  *leaf_node_next_leaf(node) = 0;
  fprintf(stderr, "[DEBUG-NODE] initialize_leaf_node: node initialized with 0 cells\n");
  fflush(stderr);
}

uint32_t get_leaf_node_cell_count_debug(void* node, const char* context) {
  uint32_t count = *leaf_node_num_cells(node);
  fprintf(stderr, "[DEBUG-NODE] %s: leaf node has %u cells\n", context, count);
  fflush(stderr);
  return count;
}

/* Table-aware leaf node helpers */
uint32_t leaf_value_size(Table* t) {
  return t->row_size;
}

uint32_t leaf_cell_size(Table* t) {
  return sizeof(uint32_t) + leaf_value_size(t);
}

int32_t leaf_space_for_cells(void) {
  return MYDB_PAGE_SIZE - LEAF_NODE_HEADER_SIZE;
}

uint32_t leaf_max_cells(Table* t) {
  return leaf_space_for_cells() / leaf_cell_size(t);
}

uint32_t leaf_right_split_count(Table* t) {
  return (leaf_max_cells(t) + 1) / 2;
}

uint32_t leaf_left_split_count(Table* t) {
  return (leaf_max_cells(t) + 1) - leaf_right_split_count(t);
}

void* leaf_cell_t(Table* t, void* node, uint32_t cell_num) {
  return (uint8_t*)node + LEAF_NODE_HEADER_SIZE + cell_num * leaf_cell_size(t);
}

uint32_t* leaf_key_t(Table* t, void* node, uint32_t cell_num) {
  return (uint32_t*)leaf_cell_t(t, node, cell_num);
}

void* leaf_value_t(Table* t, void* node, uint32_t cell_num) {
  return leaf_cell_t(t, node, cell_num) + sizeof(uint32_t);
}

/* Get maximum key in a node */
uint32_t get_node_max_key(Table* table, void* node) {
  if (get_node_type(node) == NODE_LEAF) {
    uint32_t num_cells = *leaf_node_num_cells(node);
    if (num_cells == 0) return 0;
    return *leaf_key_t(table, node, num_cells - 1);
  }
  void* right_child = get_page(table->pager, *internal_node_right_child(node));
  return get_node_max_key(table, right_child);
}

/* Search functions */
Cursor* leaf_node_find(Table* table, uint32_t page_num, uint32_t key) {
  fprintf(stderr, "[DEBUG-LEAF-FIND] leaf_node_find: searching page %u for key %u\n", page_num, key);
  fflush(stderr);
  void* node = get_page(table->pager, page_num);
  uint32_t num_cells = *leaf_node_num_cells(node);
  fprintf(stderr, "[DEBUG-LEAF-FIND] leaf_node_find: page %u has %u cells\n", page_num, num_cells);
  fflush(stderr);

  Cursor* cursor = malloc(sizeof(Cursor));
  cursor->table = table;
  cursor->page_num = page_num;
  cursor->end_of_table = false;

  uint32_t min_index = 0;
  uint32_t one_past_max_index = num_cells;
  fprintf(stderr, "[DEBUG-LEAF-FIND] leaf_node_find: binary search range [%u, %u)\n", min_index, one_past_max_index);
  fflush(stderr);

  while (one_past_max_index != min_index) {
    uint32_t index = (min_index + one_past_max_index) / 2;
    uint32_t key_at_index = *leaf_key_t(table, node, index);
    fprintf(stderr, "[DEBUG-LEAF-FIND] leaf_node_find: comparing key %u with key_at_index[%u] = %u\n", key, index, key_at_index);
    fflush(stderr);

    if (key == key_at_index) {
      fprintf(stderr, "[DEBUG-LEAF-FIND] leaf_node_find: exact match found at cell %u\n", index);
      fflush(stderr);
      cursor->cell_num = index;
      return cursor;
    }
    if (key < key_at_index) {
      one_past_max_index = index;
      fprintf(stderr, "[DEBUG-LEAF-FIND] leaf_node_find: key < key_at_index, searching [%u, %u)\n", min_index, one_past_max_index);
      fflush(stderr);
    } else {
      min_index = index + 1;
      fprintf(stderr, "[DEBUG-LEAF-FIND] leaf_node_find: key > key_at_index, searching [%u, %u)\n", min_index, one_past_max_index);
      fflush(stderr);
    }
  }

  fprintf(stderr, "[DEBUG-LEAF-FIND] leaf_node_find: no exact match, position for insertion: %u\n", min_index);
  fflush(stderr);
  cursor->cell_num = min_index;
  return cursor;
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

Cursor* table_find(Table* table, uint32_t key) {
  fprintf(stderr, "[DEBUG-FIND] table_find: searching for key=%u\n", key);
  fflush(stderr);
  uint32_t root_page_num = table->root_page_num;
  fprintf(stderr, "[DEBUG-FIND] table_find: root_page_num=%u\n", root_page_num);
  fflush(stderr);
  void* root_node = get_page(table->pager, root_page_num);

  NodeType node_type = get_node_type(root_node);
  bool is_root = is_node_root(root_node);
  fprintf(stderr, "[DEBUG-FIND] table_find: node_type=%d (0=INTERNAL, 1=LEAF), is_root=%d\n", node_type, is_root);
  fflush(stderr);

  uint8_t* header = (uint8_t*)root_node;
  fprintf(stderr, "[DEBUG-FIND] table_find: raw header bytes: [0]=%u, [1]=%u, [2-5]=%u,%u,%u,%u\n",
          header[0], header[1], header[2], header[3], header[4], header[5]);
  fflush(stderr);

  if (node_type == NODE_LEAF) {
    fprintf(stderr, "[DEBUG-FIND] table_find: root is leaf node, calling leaf_node_find\n");
    fflush(stderr);
    uint32_t num_cells = *leaf_node_num_cells(root_node);
    fprintf(stderr, "[DEBUG-FIND] table_find: leaf root has %u cells\n", num_cells);
    fflush(stderr);
    return leaf_node_find(table, root_page_num, key);
  } else {
    fprintf(stderr, "[DEBUG-FIND] table_find: root is internal node, calling internal_node_find\n");
    fflush(stderr);
    return internal_node_find(table, root_page_num, key);
  }
}

Cursor* table_start(Table* table) {
  Cursor* cursor = table_find(table, 0);
  void* node = get_page(table->pager, cursor->page_num);
  uint32_t num_cells = *leaf_node_num_cells(node);

  while (num_cells == 0) {
    uint32_t next_page_num = *leaf_node_next_leaf(node);
    if (next_page_num == 0) {
      cursor->end_of_table = true;
      return cursor;
    }
    cursor->page_num = next_page_num;
    cursor->cell_num = 0;
    node = get_page(table->pager, cursor->page_num);
    num_cells = *leaf_node_num_cells(node);
  }

  cursor->end_of_table = false;
  return cursor;
}

void* cursor_value(Cursor* cursor) {
  uint32_t page_num = cursor->page_num;
  void* page = get_page(cursor->table->pager, page_num);
  return leaf_value_t(cursor->table, page, cursor->cell_num);
}

void cursor_advance(Cursor* cursor) {
  uint32_t page_num = cursor->page_num;
  void* node = get_page(cursor->table->pager, page_num);

  cursor->cell_num += 1;
  if (cursor->cell_num >= (*leaf_node_num_cells(node))) {
    uint32_t next_page_num = *leaf_node_next_leaf(node);
    if (next_page_num == 0) {
      cursor->end_of_table = true;
    } else {
      cursor->page_num = next_page_num;
      cursor->cell_num = 0;
    }
  }
}

/* Print functions */
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

void print_tree(Table* table, uint32_t page_num, uint32_t indentation_level) {
  void* node = get_page(table->pager, page_num);
  uint32_t num_keys, child;

  switch (get_node_type(node)) {
    case (NODE_LEAF):
      num_keys = *leaf_node_num_cells(node);
      indent(indentation_level);
      printf("- leaf (size %d)\n", num_keys);
      for (uint32_t i = 0; i < num_keys; i++) {
        indent(indentation_level + 1);
        printf("- %d\n", *leaf_key_t(table, node, i));
      }
      break;
    case (NODE_INTERNAL):
      num_keys = *internal_node_num_keys(node);
      indent(indentation_level);
      printf("- internal (size %d)\n", num_keys);
      if (num_keys > 0) {
        for (uint32_t i = 0; i < num_keys; i++) {
          child = *internal_node_child(node, i);
          print_tree(table, child, indentation_level + 1);
          indent(indentation_level + 1);
          printf("- key %d\n", *internal_node_key(node, i));
        }
        child = *internal_node_right_child(node);
        print_tree(table, child, indentation_level + 1);
      }
      break;
  }
}

/* Database operations */
Table* db_open(const char* filename) {
  Pager* pager = pager_open(filename);

  if (pager->num_pages == 0) {
    get_page(pager, 0);
    catalog_init(pager);
  } else {
    CatalogHeader* hdr = (CatalogHeader*)get_page(pager, 0);
    if (hdr->magic != DB_MAGIC) {
      printf("Invalid DB file (bad magic).\n");
      exit(EXIT_FAILURE);
    }
  }

  Table* table = malloc(sizeof(Table));
  table->pager = pager;
  table->root_page_num = INVALID_PAGE_NUM;

  if (pager && pager->filename) {
    load_schemas_for_db(pager->filename);
  }

  return table;
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
  if (pager->filename) {
    save_schemas_for_db(pager->filename);
    free(pager->filename);
  }
  free(pager);
  free(table);
}

/* Row serialization (dynamic schema) */
void serialize_row_dynamic(Table* t, char* const* values, uint32_t n, void* dest) {
  uint8_t* p = (uint8_t*)dest;
  for (uint32_t i = 0; i < t->active_schema.num_columns; i++) {
    ColumnDef* c = &t->active_schema.columns[i];
    const char* val = (i < n) ? values[i] : "";
    if (c->type == COL_TYPE_INT) {
      int v = 0;
      parse_int(val, &v);
      memcpy(p, &v, 4);
      p += 4;
    } else if (c->type == COL_TYPE_TIMESTAMP) {
      int64_t tv = 0;
      if (val == NULL || val[0] == '\0') {
        tv = (int64_t)time(NULL);
      } else {
        if (parse_int64(val, &tv) != 0) {
          tv = (int64_t)time(NULL);
        }
      }
      memcpy(p, &tv, 8);
      p += 8;
    } else {
      size_t len = strlen(val);
      size_t to_copy = len > c->size ? c->size : len;
      memcpy(p, val, to_copy);
      if (to_copy < c->size) {
        memset(p + to_copy, 0, c->size - to_copy);
      }
      p += c->size;
    }
  }
}

/* Update internal node key */
void update_internal_node_key(void* node, uint32_t old_key, uint32_t new_key) {
  uint32_t old_child_index = internal_node_find_child(node, old_key);
  *internal_node_key(node, old_child_index) = new_key;
}

/* Create a new root node */
void create_new_root(Table* table, uint32_t right_child_page_num) {
  void* root = get_page(table->pager, table->root_page_num);
  void* right_child = get_page(table->pager, right_child_page_num);
  uint32_t left_child_page_num = get_unused_page_num(table->pager);
  void* left_child = get_page(table->pager, left_child_page_num);

  if (get_node_type(root) == NODE_INTERNAL) {
    initialize_internal_node(right_child);
    initialize_internal_node(left_child);
  }

  memcpy(left_child, root, MYDB_PAGE_SIZE);
  set_node_root(left_child, false);

  if (get_node_type(left_child) == NODE_INTERNAL) {
    void* child;
    for (uint32_t i = 0; i < *internal_node_num_keys(left_child); i++) {
      child = get_page(table->pager, *internal_node_child(left_child, i));
      *node_parent(child) = left_child_page_num;
    }
    child = get_page(table->pager, *internal_node_right_child(left_child));
    *node_parent(child) = left_child_page_num;
  }

  initialize_internal_node(root);
  set_node_root(root, true);
  *internal_node_num_keys(root) = 1;
  *internal_node_child(root, 0) = left_child_page_num;
  uint32_t left_child_max_key = get_node_max_key(table, left_child);
  *internal_node_key(root, 0) = left_child_max_key;
  *internal_node_right_child(root) = right_child_page_num;
  *node_parent(left_child) = table->root_page_num;
  *node_parent(right_child) = table->root_page_num;
}

/* Internal node insert and split */
void internal_node_insert(Table* table, uint32_t parent_page_num, uint32_t child_page_num);
void internal_node_split_and_insert(Table* table, uint32_t parent_page_num, uint32_t child_page_num);

void internal_node_insert(Table* table, uint32_t parent_page_num, uint32_t child_page_num) {
  void* parent = get_page(table->pager, parent_page_num);
  void* child = get_page(table->pager, child_page_num);
  uint32_t child_max_key = get_node_max_key(table, child);
  uint32_t index = internal_node_find_child(parent, child_max_key);

  uint32_t original_num_keys = *internal_node_num_keys(parent);

  if (original_num_keys >= INTERNAL_NODE_MAX_KEYS) {
    internal_node_split_and_insert(table, parent_page_num, child_page_num);
    return;
  }

  uint32_t right_child_page_num = *internal_node_right_child(parent);
  if (right_child_page_num == INVALID_PAGE_NUM) {
    *internal_node_right_child(parent) = child_page_num;
    return;
  }

  void* right_child = get_page(table->pager, right_child_page_num);
  *internal_node_num_keys(parent) = original_num_keys + 1;

  if (child_max_key > get_node_max_key(table, right_child)) {
    *internal_node_child(parent, original_num_keys) = right_child_page_num;
    *internal_node_key(parent, original_num_keys) =
        get_node_max_key(table, right_child);
    *internal_node_right_child(parent) = child_page_num;
  } else {
    for (int64_t ii = (int64_t)original_num_keys; ii > (int64_t)index; --ii) {
      uint32_t i = (uint32_t)ii;
      void* destination = internal_node_cell(parent, i);
      void* source = internal_node_cell(parent, i - 1);
      memcpy(destination, source, INTERNAL_NODE_CELL_SIZE);
    }
    *internal_node_child(parent, index) = child_page_num;
    *internal_node_key(parent, index) = child_max_key;
  }
}

void internal_node_split_and_insert(Table* table, uint32_t parent_page_num, uint32_t child_page_num) {
  uint32_t old_page_num = parent_page_num;
  void* old_node = get_page(table->pager, parent_page_num);
  uint32_t old_max = get_node_max_key(table, old_node);

  void* child = get_page(table->pager, child_page_num);
  uint32_t child_max = get_node_max_key(table, child);

  uint32_t new_page_num = get_unused_page_num(table->pager);
  uint32_t splitting_root = is_node_root(old_node);

  void* parent;
  void* new_node;
  if (splitting_root) {
    create_new_root(table, new_page_num);
    parent = get_page(table->pager, table->root_page_num);
    old_page_num = *internal_node_child(parent, 0);
    old_node = get_page(table->pager, old_page_num);
  } else {
    parent = get_page(table->pager, *node_parent(old_node));
    new_node = get_page(table->pager, new_page_num);
    initialize_internal_node(new_node);
  }

  uint32_t* old_num_keys = internal_node_num_keys(old_node);
  uint32_t cur_page_num = *internal_node_right_child(old_node);
  void* cur = get_page(table->pager, cur_page_num);

  internal_node_insert(table, new_page_num, cur_page_num);
  *node_parent(cur) = new_page_num;
  *internal_node_right_child(old_node) = INVALID_PAGE_NUM;

  for (int64_t ii = (int64_t)(INTERNAL_NODE_MAX_KEYS - 1); ii > (int64_t)(INTERNAL_NODE_MAX_KEYS / 2); --ii) {
    uint32_t i = (uint32_t)ii;
    cur_page_num = *internal_node_child(old_node, i);
    cur = get_page(table->pager, cur_page_num);

    internal_node_insert(table, new_page_num, cur_page_num);
    *node_parent(cur) = new_page_num;

    (*old_num_keys)--;
  }

  *internal_node_right_child(old_node) = *internal_node_child(old_node, *old_num_keys - 1);
  (*old_num_keys)--;

  uint32_t max_after_split = get_node_max_key(table, old_node);
  uint32_t destination_page_num = child_max < max_after_split ? old_page_num : new_page_num;

  internal_node_insert(table, destination_page_num, child_page_num);
  *node_parent(child) = destination_page_num;

  update_internal_node_key(parent, old_max, get_node_max_key(table, old_node));

  if (!splitting_root) {
    internal_node_insert(table, *node_parent(old_node), new_page_num);
    *node_parent(new_node) = *node_parent(old_node);
  }
}

/* Leaf node insert and split */
void leaf_node_insert(Cursor* cursor, uint32_t key, char* const* values, uint32_t nvals) {
  fprintf(stderr, "[DEBUG-LEAF-INSERT] leaf_node_insert: key=%u, page_num=%u, cell_num=%u\n",
          key, cursor->page_num, cursor->cell_num);
  fflush(stderr);

  void* node = get_page(cursor->table->pager, cursor->page_num);

  uint32_t num_cells = *leaf_node_num_cells(node);
  uint32_t max_cells = leaf_max_cells(cursor->table);
  fprintf(stderr, "[DEBUG-LEAF-INSERT] leaf_node_insert: current cells=%u, max_cells=%u\n",
          num_cells, max_cells);
  fflush(stderr);

  if (num_cells >= max_cells) {
    fprintf(stderr, "[DEBUG-LEAF-INSERT] leaf_node_insert: node is full, splitting\n");
    fflush(stderr);
    leaf_node_split_and_insert(cursor, key, values, nvals);
    return;
  }

  if (cursor->cell_num < num_cells) {
    fprintf(stderr, "[DEBUG-LEAF-INSERT] leaf_node_insert: shifting cells from %u to %u\n",
            cursor->cell_num, num_cells);
    fflush(stderr);
    for (int64_t ii = (int64_t)num_cells; ii > (int64_t)cursor->cell_num; --ii) {
      uint32_t i = (uint32_t)ii;
      memcpy(leaf_cell_t(cursor->table, node, i),
             leaf_cell_t(cursor->table, node, i - 1),
             leaf_cell_size(cursor->table));
    }
  } else {
    fprintf(stderr, "[DEBUG-LEAF-INSERT] leaf_node_insert: inserting at end (cell_num=%u, num_cells=%u)\n",
            cursor->cell_num, num_cells);
    fflush(stderr);
  }

  fprintf(stderr, "[DEBUG-LEAF-INSERT] leaf_node_insert: updating cell count from %u to %u\n",
          num_cells, num_cells + 1);
  fflush(stderr);
  *(leaf_node_num_cells(node)) += 1;
  *(leaf_key_t(cursor->table, node, cursor->cell_num)) = key;
  serialize_row_dynamic(cursor->table, values, nvals,
                        leaf_value_t(cursor->table, node, cursor->cell_num));

  uint32_t final_cells = *leaf_node_num_cells(node);
  fprintf(stderr, "[DEBUG-LEAF-INSERT] leaf_node_insert: completed, final cell count=%u\n",
          final_cells);
  fflush(stderr);
}

void leaf_node_split_and_insert(Cursor* cursor, uint32_t key, char* const* values, uint32_t nvals) {
  void* old_node = get_page(cursor->table->pager, cursor->page_num);
  uint32_t old_max = get_node_max_key(cursor->table, old_node);
  uint32_t new_page_num = get_unused_page_num(cursor->table->pager);
  void* new_node = get_page(cursor->table->pager, new_page_num);
  initialize_leaf_node(new_node);
  *node_parent(new_node) = *node_parent(old_node);
  *leaf_node_next_leaf(new_node) = *leaf_node_next_leaf(old_node);
  *leaf_node_next_leaf(old_node) = new_page_num;

  for (int64_t ii = (int64_t)leaf_max_cells(cursor->table); ii >= 0; --ii) {
    uint32_t i = (uint32_t)ii;
    void* destination_node;
    if (i >= leaf_left_split_count(cursor->table)) {
      destination_node = new_node;
    } else {
      destination_node = old_node;
    }
    uint32_t index_within_node = i % leaf_left_split_count(cursor->table);
    void* destination = leaf_cell_t(cursor->table, destination_node, index_within_node);

    if (i == (uint32_t)cursor->cell_num) {
      serialize_row_dynamic(cursor->table, values, nvals,
                            leaf_value_t(cursor->table, destination_node, index_within_node));
      *leaf_key_t(cursor->table, destination_node, index_within_node) = key;
    } else if (i > (uint32_t)cursor->cell_num) {
      memcpy(destination, leaf_cell_t(cursor->table, old_node, (uint32_t)i - 1), leaf_cell_size(cursor->table));
    } else {
      memcpy(destination, leaf_cell_t(cursor->table, old_node, (uint32_t)i), leaf_cell_size(cursor->table));
    }
  }

  *(leaf_node_num_cells(old_node)) = leaf_left_split_count(cursor->table);
  *(leaf_node_num_cells(new_node)) = leaf_right_split_count(cursor->table);

  if (is_node_root(old_node)) {
    return create_new_root(cursor->table, new_page_num);
  } else {
    uint32_t parent_page_num = *node_parent(old_node);
    uint32_t new_max = get_node_max_key(cursor->table, old_node);
    void* parent = get_page(cursor->table->pager, parent_page_num);

    update_internal_node_key(parent, old_max, new_max);
    internal_node_insert(cursor->table, parent_page_num, new_page_num);
    return;
  }
}

/* Delete and merge operations */
int find_child_index_in_parent(void* parent, uint32_t child_page_num) {
  uint32_t num_keys = *internal_node_num_keys(parent);

  for (uint32_t i = 0; i < num_keys; i++) {
    if (*internal_node_child(parent, i) == child_page_num) {
      return (int)i;
    }
  }

  if (*internal_node_right_child(parent) == child_page_num) {
    return (int)num_keys;
  }

  return -1;
}

void find_siblings(Table* table, uint32_t page_num,
                  uint32_t* left_sibling, uint32_t* right_sibling) {
  *left_sibling = INVALID_PAGE_NUM;
  *right_sibling = INVALID_PAGE_NUM;

  void* node = get_page(table->pager, page_num);

  if (is_node_root(node)) {
    return;
  }

  uint32_t parent_page_num = *node_parent(node);
  void* parent = get_page(table->pager, parent_page_num);

  int my_index = find_child_index_in_parent(parent, page_num);
  if (my_index < 0) {
    return;
  }

  uint32_t num_keys = *internal_node_num_keys(parent);

  if (my_index > 0) {
    *left_sibling = *internal_node_child(parent, my_index - 1);
  }

  if (my_index < (int)num_keys) {
    *right_sibling = *internal_node_child(parent, my_index + 1);
  }
}

void internal_node_remove_child(Table* table, void* parent, uint32_t child_page_num) {
  uint32_t num_keys = *internal_node_num_keys(parent);
  int child_index = find_child_index_in_parent(parent, child_page_num);

  if (child_index < 0) {
    return;
  }

  fprintf(stderr, "[MERGE] Removing child at index %d from parent\n", child_index);

  if (child_index == (int)num_keys) {
    if (num_keys > 0) {
      *internal_node_right_child(parent) = *internal_node_child(parent, num_keys - 1);
      (*internal_node_num_keys(parent))--;
    } else {
      *internal_node_right_child(parent) = INVALID_PAGE_NUM;
    }
    return;
  }

  for (uint32_t i = child_index; i < num_keys - 1; i++) {
    void* dest = internal_node_cell(parent, i);
    void* src = internal_node_cell(parent, i + 1);
    memcpy(dest, src, INTERNAL_NODE_CELL_SIZE);
  }

  if (num_keys > 1) {
    uint32_t right_child = *internal_node_right_child(parent);
    *internal_node_child(parent, num_keys - 1) = right_child;

    void* right_node = get_page(table->pager, right_child);
    uint32_t right_max = get_node_max_key(table, right_node);
    *internal_node_key(parent, num_keys - 1) = right_max;
  }

  (*internal_node_num_keys(parent))--;
}

void leaf_node_merge_with_right(Table* table, uint32_t left_page_num, uint32_t right_page_num) {
  fprintf(stderr, "[MERGE] Merging leaf nodes %u + %u\n", left_page_num, right_page_num);

  void* left_node = get_page(table->pager, left_page_num);
  void* right_node = get_page(table->pager, right_page_num);

  uint32_t left_cells = *leaf_node_num_cells(left_node);
  uint32_t right_cells = *leaf_node_num_cells(right_node);

  for (uint32_t i = 0; i < right_cells; i++) {
    void* src = leaf_cell_t(table, right_node, i);
    void* dest = leaf_cell_t(table, left_node, left_cells + i);
    memcpy(dest, src, leaf_cell_size(table));
  }

  *leaf_node_num_cells(left_node) = left_cells + right_cells;

  uint32_t right_next = *leaf_node_next_leaf(right_node);
  *leaf_node_next_leaf(left_node) = right_next;

  *leaf_node_num_cells(right_node) = 0;

  fprintf(stderr, "[MERGE] Merge complete, left node now has %u cells\n",
          *leaf_node_num_cells(left_node));
}

void leaf_node_merge_with_left(Table* table, uint32_t left_page_num, uint32_t right_page_num) {
  leaf_node_merge_with_right(table, left_page_num, right_page_num);
}

void handle_underflow(Table* table, uint32_t page_num) {
  fprintf(stderr, "[MERGE] handle_underflow: checking page %u\n", page_num);

  void* node = get_page(table->pager, page_num);
  uint32_t num_cells = *leaf_node_num_cells(node);

  if (num_cells > 0) {
    fprintf(stderr, "[MERGE] Node still has %u cells, no merge needed\n", num_cells);
    return;
  }

  if (is_node_root(node)) {
    fprintf(stderr, "[MERGE] Root node is empty, tree is now empty\n");
    return;
  }

  fprintf(stderr, "[MERGE] Node is empty, starting merge process\n");

  uint32_t left_sibling = INVALID_PAGE_NUM;
  uint32_t right_sibling = INVALID_PAGE_NUM;
  find_siblings(table, page_num, &left_sibling, &right_sibling);

  uint32_t parent_page_num = *node_parent(node);
  void* parent = get_page(table->pager, parent_page_num);

  if (right_sibling != INVALID_PAGE_NUM) {
    if (left_sibling != INVALID_PAGE_NUM) {
      void* left_node = get_page(table->pager, left_sibling);
      *leaf_node_next_leaf(left_node) = right_sibling;
    }
  } else if (left_sibling != INVALID_PAGE_NUM) {
    void* left_node = get_page(table->pager, left_sibling);
    uint32_t my_next = *leaf_node_next_leaf(node);
    *leaf_node_next_leaf(left_node) = my_next;
  }

  internal_node_remove_child(table, parent, page_num);

  uint32_t parent_num_keys = *internal_node_num_keys(parent);
  fprintf(stderr, "[MERGE] Parent node now has %u keys\n", parent_num_keys);

  if (parent_num_keys == 0 && !is_node_root(parent)) {
    fprintf(stderr, "[MERGE] Parent node is also empty, recursing\n");
    handle_underflow(table, parent_page_num);
  }

  if (is_node_root(parent) && parent_num_keys == 0) {
    uint32_t only_child = *internal_node_right_child(parent);
    if (only_child != INVALID_PAGE_NUM) {
      fprintf(stderr, "[MERGE] Root node has only one child, promoting to new root\n");

      void* child_node = get_page(table->pager, only_child);

      memcpy(parent, child_node, MYDB_PAGE_SIZE);
      set_node_root(parent, true);

      fprintf(stderr, "[MERGE] Tree height reduced\n");
    }
  }
}

