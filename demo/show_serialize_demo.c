#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

typedef enum { COL_TYPE_INT, COL_TYPE_STRING } ColumType;

typedef struct {
  ColumType type;
  uint32_t size; // 对字符串列有效，表示定长字节数
} ColumnDef;

typedef struct {
  uint32_t num_columns;
  ColumnDef columns[3];
} TableSchema;

typedef struct {
  TableSchema active_schema;
  uint32_t row_size;
} Table;

static inline uint32_t compute_row_size(const TableSchema* s){
  uint32_t sz = 0;
  for (uint32_t i = 0; i < s->num_columns; i++) {
    sz += (s->columns[i].type == COL_TYPE_INT) ? 4 : s->columns[i].size;
  }
  return sz;
}

static int parse_int(const char* s, int* out){
  char* end = NULL;
  long v = strtol(s, &end, 10);
  if (end == s || *end != '\0') return -1;
  if (v < INT32_MIN || v > INT32_MAX) return -1;
  *out = (int)v;
  return 0;
}

static void serialize_row_dynamic(Table* t, char* const* values, uint32_t n, void* dest){
  uint8_t* p = (uint8_t*)dest;
  for (uint32_t i = 0; i < t->active_schema.num_columns; i++) {
    const ColumnDef* c = &t->active_schema.columns[i];
    const char* val = (i < n) ? values[i] : "";
    if (c->type == COL_TYPE_INT) {
      int v = 0;
      (void)parse_int(val, &v);          // 解析失败时按 0 写入
      memcpy(p, &v, 4);
      p += 4;
    } else {
      size_t len = strlen(val);
      size_t to_copy = len > c->size ? c->size : len;
      memcpy(p, val, to_copy);           // 先拷贝有效内容
      if (to_copy < c->size) {
        memset(p + to_copy, 0, c->size - to_copy); // 余量零填充
      }
      p += c->size;
    }
  }
}

static void print_row_dynamic(Table* t, const void* src){
  const uint8_t* p = (const uint8_t*)src;
  printf("(");
  for (uint32_t i = 0; i < t->active_schema.num_columns; i++) {
    const ColumnDef* c = &t->active_schema.columns[i];
    if (i) printf(", ");
    if (c->type == COL_TYPE_INT) {
      int v;
      memcpy(&v, p, 4);
      p += 4;
      printf("%d", v);
    } else {
      char buf[512];
      size_t m = c->size < sizeof(buf) - 1 ? c->size : sizeof(buf) - 1;
      memcpy(buf, p, m);
      buf[m] = 0;
      size_t r = m;
      while (r > 0 && buf[r - 1] == 0) r--;
      buf[r] = 0;
      printf("%s", buf);
      p += c->size;
    }
  }
  printf(")\n");
}

int main(void){
  // 定义 schema: id(int4), username(string16), email(string32)
  Table t = {0};
  t.active_schema.num_columns = 3;
  t.active_schema.columns[0] = (ColumnDef){ .type = COL_TYPE_INT, .size = 4 };
  t.active_schema.columns[1] = (ColumnDef){ .type = COL_TYPE_STRING, .size = 16 };
  t.active_schema.columns[2] = (ColumnDef){ .type = COL_TYPE_STRING, .size = 32 };
  t.row_size = compute_row_size(&t.active_schema);

  // 准备一行数据
  char* values[] = { "1", "alice", "alice@example.com" };
  uint8_t row[128] = {0}; // 只需保证 >= t.row_size
  serialize_row_dynamic(&t, values, 3, row);

  // 打印该行
  print_row_dynamic(&t, row);
  return 0;
}