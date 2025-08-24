# Makefile for implement/ directory
# Builds the implementation sources into `mydb` executable

SRCS = db.c sql_lexer.c sql_parser.c
BIN_DIR = bin
OBJS = $(patsubst %.c,$(BIN_DIR)/%.o,$(SRCS))

CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -g
TARGET = $(BIN_DIR)/mydb

.PHONY: all run clean format

all: $(TARGET)

$(TARGET): $(OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BIN_DIR)/%.o: %.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET) ../mydb.db

clean:
	rm -f $(TARGET) $(BIN_DIR)/*.o *.db

format:
	clang-format -style=Google -i *.c
