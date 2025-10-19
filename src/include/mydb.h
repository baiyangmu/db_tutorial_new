#ifndef MYDB_H
#define MYDB_H

/* Public library API for mydb */

/* Opaque handle for database connection */
typedef void* MYDB_Handle;

/* Open/Close operations */
MYDB_Handle mydb_open(const char* filename);
MYDB_Handle mydb_open_with_ems(const char* filename);
void mydb_close(MYDB_Handle h);
void mydb_close_with_ems(MYDB_Handle h);

/* Execute SQL and get JSON result */
int mydb_execute_json(MYDB_Handle h, const char* sql, char** out_json);
int mydb_execute_json_with_ems(MYDB_Handle h, const char* sql, char** out_json);

#endif /* MYDB_H */

