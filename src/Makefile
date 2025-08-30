SRCS = db.c sql_lexer.c sql_parser.c
BIN_DIR = bin
OBJS = $(patsubst %.c,$(BIN_DIR)/%.o,$(SRCS))

CC = gcc
# macos CFLAGS = -std=c11 -Wall -Wextra -g
#linux:
CFLAGS = -std=gnu11 -Wall -Wextra -g
TARGET = $(BIN_DIR)/mydb

LIB_DIR = $(BIN_DIR)
LIBNAME = mydb
LIB_SRCS = $(SRCS)
# PIC objects for building shared/static libs (compile with -fPIC and -DBUILDING_MYDB_LIB)
LIB_OBJS = $(patsubst %.c,$(BIN_DIR)/pic_%.o,$(LIB_SRCS))

.PHONY: all lib shared static run clean format run_wasm

all: $(TARGET)

$(TARGET): $(OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

# normal object rule (for the executable)
$(BIN_DIR)/%.o: %.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# PIC object rule used only by library targets
PIC_CFLAGS = $(CFLAGS) -fPIC -DBUILDING_MYDB_LIB
$(BIN_DIR)/pic_%.o: %.c | $(BIN_DIR)
	$(CC) $(PIC_CFLAGS) -c $< -o $@

# Shared library
$(LIB_DIR)/lib$(LIBNAME).so: $(LIB_OBJS) | $(LIB_DIR)
	$(CC) -shared -o $@ $(LIB_OBJS)

# Static archive (using PIC objects is fine)
$(LIB_DIR)/lib$(LIBNAME).a: $(LIB_OBJS) | $(LIB_DIR)
	ar rcs $@ $(LIB_OBJS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(LIB_DIR):
	mkdir -p $(LIB_DIR)

run: $(TARGET)
	./$(TARGET) ../mydb.db

clean:
	rm -f $(TARGET) $(BIN_DIR)/*.o $(BIN_DIR)/pic_*.o $(LIB_DIR)/* *.db

format:
	clang-format -style=Google -i *.c

# Emscripten / WASM build
EMCC = emcc
EMFLAGS = -O2 -s WASM=1 -s ALLOW_MEMORY_GROWTH=1 -s MODULARIZE=1 \
	-s 'EXPORT_NAME="MyDB"' \
	-s EXPORTED_FUNCTIONS='["_mydb_open_with_ems","_mydb_close_with_ems","_mydb_execute_json_with_ems","_malloc","_free"]' \
	-s EXPORTED_RUNTIME_METHODS='["cwrap","ccall","UTF8ToString","stringToUTF8","lengthBytesUTF8","getValue","setValue","FS"]'

$(BIN_DIR)/$(LIBNAME).js: $(SRCS) | $(BIN_DIR)
	@mkdir -p $(BIN_DIR)
	$(EMCC) $(CFLAGS) -DBUILDING_MYDB_LIB $(SRCS) -o $@ $(EMFLAGS)


run_wasm: $(BIN_DIR)/$(LIBNAME).js
	@echo "Built WebAssembly module at $(BIN_DIR)/$(LIBNAME).js and .wasm"
	@echo "Serve the $(BIN_DIR) directory via HTTP (e.g. 'python3 -m http.server --directory $(BIN_DIR) 8000') and use /persistent for IDBFS."