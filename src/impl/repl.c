#include "../include/repl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Create new input buffer */
InputBuffer* new_input_buffer(void) {
  InputBuffer* input_buffer = malloc(sizeof(InputBuffer));
  input_buffer->buffer = NULL;
  input_buffer->buffer_length = 0;
  input_buffer->input_length = 0;
  return input_buffer;
}

/* Print prompt */
void print_prompt(void) {
  printf("db > ");
}

/* Read input from stdin */
void read_input(InputBuffer* input_buffer) {
  ssize_t bytes_read =
      getline(&(input_buffer->buffer), &(input_buffer->buffer_length), stdin);

  if (bytes_read <= 0) {
    printf("Error reading input\n");
    exit(EXIT_FAILURE);
  }

  /* Ignore trailing newline */
  input_buffer->input_length = bytes_read - 1;
  input_buffer->buffer[bytes_read - 1] = 0;
}

/* Close input buffer */
void close_input_buffer(InputBuffer* input_buffer) {
  free(input_buffer->buffer);
  free(input_buffer);
}

/* Handle meta commands (.exit, .btree, .constants) */
MetaCommandResult do_meta_command(InputBuffer* input_buffer, Table* table) {
  if (strcmp(input_buffer->buffer, ".exit") == 0) {
    close_input_buffer(input_buffer);
    db_close(table);
    exit(EXIT_SUCCESS);
  } else if (strcmp(input_buffer->buffer, ".btree") == 0) {
    if (table->root_page_num == INVALID_PAGE_NUM) {
      printf("No active table. Use 'use <table>' first.\n");
    } else {
      printf("Tree:\n");
      print_tree(table, table->root_page_num, 0);
    }
    return META_COMMAND_SUCCESS;
  } else if (strcmp(input_buffer->buffer, ".constants") == 0) {
    printf("Constants:\n");
    print_constants(table);
    return META_COMMAND_SUCCESS;
  } else {
    return META_COMMAND_UNRECOGNIZED_COMMAND;
  }
}

