### 说明
- 手动交互验证用例；逐条在程序中输入命令即可，不依赖 Ruby/脚本。
- 目前支持 `insert into <table> <values...>` 语法；若实现了 `insert <values...>`，可自行替换。
- `string` 为定长字符串（默认 255）。

### 编译与启动
```bash
cd /Users/user/Projects/db_learn/db_tutorial-master
cc implement/db.c -o implement/mydb_impl
```

### 场景一：基础功能（建表/切表/插入/查询/常量/树）
```bash
./implement/mydb_impl implement/cli_test.db
```
在交互中逐条输入并观察关键输出：
```
create table users (id int, username string, email string)
# 期望：Table 'users' created with 3 columns. + Executed.

use users
# 期望：Using table 'users'. + Executed.

insert into users 1 alice alice@example.com
# 期望：Executed.

select
# 期望：(1, alice, alice@example.com) + Executed.

.constants
# 期望：ROW_SIZE(table)、LEAF_NODE_CELL_SIZE(table)、LEAF_NODE_MAX_CELLS(table)

.btree
# 期望：Tree:
# - leaf (size 1)
#   - 1

.exit
```

### 场景二：持久化（重启仍可见）
```bash
./implement/mydb_impl implement/cli_test.db
```
```
use users
select
# 期望：(1, alice, alice@example.com) + Executed.
.exit
```

### 场景三：多表创建与隔离
```bash
./implement/mydb_impl implement/cli_test.db
```
```
create table orders (id int, username string, email string)
use orders
select              # 期望：空表
insert into orders 2 bob bob@example.com
select              # 期望：(2, bob, bob@example.com)

use users
select              # 期望：仍只有 (1, alice, alice@example.com)
.exit
```

### 场景四：动态 schema 示例（不同列顺序/类型）
使用全新库验证不同 schema（你也可复用现有库）：
```bash
./implement/mydb_impl implement/multi_schema.db
```
表一（int, int, string）：
```
create table accounts (id int, age int, note string)
use accounts
.constants           # 期望：ROW_SIZE 与 users 不同
insert into accounts 1 33 vip
select               # 期望：(1, 33, vip)
```
表二（int, string, int）：
```
create table books (id int, title string, rating int)
use books
.constants           # 期望：ROW_SIZE=4+255+4=263（如未改默认 string 长度）
insert into books 1 moby 5
select               # 期望：(1, moby, 5)

use accounts
select               # 期望：(1, 33, vip)
.exit
```

### 场景五：错误与边界
无活跃表：
```bash
./implement/mydb_impl implement/empty_test.db
```
```
select
# 期望：提示 No active table ... + Executed.
.exit
```
不存在表：
```bash
./implement/mydb_impl implement/cli_test.db
```
```
use not_exists
# 期望：Table not found: not_exists
.exit
```
重复键（第一列为主键）：
```bash
./implement/mydb_impl implement/cli_test.db
```
```
use users
insert into users 1 dupe dup@example.com
# 期望：Error: Duplicate key.
.exit
```

### 场景六：B-Tree 分裂（可选）
插入多条记录触发叶分裂，再查看树：
```bash
./implement/mydb_impl implement/split_test.db
```
```
create table t1 (id int, username string, email string)
use t1
insert into t1 1 a a@e
insert into t1 2 b b@e
insert into t1 3 c c@e
insert into t1 4 d d@e
insert into t1 5 e e@e
insert into t1 6 f f@e
insert into t1 7 g g@e
insert into t1 8 h h@e
.btree
# 期望：出现 internal/leaf 多层结构（非单一 leaf）
.exit
```

### 快速检查清单
- 建表/切表/插入/查询 正常
- 重启后数据可见（持久化）
- 多表隔离：不同表数据互不影响
- 动态 schema：列顺序/类型变化后 `.constants` 与行打印符合预期
- 错误分支：无活跃表、表不存在、重复键有明确提示
- `.btree` 能展示树形结构，批量插入后能看到分裂

