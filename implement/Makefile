# Makefile for implement/ directory
# Builds the implementation sources into `mydb` executable

SRCS = db.c sql_lexer.c sql_parser.c
OBJS = $(SRCS:.c=.o)

CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -g
TARGET = mydb

.PHONY: all run clean format

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET) ../mydb.db

clean:
	rm -f $(TARGET) *.o *.db

format:
	clang-format -style=Google -i *.c
