# MyDB 重构版本使用指南

## 快速开始

### 1. 编译项目

```bash
cd src
make -f Makefile.new
```

编译成功后会生成 `db` 可执行文件。

### 2. 运行数据库

```bash
./db mytest.db
```

### 3. 基本操作

```sql
-- 创建表
db > create table users (id int, name string, email string)

-- 使用表
db > use users

-- 插入数据
db > insert into users 1 Alice alice@example.com
db > insert into users 2 Bob bob@example.com

-- 查询数据
db > select * from users
db > select name,email from users where id = 1

-- 删除数据
db > delete from users where id = 1

-- 查看B树结构
db > .btree

-- 退出
db > .exit
```

## 编译选项

### 编译主程序

```bash
make -f Makefile.new          # 编译
make -f Makefile.new clean    # 清理
```

### 编译测试

```bash
make -f Makefile.new test     # 编译测试
make -f Makefile.new run-test # 运行测试
```

### 调试版本

```bash
make -f Makefile.new debug    # 编译调试版本
```

## 项目结构

```
src/
├── include/          # 头文件（模块接口）
│   ├── mydb.h       # 公共 API
│   ├── btree.h      # B树操作
│   ├── catalog.h    # 目录管理
│   ├── pager.h      # 页面管理
│   ├── schema.h     # 表结构定义
│   ├── sql_executor.h  # SQL执行
│   ├── repl.h       # 命令行
│   └── util.h       # 工具函数
│
├── impl/            # 实现文件
│   ├── mydb.c
│   ├── btree.c
│   ├── catalog.c
│   ├── pager.c
│   ├── schema.c
│   ├── sql_executor.c
│   ├── repl.c
│   └── util.c
│
├── test/            # 单元测试
│   ├── test_schema.c
│   ├── test_util.c
│   └── test_main.c
│
└── main.c           # 主程序
```

## 运行测试

```bash
# 编译并运行所有测试
make -f Makefile.new run-test

# 或者单独运行某个测试
./bin/test_schema
./bin/test_util
```

## 功能特性

### 已实现功能

- ✅ CREATE TABLE - 创建表
- ✅ INSERT - 插入数据
- ✅ SELECT - 查询数据（支持 WHERE, ORDER BY, LIMIT, OFFSET）
- ✅ DELETE - 删除数据
- ✅ 多表支持（通过 USE 命令切换）
- ✅ B+树索引
- ✅ 动态 Schema
- ✅ 持久化存储
- ✅ 单元测试框架

### 支持的数据类型

- `int` - 32位整数
- `string` - 可变长字符串
- `timestamp` - 64位时间戳

### 元命令

- `.exit` - 退出程序
- `.btree` - 显示当前表的B树结构
- `.constants` - 显示常量信息

## 开发指南

### 添加新模块

1. 在 `include/` 创建头文件
2. 在 `impl/` 创建实现文件
3. 在需要的地方 `#include` 对应头文件
4. Makefile 会自动发现并编译

### 添加测试

1. 在 `test/` 创建 `test_xxx.c`
2. 编写测试函数
3. 运行 `make -f Makefile.new test`

### 调试技巧

```bash
# 使用 GDB 调试
gdb ./db
(gdb) run test.db

# 查看内存使用
valgrind --leak-check=full ./db test.db
```

## 性能考虑

- 页面缓存：最多缓存 400 个页面
- B+树结构：支持高效的范围查询
- 延迟写入：修改在内存中累积，关闭时统一写入

## 限制

- 最大表数：32
- 最大列数：100
- 页面大小：4096 字节
- 字符串最大长度：由创建表时指定

## 故障排除

### 编译错误

```bash
# 确保所有依赖已安装
make -f Makefile.new clean
make -f Makefile.new
```

### 运行时错误

- 检查数据库文件权限
- 确保磁盘空间充足
- 查看调试输出（stderr）

## 更多信息

- 查看 `REFACTORING.md` 了解重构细节
- 查看 `test/README.md` 了解测试说明
- 查看各个头文件中的注释了解API详情

