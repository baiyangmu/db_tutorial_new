#ifndef SQL_PARSER_H
#define SQL_PARSER_H

#include <stdint.h>
#include "sql_ast.h"

#define PARSED_TABLE_NAME_LEN 64

#define PARSED_MAX_PROJ 16
#define PARSED_MAX_PROJ_NAME_LEN 64

typedef enum{
    PARSED_SELECT,
    PARSED_INSERT,
    PARSED_UPDATE,
    PARSED_DELETE,
    PARSED_USE,
    PARSED_UNKNOWN
} ParsedKind;


typedef struct{
    ParsedKind kind;
    char table_name[PARSED_TABLE_NAME_LEN];
    char proj_list[PARSED_MAX_PROJ][PARSED_MAX_PROJ_NAME_LEN];
    uint32_t proj_count;
    int select_all;
    Expr* where;


    char insert_table[PARSED_TABLE_NAME_LEN];
    char** insert_value;
    uint32_t insert_n;

    uint32_t limit;
    int has_limit;
    uint32_t offset;
    int has_offset;
    char order_by[PARSED_MAX_PROJ_NAME_LEN];
    int order_desc;
    char table_alias[PARSED_TABLE_NAME_LEN];

} ParsedStmt;

int parse_sql_to_parsed_stmt(const char* sql,ParsedStmt* out);
void parsed_stmt_free(ParsedStmt* ps);


#endif