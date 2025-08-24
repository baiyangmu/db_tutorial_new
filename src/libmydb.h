#ifndef LIBMYDB_H
#define LIBMYDB_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void* MYDB_Handle;

MYDB_Handle mydb_open(const char* filename);
void mydb_close(MYDB_Handle h);
int mydb_execute_json(MYDB_Handle h, const char* sql, char** out_json);

#ifdef __cplusplus
}
#endif

#endif