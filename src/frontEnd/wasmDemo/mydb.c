#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void* MYDB_Handle;

typedef struct{
    char filename[256];
    int is_open;
} MyDB;


MYDB_Handle mydb_open(const char* filename){
    printf("execute mydb_open");
    MyDB* db = (MyDB*)malloc(sizeof(MyDB));
    snprintf(db->filename,sizeof(db->filename),"%s",filename);
    db->is_open = 1;
    printf("Opened DB: %s\n", db->filename);
    return (MYDB_Handle)db;
}

void mydb_close(MYDB_Handle h) {
    printf("execute mydb_close");
    MyDB* db = (MyDB*)h;
    if (db && db->is_open) {
        printf("Closed DB: %s\n", db->filename);
        db->is_open = 0;
        free(db);
    }
}



int mydb_execute_json(MYDB_Handle h, const char* sql, char** out_json) {
    printf("execute mydb_execute_json");
    MyDB* db = (MyDB*)h;
    if (!db || !db->is_open) {
        return -1;
    }

    // 简单返回一个 JSON 字符串，实际可以替换为真实查询结果
    const char* result = "{\"status\":\"ok\",\"sql\":\"executed\"}";
    *out_json = (char*)malloc(strlen(result) + 1);
    strcpy(*out_json, result);
    printf("Executed SQL: %s\n", sql);
    return 0;
}


/**
命令：


emcc mydb.c -o mydb.js \
  -s MODULARIZE=1 \
  -s EXPORT_NAME="MyDB" \
  -s EXPORTED_FUNCTIONS='["_mydb_open","_mydb_close","_mydb_execute_json","_malloc","_free"]' \
  -s EXPORTED_RUNTIME_METHODS='["cwrap","ccall","UTF8ToString","stringToUTF8","lengthBytesUTF8","getValue","setValue"]'


  
 */