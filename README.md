# MyDB - 从零构建的简单数据库

一个从头开始实现的轻量级关系型数据库引擎，具有 B-Tree 存储、SQL 解析和多表支持。本项目既可作为独立程序运行，也可作为库被其他程序调用（包括编译为 WebAssembly）。

## 📋 目录

- [核心特性](#核心特性)
- [快速开始](#快速开始)
- [使用方法](#使用方法)
- [SQL 语法支持](#sql-语法支持)
- [库模式使用](#库模式使用)
- [技术实现](#技术实现)
- [当前限制](#当前限制)
- [编译说明](#编译说明)

## 🎯 核心特性

### 1. 多表目录（Multi-Table Catalog）
- 持久化目录位于 page 0，存储所有表的元数据
- 单个 DB 文件支持最多 32 张表
- 使用 `use <table>` 命令切换当前活动表
- Schema 信息嵌入到 DB 文件中，无需外部配置文件

### 2. 灵活的 Schema 定义
- 每张表最多支持 **100 列**
- 支持三种数据类型：
  - `int`：4 字节整数
  - `string`：定长字符串（最大 255 字节）
  - `timestamp`：8 字节 Unix 时间戳（自动记录当前时间）
- 行大小根据 schema 在运行时动态计算

### 3. B-Tree 存储引擎
- 完整的 B-Tree 实现（内部节点 + 叶子节点）
- 自动节点分裂（leaf split 和 internal split）
- 4KB 页面大小，支持最多 400 页
- 页面缓存机制，按需加载和刷写
- 主键索引（第一列必须为 int 类型）

### 4. SQL 解析器
- 支持 `CREATE TABLE`、`INSERT`、`SELECT`、`DELETE` 语句
- WHERE 子句生成 AST（抽象语法树）
- 支持复杂表达式：
  - 比较运算符：`=`、`!=`、`<`、`<=`、`>`、`>=`
  - 逻辑运算符：`AND`、`OR`、`NOT`
  - 特殊运算符：`BETWEEN`、`IN`、`IS NULL`、`IS NOT NULL`

### 5. 强大的查询功能
- **投影**：支持 `SELECT *` 或指定列名
- **过滤**：WHERE 子句支持多条件组合
- **排序**：`ORDER BY column [ASC|DESC]`
- **分页**：`LIMIT` 和 `OFFSET` 支持
- **点查优化**：主键相等查询走快速路径（O(log n) vs O(n)）

### 6. 删除操作
- 按主键删除记录
- 自动更新父节点的 key 值
- 支持复杂的 WHERE 条件（基于 AST 求值）

### 7. 多种运行模式
- **交互式 REPL**：命令行交互界面
- **C 库**：通过头文件 `libmydb.h` 被其他程序调用
- **WebAssembly**：编译为 WASM 在浏览器中运行
- **JSON API**：提供 JSON 格式的结果输出

## 🚀 快速开始

### 编译

```bash
cd src
make
```

### 运行

```bash
./db mytest.db
```

## 📖 使用方法

### 基础操作流程

```sql
-- 1. 创建表
db > create table users (id int, name string, email string, created_at timestamp)
Table 'users' created with 4 columns.

-- 2. 激活表（切换到该表）
db > use users
Using table 'users'.

-- 3. 插入数据
db > insert into users 1 Alice alice@example.com 1704067200
Executed.

-- timestamp 列可省略，自动使用当前时间
db > insert into users 2 Bob bob@example.com
Executed.

-- 4. 查询数据
db > select * from users
(1, Alice, alice@example.com, 1704067200)
(2, Bob, bob@example.com, 1734153600)
Executed.

-- 5. 条件查询
db > select name, email from users where id = 1
(Alice, alice@example.com)
Executed.

-- 6. 排序和分页
db > select * from users order by id desc limit 1
(2, Bob, bob@example.com, 1734153600)
Executed.

-- 7. 删除数据
db > delete from users where id = 2
Executed.

-- 8. 退出
db > .exit
```

### 元命令

```bash
.exit         # 退出程序（保存所有数据）
.btree        # 查看当前表的 B-Tree 结构
.constants    # 显示内部常量（页面大小、单元格大小等）
```

## 📝 SQL 语法支持

### CREATE TABLE

```sql
create table <table_name> (
    <col1> <type>,
    <col2> <type>,
    ...
)

-- 示例
create table products (id int, name string, price int, stock int)
create table logs (id int, message string, timestamp timestamp)
```

**注意**：
- 第一列必须是 `int` 类型，作为主键
- `string` 类型默认为 255 字节
- `timestamp` 类型为 8 字节整数

### INSERT

```sql
-- 方式1：插入到当前活动表
insert <value1> <value2> ...

-- 方式2：指定表名
insert into <table_name> <value1> <value2> ...

-- 示例
insert 1 Alice alice@example.com
insert into users 2 Bob bob@example.com
insert into logs 1 "Server started"  -- timestamp 自动填充
```

### SELECT

```sql
select <columns> from <table_name>
    [where <condition>]
    [order by <column> [asc|desc]]
    [limit <n>]
    [offset <n>]

-- 示例
select * from users
select id, name from users where id > 10
select * from users where name = Alice and id < 100
select * from users order by id desc limit 10 offset 20
select * from users where id between 1 and 100
select name from users where email is not null
```

**WHERE 条件支持**：
- 比较：`=`, `!=`, `<`, `<=`, `>`, `>=`
- 逻辑：`AND`, `OR`, `NOT`
- 范围：`BETWEEN x AND y`
- 集合：`IN (value1, value2, ...)`
- 空值：`IS NULL`, `IS NOT NULL`

### DELETE

```sql
delete from <table_name> where <condition>

-- 示例
delete from users where id = 5
delete from users where id = 10 and name = Alice
```

**注意**：DELETE 目前仅支持包含主键（id）的 WHERE 条件。

### USE

```sql
use <table_name>

-- 示例
use users
use products
```

## 🔧 库模式使用

### C 语言调用

```c
#include "libmydb.h"

// 打开数据库
MYDB_Handle db = mydb_open("test.db");

// 执行 SQL 并获取 JSON 结果
char* result = NULL;
int rc = mydb_execute_json(db, 
    "select * from users where id = 1", 
    &result);

if (rc == 0 && result) {
    printf("Result: %s\n", result);
    free(result);
}

// 关闭数据库
mydb_close(db);
```

### WebAssembly 模式

```c
#include "libmydb.h"

// 使用 Emscripten 的持久化文件系统
MYDB_Handle db = mydb_open_with_ems("mydb.db");

char* json_result = NULL;
mydb_execute_json_with_ems(db, 
    "create table users (id int, name string)", 
    &json_result);

// 结果格式：{"ok":true,"message":"Executed."}
if (json_result) {
    // 处理 JSON 结果
    free(json_result);
}

mydb_close_with_ems(db);
```

### JSON 响应格式

**成功的查询**：
```json
{
  "ok": true,
  "rows": [
    {"id": 1, "name": "Alice", "email": "alice@example.com"},
    {"id": 2, "name": "Bob", "email": "bob@example.com"}
  ]
}
```

**成功的修改**：
```json
{
  "ok": true
}
```

**错误**：
```json
{
  "ok": false,
  "error": "duplicate_key"
}
```

## 🏗️ 技术实现

### 存储结构

```
DB File Layout:
┌─────────────────┬─────────────────┬─────────────────┬─────────────────┐
│   Page 0        │   Page 1        │   Page 2        │   Page N        │
│  (Catalog)      │  (Schema Blob)  │  (B-Tree Node)  │  (B-Tree Node)  │
└─────────────────┴─────────────────┴─────────────────┴─────────────────┘

Page 0 - Catalog Header:
- magic: 0x44544231 ("DTB1")
- version: 2
- num_tables: 当前表数量
- schemas_start_page: schema blob 起始页
- schemas_byte_len: schema 数据长度
- Table entries: 每张表的元数据（名称、root page、schema index）
```

### B-Tree 节点结构

**公共头部**（7 字节）：
- node_type: 1 字节（0=内部节点, 1=叶子节点）
- is_root: 1 字节
- parent_pointer: 4 字节

**叶子节点**：
- num_cells: 4 字节
- next_leaf: 4 字节（链表指针）
- cells: [key(4字节) + value(动态大小)] × N

**内部节点**：
- num_keys: 4 字节
- right_child: 4 字节
- cells: [child_pointer(4字节) + key(4字节)] × N

### 内存管理

- 页面缓存：最多 400 页在内存中
- 按需加载：首次访问页面时从磁盘读取
- 延迟写入：关闭数据库或显式 flush 时写入磁盘
- Schema 全局缓存：g_table_schemas 数组存储所有表的 schema

## ⚠️ 当前限制

1. **事务支持**：无 ACID 保证，无 WAL（Write-Ahead Logging）
2. **并发控制**：不支持多线程/多进程并发访问
3. **页面回收**：删除数据后页面不会被重用
4. **B-Tree 平衡**：未实现节点合并和重分配（只有分裂）
5. **DELETE 限制**：只支持通过主键删除
6. **JOIN 操作**：不支持多表关联查询
7. **聚合函数**：不支持 COUNT、SUM、AVG 等
8. **索引**：仅有主键索引，无二级索引
9. **约束**：不支持 UNIQUE、FOREIGN KEY、CHECK 等
10. **数据类型**：仅支持 int、string、timestamp 三种

## 🔨 编译说明

### 标准编译

```bash
cd src
make          # 编译 db 可执行文件
make clean    # 清理编译产物
```

### 编译为库

```bash
# 生成 libmydb.a 静态库
gcc -c db.c sql_lexer.c sql_parser.c -DBUILDING_MYDB_LIB
ar rcs libmydb.a db.o sql_lexer.o sql_parser.o
```

### 编译为 WebAssembly

```bash
emcc db.c sql_lexer.c sql_parser.c \
    -o mydb.js \
    -s EXPORTED_FUNCTIONS='["_mydb_open_with_ems","_mydb_close_with_ems","_mydb_execute_json_with_ems","_malloc","_free"]' \
    -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString"]' \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s MODULARIZE=1 \
    -s EXPORT_NAME="createMyDBModule" \
    -DBUILDING_MYDB_LIB
```

## 📚 项目结构

```
src/
├── db.c              # 主实现文件（3175 行）
├── sql_lexer.c       # SQL 词法分析器
├── sql_lexer.h
├── sql_parser.c      # SQL 语法分析器
├── sql_parser.h
├── sql_ast.h         # AST 数据结构定义
├── libmydb.h         # 库接口头文件
├── Makefile          # 编译配置
└── frontEnd/         # 前端示例
    ├── demo.html
    ├── pythonUseDemo.py
    └── wasmDemo/     # WebAssembly 示例
```

## 🎓 设计思想

本项目遵循"教学优先"和"实用性"的平衡：

1. **简洁性**：所有核心代码集中在单个 C 文件中（3000+ 行），易于理解
2. **模块化**：清晰分离 Pager、B-Tree、SQL Parser、Executor 等模块
3. **可扩展**：通过 AST 结构易于添加新的 SQL 功能
4. **跨平台**：支持 Linux、macOS、WebAssembly
5. **零依赖**：仅依赖标准 C 库

## 🤝 贡献

欢迎提交 Issue 和 Pull Request！

## 📄 许可证

见 LICENSE 文件。

---
---
---

# MyDB - A Simple Database Built from Scratch

A lightweight relational database engine built from the ground up, featuring B-Tree storage, SQL parsing, and multi-table support. Can run as a standalone program or be embedded as a library (including WebAssembly compilation).

## 📋 Table of Contents

- [Core Features](#core-features)
- [Quick Start](#quick-start)
- [Usage Guide](#usage-guide)
- [SQL Syntax Support](#sql-syntax-support)
- [Library Mode](#library-mode)
- [Technical Implementation](#technical-implementation)
- [Current Limitations](#current-limitations)
- [Build Instructions](#build-instructions)

## 🎯 Core Features

### 1. Multi-Table Catalog
- Persistent catalog stored in page 0 with all table metadata
- Single DB file supports up to 32 tables
- Switch active table with `use <table>` command
- Schema information embedded in DB file, no external configuration needed

### 2. Flexible Schema Definition
- Up to **100 columns** per table
- Three data types supported:
  - `int`: 4-byte integer
  - `string`: fixed-length string (max 255 bytes)
  - `timestamp`: 8-byte Unix timestamp (auto-populated with current time)
- Row size dynamically calculated at runtime based on schema

### 3. B-Tree Storage Engine
- Complete B-Tree implementation (internal nodes + leaf nodes)
- Automatic node splitting (leaf split and internal split)
- 4KB page size, supports up to 400 pages
- Page caching mechanism with on-demand loading and flushing
- Primary key index (first column must be int type)

### 4. SQL Parser
- Supports `CREATE TABLE`, `INSERT`, `SELECT`, `DELETE` statements
- WHERE clause generates AST (Abstract Syntax Tree)
- Complex expression support:
  - Comparison operators: `=`, `!=`, `<`, `<=`, `>`, `>=`
  - Logical operators: `AND`, `OR`, `NOT`
  - Special operators: `BETWEEN`, `IN`, `IS NULL`, `IS NOT NULL`

### 5. Powerful Query Features
- **Projection**: Support `SELECT *` or specific column names
- **Filtering**: WHERE clause with multi-condition combinations
- **Sorting**: `ORDER BY column [ASC|DESC]`
- **Pagination**: `LIMIT` and `OFFSET` support
- **Point Query Optimization**: Primary key equality queries use fast path (O(log n) vs O(n))

### 6. Delete Operations
- Delete records by primary key
- Automatically update parent node keys
- Support complex WHERE conditions (AST-based evaluation)

### 7. Multiple Execution Modes
- **Interactive REPL**: Command-line interface
- **C Library**: Callable via `libmydb.h` header
- **WebAssembly**: Compile to WASM for browser execution
- **JSON API**: Provides JSON-formatted result output

## 🚀 Quick Start

### Build

```bash
cd src
make
```

### Run

```bash
./db mytest.db
```

## 📖 Usage Guide

### Basic Operation Workflow

```sql
-- 1. Create table
db > create table users (id int, name string, email string, created_at timestamp)
Table 'users' created with 4 columns.

-- 2. Activate table (switch to this table)
db > use users
Using table 'users'.

-- 3. Insert data
db > insert into users 1 Alice alice@example.com 1704067200
Executed.

-- timestamp column can be omitted, will use current time
db > insert into users 2 Bob bob@example.com
Executed.

-- 4. Query data
db > select * from users
(1, Alice, alice@example.com, 1704067200)
(2, Bob, bob@example.com, 1734153600)
Executed.

-- 5. Conditional query
db > select name, email from users where id = 1
(Alice, alice@example.com)
Executed.

-- 6. Sorting and pagination
db > select * from users order by id desc limit 1
(2, Bob, bob@example.com, 1734153600)
Executed.

-- 7. Delete data
db > delete from users where id = 2
Executed.

-- 8. Exit
db > .exit
```

### Meta Commands

```bash
.exit         # Exit program (saves all data)
.btree        # View B-Tree structure of current table
.constants    # Display internal constants (page size, cell size, etc.)
```

## 📝 SQL Syntax Support

### CREATE TABLE

```sql
create table <table_name> (
    <col1> <type>,
    <col2> <type>,
    ...
)

-- Examples
create table products (id int, name string, price int, stock int)
create table logs (id int, message string, timestamp timestamp)
```

**Notes**:
- First column must be `int` type (primary key)
- `string` type defaults to 255 bytes
- `timestamp` type is 8-byte integer

### INSERT

```sql
-- Method 1: Insert into current active table
insert <value1> <value2> ...

-- Method 2: Specify table name
insert into <table_name> <value1> <value2> ...

-- Examples
insert 1 Alice alice@example.com
insert into users 2 Bob bob@example.com
insert into logs 1 "Server started"  -- timestamp auto-filled
```

### SELECT

```sql
select <columns> from <table_name>
    [where <condition>]
    [order by <column> [asc|desc]]
    [limit <n>]
    [offset <n>]

-- Examples
select * from users
select id, name from users where id > 10
select * from users where name = Alice and id < 100
select * from users order by id desc limit 10 offset 20
select * from users where id between 1 and 100
select name from users where email is not null
```

**WHERE Condition Support**:
- Comparison: `=`, `!=`, `<`, `<=`, `>`, `>=`
- Logical: `AND`, `OR`, `NOT`
- Range: `BETWEEN x AND y`
- Set: `IN (value1, value2, ...)`
- Null: `IS NULL`, `IS NOT NULL`

### DELETE

```sql
delete from <table_name> where <condition>

-- Examples
delete from users where id = 5
delete from users where id = 10 and name = Alice
```

**Note**: DELETE currently only supports WHERE conditions that include the primary key (id).

### USE

```sql
use <table_name>

-- Examples
use users
use products
```

## 🔧 Library Mode

### C Language Usage

```c
#include "libmydb.h"

// Open database
MYDB_Handle db = mydb_open("test.db");

// Execute SQL and get JSON result
char* result = NULL;
int rc = mydb_execute_json(db, 
    "select * from users where id = 1", 
    &result);

if (rc == 0 && result) {
    printf("Result: %s\n", result);
    free(result);
}

// Close database
mydb_close(db);
```

### WebAssembly Mode

```c
#include "libmydb.h"

// Use Emscripten persistent filesystem
MYDB_Handle db = mydb_open_with_ems("mydb.db");

char* json_result = NULL;
mydb_execute_json_with_ems(db, 
    "create table users (id int, name string)", 
    &json_result);

// Result format: {"ok":true,"message":"Executed."}
if (json_result) {
    // Process JSON result
    free(json_result);
}

mydb_close_with_ems(db);
```

### JSON Response Format

**Successful Query**:
```json
{
  "ok": true,
  "rows": [
    {"id": 1, "name": "Alice", "email": "alice@example.com"},
    {"id": 2, "name": "Bob", "email": "bob@example.com"}
  ]
}
```

**Successful Modification**:
```json
{
  "ok": true
}
```

**Error**:
```json
{
  "ok": false,
  "error": "duplicate_key"
}
```

## 🏗️ Technical Implementation

### Storage Structure

```
DB File Layout:
┌─────────────────┬─────────────────┬─────────────────┬─────────────────┐
│   Page 0        │   Page 1        │   Page 2        │   Page N        │
│  (Catalog)      │  (Schema Blob)  │  (B-Tree Node)  │  (B-Tree Node)  │
└─────────────────┴─────────────────┴─────────────────┴─────────────────┘

Page 0 - Catalog Header:
- magic: 0x44544231 ("DTB1")
- version: 2
- num_tables: current table count
- schemas_start_page: schema blob start page
- schemas_byte_len: schema data length
- Table entries: metadata for each table (name, root page, schema index)
```

### B-Tree Node Structure

**Common Header** (7 bytes):
- node_type: 1 byte (0=internal, 1=leaf)
- is_root: 1 byte
- parent_pointer: 4 bytes

**Leaf Node**:
- num_cells: 4 bytes
- next_leaf: 4 bytes (linked list pointer)
- cells: [key(4 bytes) + value(dynamic size)] × N

**Internal Node**:
- num_keys: 4 bytes
- right_child: 4 bytes
- cells: [child_pointer(4 bytes) + key(4 bytes)] × N

### Memory Management

- Page cache: up to 400 pages in memory
- On-demand loading: read from disk on first page access
- Lazy writing: write to disk on database close or explicit flush
- Schema global cache: g_table_schemas array stores all table schemas

## ⚠️ Current Limitations

1. **Transaction Support**: No ACID guarantees, no WAL (Write-Ahead Logging)
2. **Concurrency Control**: No multi-thread/multi-process concurrent access support
3. **Page Reclamation**: Pages are not reused after deletion
4. **B-Tree Balancing**: No node merge and redistribution (only split implemented)
5. **DELETE Limitation**: Only supports deletion by primary key
6. **JOIN Operations**: No multi-table join queries
7. **Aggregate Functions**: No COUNT, SUM, AVG, etc.
8. **Indexes**: Only primary key index, no secondary indexes
9. **Constraints**: No UNIQUE, FOREIGN KEY, CHECK, etc.
10. **Data Types**: Only int, string, timestamp supported

## 🔨 Build Instructions

### Standard Build

```bash
cd src
make          # Build db executable
make clean    # Clean build artifacts
```

### Build as Library

```bash
# Generate libmydb.a static library
gcc -c db.c sql_lexer.c sql_parser.c -DBUILDING_MYDB_LIB
ar rcs libmydb.a db.o sql_lexer.o sql_parser.o
```

### Build as WebAssembly

```bash
emcc db.c sql_lexer.c sql_parser.c \
    -o mydb.js \
    -s EXPORTED_FUNCTIONS='["_mydb_open_with_ems","_mydb_close_with_ems","_mydb_execute_json_with_ems","_malloc","_free"]' \
    -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString"]' \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s MODULARIZE=1 \
    -s EXPORT_NAME="createMyDBModule" \
    -DBUILDING_MYDB_LIB
```

## 📚 Project Structure

```
src/
├── db.c              # Main implementation (3175 lines)
├── sql_lexer.c       # SQL lexer
├── sql_lexer.h
├── sql_parser.c      # SQL parser
├── sql_parser.h
├── sql_ast.h         # AST data structure definitions
├── libmydb.h         # Library interface header
├── Makefile          # Build configuration
└── frontEnd/         # Frontend examples
    ├── demo.html
    ├── pythonUseDemo.py
    └── wasmDemo/     # WebAssembly examples
```

## 🎓 Design Philosophy

This project balances "educational value" with "practicality":

1. **Simplicity**: All core code concentrated in a single C file (3000+ lines), easy to understand
2. **Modularity**: Clear separation of Pager, B-Tree, SQL Parser, Executor modules
3. **Extensibility**: AST structure makes it easy to add new SQL features
4. **Cross-platform**: Supports Linux, macOS, WebAssembly
5. **Zero Dependencies**: Only depends on standard C library

## 🤝 Contributing

Issues and Pull Requests are welcome!

## 📄 License

See LICENSE file.