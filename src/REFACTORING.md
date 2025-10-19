# MyDB 代码重构文档

## 概述

本次重构将原来的单体文件 `db.c` (3400+ 行) 重构为模块化的架构，提高了代码的可维护性、可测试性和可扩展性。

## 新的目录结构

```
src/
├── include/              # 头文件目录
│   ├── mydb.h           # 公共 API
│   ├── btree.h          # B树操作
│   ├── catalog.h        # 目录管理
│   ├── pager.h          # 页面管理
│   ├── schema.h         # 表结构定义
│   ├── sql_executor.h   # SQL执行器
│   ├── repl.h           # 交互式命令行
│   └── util.h           # 工具函数
│
├── impl/                 # 实现文件目录
│   ├── mydb.c           # 公共 API 实现
│   ├── btree.c          # B树操作实现
│   ├── catalog.c        # 目录管理实现
│   ├── pager.c          # 页面管理实现
│   ├── schema.c         # 表结构实现
│   ├── sql_executor.c   # SQL执行器实现
│   ├── repl.c           # 命令行实现
│   └── util.c           # 工具函数实现
│
├── test/                 # 单元测试目录
│   ├── test_schema.c    # Schema 测试
│   ├── test_util.c      # 工具函数测试
│   ├── test_main.c      # 测试主程序
│   └── README.md        # 测试说明
│
├── bin/                  # 编译输出目录 (自动创建)
│
├── main.c               # 主程序入口
├── Makefile.new         # 新的 Makefile
└── REFACTORING.md       # 本文档
```

## 模块说明

### 1. util (工具模块)
**文件**: `include/util.h`, `impl/util.c`

**职责**:
- 通用工具函数（解析、字符串处理）
- 行数据访问辅助函数
- 排序支持
- JSON 字符串构建

**主要函数**:
- `parse_int()` / `parse_int64()` - 字符串到整数转换
- `row_get_int()` / `row_get_string()` - 从行中提取数据
- `print_row_dynamic()` - 打印行数据
- `sb_*()` - 字符串缓冲区操作

### 2. schema (表结构模块)
**文件**: `include/schema.h`, `impl/schema.c`

**职责**:
- 表结构定义和管理
- 列类型定义
- 行大小计算
- Schema 序列化/反序列化

**主要类型**:
- `ColumType` - 列类型枚举
- `ColumnDef` - 列定义结构
- `TableSchema` - 表结构
- `Row` - 传统行结构（兼容）

**主要函数**:
- `parse_column_type()` - 解析列类型
- `schema_col_index()` - 查找列索引
- `compute_row_size()` - 计算行大小
- `parse_schemas_from_str()` - 解析 schema 字符串

### 3. pager (页面管理模块)
**文件**: `include/pager.h`, `impl/pager.c`

**职责**:
- 页面缓存管理
- 磁盘 I/O 操作
- 页面分配

**主要类型**:
- `Pager` - 页面管理器结构

**主要函数**:
- `pager_open()` - 打开数据库文件
- `get_page()` - 获取页面（带缓存）
- `pager_flush()` - 刷新页面到磁盘
- `get_unused_page_num()` - 获取未使用页号

### 4. catalog (目录管理模块)
**文件**: `include/catalog.h`, `impl/catalog.c`

**职责**:
- 数据库元数据管理
- 表目录维护
- Schema 持久化

**主要类型**:
- `CatalogHeader` - 目录头
- `CatalogEntry` - 表条目

**主要函数**:
- `catalog_init()` - 初始化目录
- `catalog_find()` - 查找表
- `catalog_add_table()` - 添加表
- `handle_create_table_ex()` - 处理 CREATE TABLE
- `load_schemas_for_db()` / `save_schemas_for_db()` - Schema 持久化

### 5. btree (B树模块)
**文件**: `include/btree.h`, `impl/btree.c`

**职责**:
- B+树索引实现
- 节点操作
- 游标管理
- 插入/删除/查找操作
- 节点分裂和合并

**主要类型**:
- `Table` - 表结构
- `Cursor` - 游标
- `NodeType` - 节点类型

**主要函数**:
- `db_open()` / `db_close()` - 数据库打开/关闭
- `table_find()` - 查找键
- `table_start()` - 获取起始游标
- `leaf_node_insert()` - 叶节点插入
- `handle_underflow()` - 处理节点下溢

### 6. sql_executor (SQL执行器模块)
**文件**: `include/sql_executor.h`, `impl/sql_executor.c`

**职责**:
- SQL 语句准备
- SQL 语句执行
- WHERE 子句评估
- 结果集处理

**主要类型**:
- `Statement` - SQL 语句结构
- `StatementType` - 语句类型
- `PrepareResult` / `ExecuteResult` - 结果枚举

**主要函数**:
- `prepare_statement()` - 准备语句
- `execute_insert()` - 执行 INSERT
- `execute_select()` - 执行 SELECT
- `execute_delete()` - 执行 DELETE
- `eval_expr_to_bool()` - 表达式求值

### 7. repl (交互式命令行模块)
**文件**: `include/repl.h`, `impl/repl.c`

**职责**:
- 用户交互
- 命令读取
- 元命令处理

**主要类型**:
- `InputBuffer` - 输入缓冲区
- `MetaCommandResult` - 元命令结果

**主要函数**:
- `new_input_buffer()` - 创建输入缓冲
- `read_input()` - 读取输入
- `do_meta_command()` - 处理元命令（.exit, .btree等）

### 8. mydb (公共API模块)
**文件**: `include/mydb.h`, `impl/mydb.c`

**职责**:
- 对外公共接口
- JSON 格式输出
- Emscripten 支持

**主要函数**:
- `mydb_open()` / `mydb_close()` - 打开/关闭数据库
- `mydb_execute_json()` - 执行SQL并返回JSON
- `mydb_open_with_ems()` - Emscripten 版本

## 编译和使用

### 使用新的 Makefile

```bash
cd src

# 查看帮助
make -f Makefile.new help

# 编译主程序
make -f Makefile.new

# 编译并运行测试
make -f Makefile.new test
make -f Makefile.new run-test

# 清理
make -f Makefile.new clean
```

### 替换旧 Makefile

如果确认新结构正常工作：

```bash
cd src
mv Makefile Makefile.old
mv Makefile.new Makefile
```

## 迁移指南

### 从旧代码迁移

1. **引用头文件**: 将 `#include "db.c"` 改为引用对应的模块头文件
2. **函数调用**: 大部分函数签名保持不变
3. **全局变量**: 现在通过对应模块的头文件访问

### 示例

**旧代码**:
```c
// 直接在 db.c 中使用
Table* table = db_open("test.db");
```

**新代码**:
```c
#include "include/btree.h"
Table* table = db_open("test.db");
```

## 测试

### 运行测试

```bash
make -f Makefile.new run-test
```

### 添加新测试

1. 在 `test/` 目录创建 `test_xxx.c`
2. 包含需要的头文件
3. 编写测试函数
4. 测试会自动被 Makefile 发现和编译

## 优势

1. **模块化**: 每个模块职责清晰，独立开发和测试
2. **可维护性**: 代码分散到多个小文件，更容易理解和修改
3. **可测试性**: 每个模块可以独立进行单元测试
4. **可扩展性**: 添加新功能时只需修改相关模块
5. **编译速度**: 修改单个模块只需重新编译该模块

## 兼容性

- ✅ 保持所有原有功能不变
- ✅ 函数签名基本保持不变
- ✅ 支持原有的 SQL 语法
- ✅ 数据库文件格式兼容
- ✅ 支持 Emscripten 编译

## 后续改进建议

1. 添加更多单元测试覆盖
2. 实现集成测试
3. 添加性能测试
4. 改进错误处理
5. 添加日志模块
6. 实现连接池（多表操作）

