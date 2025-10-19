#ifndef MYDB_REPL_H
#define MYDB_REPL_H

#include "btree.h"
#include "sql_executor.h"

/* Meta command results */
typedef enum {
  META_COMMAND_SUCCESS,
  META_COMMAND_UNRECOGNIZED_COMMAND
} MetaCommandResult;

/* Input buffer operations */
InputBuffer* new_input_buffer(void);
void read_input(InputBuffer* input_buffer);
void close_input_buffer(InputBuffer* input_buffer);
void print_prompt(void);

/* Meta commands */
MetaCommandResult do_meta_command(InputBuffer* input_buffer, Table* table);

#endif /* MYDB_REPL_H */

