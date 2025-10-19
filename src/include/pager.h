#ifndef MYDB_PAGER_H
#define MYDB_PAGER_H

#include <stdint.h>
#include "util.h"

/* Pager structure */
typedef struct {
  int file_descriptor;
  uint32_t file_length;
  uint32_t num_pages;
  void* pages[TABLE_MAX_PAGES];
  char* filename;
} Pager;

/* Pager operations */
Pager* pager_open(const char* filename);
void* get_page(Pager* pager, uint32_t page_num);
void pager_flush(Pager* pager, uint32_t page_num);
uint32_t get_unused_page_num(Pager* pager);

#endif /* MYDB_PAGER_H */

