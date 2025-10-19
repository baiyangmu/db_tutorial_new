#include "../include/util.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

void test_parse_int() {
    printf("Running test_parse_int...\n");
    
    int value;
    
    // Valid integers
    assert(parse_int("123", &value) == 0);
    assert(value == 123);
    
    assert(parse_int("-456", &value) == 0);
    assert(value == -456);
    
    assert(parse_int("0", &value) == 0);
    assert(value == 0);
    
    // Invalid integers
    assert(parse_int("abc", &value) != 0);
    assert(parse_int("12.34", &value) != 0);
    assert(parse_int("", &value) != 0);
    assert(parse_int("123abc", &value) != 0);
    
    printf("  ✓ test_parse_int passed\n");
}

void test_parse_int64() {
    printf("Running test_parse_int64...\n");
    
    int64_t value;
    
    // Valid integers
    assert(parse_int64("1234567890", &value) == 0);
    assert(value == 1234567890);
    
    assert(parse_int64("-9876543210", &value) == 0);
    assert(value == -9876543210);
    
    // Invalid integers
    assert(parse_int64("not_a_number", &value) != 0);
    
    printf("  ✓ test_parse_int64 passed\n");
}

void test_string_buffer() {
    printf("Running test_string_buffer...\n");
    
    StrBuf sb;
    sb_init(&sb);
    
    assert(sb.buf != NULL);
    assert(sb.len == 0);
    assert(sb.cap > 0);
    
    sb_append(&sb, "Hello");
    assert(strcmp(sb.buf, "Hello") == 0);
    assert(sb.len == 5);
    
    sb_append(&sb, " World");
    assert(strcmp(sb.buf, "Hello World") == 0);
    assert(sb.len == 11);
    
    sb_appendf(&sb, "! %d", 123);
    assert(strcmp(sb.buf, "Hello World! 123") == 0);
    
    sb_free(&sb);
    assert(sb.buf == NULL);
    
    printf("  ✓ test_string_buffer passed\n");
}

void test_json_escape() {
    printf("Running test_json_escape...\n");
    
    StrBuf sb;
    sb_init(&sb);
    
    json_escape_append(&sb, "hello");
    assert(strcmp(sb.buf, "\"hello\"") == 0);
    
    sb_free(&sb);
    sb_init(&sb);
    
    json_escape_append(&sb, "test\"quote");
    assert(strstr(sb.buf, "\\\"") != NULL);
    
    sb_free(&sb);
    
    printf("  ✓ test_json_escape passed\n");
}

int main() {
    printf("\n=== Running Util Tests ===\n\n");
    
    test_parse_int();
    test_parse_int64();
    test_string_buffer();
    test_json_escape();
    
    printf("\n=== All Util Tests Passed ===\n\n");
    return 0;
}

