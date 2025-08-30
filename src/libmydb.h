#ifndef LIBMYDB_H
#define LIBMYDB_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void* MYDB_Handle;

MYDB_Handle mydb_open(const char* filename);
void mydb_close(MYDB_Handle h);
int mydb_execute_json(MYDB_Handle h, const char* sql, char** out_json);

/* Emscripten-specific variants (available when building with Emscripten)
   These are implemented in `db.c` and exported for the WASM build. */
MYDB_Handle mydb_open_with_ems(const char* filename);
void mydb_close_with_ems(MYDB_Handle h);
int mydb_execute_json_with_ems(MYDB_Handle h, const char* sql, char** out_json);

#ifdef __cplusplus
}
#endif

#endif