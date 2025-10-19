#include "../include/schema.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

void test_parse_column_type() {
    printf("Running test_parse_column_type...\n");
    
    assert(parse_column_type("int") == COL_TYPE_INT);
    assert(parse_column_type("string") == COL_TYPE_STRING);
    assert(parse_column_type("timestamp") == COL_TYPE_TIMESTAMP);
    assert(parse_column_type("unknown") == COL_TYPE_INT); // default
    
    printf("  ✓ test_parse_column_type passed\n");
}

void test_schema_col_index() {
    printf("Running test_schema_col_index...\n");
    
    TableSchema schema;
    schema.num_columns = 3;
    strcpy(schema.columns[0].name, "id");
    strcpy(schema.columns[1].name, "name");
    strcpy(schema.columns[2].name, "email");
    
    assert(schema_col_index(&schema, "id") == 0);
    assert(schema_col_index(&schema, "name") == 1);
    assert(schema_col_index(&schema, "email") == 2);
    assert(schema_col_index(&schema, "nonexistent") == -1);
    
    printf("  ✓ test_schema_col_index passed\n");
}

void test_compute_row_size() {
    printf("Running test_compute_row_size...\n");
    
    TableSchema schema;
    schema.num_columns = 3;
    schema.columns[0].type = COL_TYPE_INT;
    schema.columns[0].size = 4;
    schema.columns[1].type = COL_TYPE_STRING;
    schema.columns[1].size = 32;
    schema.columns[2].type = COL_TYPE_STRING;
    schema.columns[2].size = 255;
    
    uint32_t size = compute_row_size(&schema);
    assert(size == 4 + 32 + 255);
    
    printf("  ✓ test_compute_row_size passed\n");
}

void test_schema_col_offset() {
    printf("Running test_schema_col_offset...\n");
    
    TableSchema schema;
    schema.num_columns = 3;
    schema.columns[0].type = COL_TYPE_INT;
    schema.columns[0].size = 4;
    schema.columns[1].type = COL_TYPE_STRING;
    schema.columns[1].size = 32;
    schema.columns[2].type = COL_TYPE_STRING;
    schema.columns[2].size = 255;
    
    assert(schema_col_offset(&schema, 0) == 0);
    assert(schema_col_offset(&schema, 1) == 4);
    assert(schema_col_offset(&schema, 2) == 4 + 32);
    
    printf("  ✓ test_schema_col_offset passed\n");
}

int main() {
    printf("\n=== Running Schema Tests ===\n\n");
    
    test_parse_column_type();
    test_schema_col_index();
    test_compute_row_size();
    test_schema_col_offset();
    
    printf("\n=== All Schema Tests Passed ===\n\n");
    return 0;
}

