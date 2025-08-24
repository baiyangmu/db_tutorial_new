# 简介：构建一个简单的数据库

本文档是对实现目录（implement/）的简要说明，先给出中文可读版本，随后保留原始英文说明以便参考。

主要特性（中文摘要）

- **多表目录（Catalog）**：持久化的 catalog 位于 page 0，保存表的元数据（schema 与 root page）。一个 DB 文件中可以存在多张表，可以使用 `use <table>` 切换当前表。

- **动态且更大的表结构**：支持最多 100 列，列类型包含 `int`、`string`（定长）和 `timestamp`（8 字节）。行大小在运行时根据 schema 计算。

- **B‑Tree 存储**：实现了内部/叶子节点的 B‑Tree（含插入与分裂逻辑），页被缓存到内存并由 pager 刷写到磁盘。

- **动态行序列化**：`serialize_row_dynamic` 根据当前 schema 将列值序列化；当省略 `timestamp` 列值时默认使用当前 epoch 时间。

- **SQL 解析与 WHERE AST**：基础的词法/解析器会生成 WHERE 子句的 AST（支持比较、`AND/OR/NOT`、`IN`、`BETWEEN`、`IS NULL` 等），查询时用该 AST 进行求值。

- **查询功能**：`SELECT` 支持多列投影、`ORDER BY <col> [ASC|DESC]`、`LIMIT`/`OFFSET`，并在 WHERE 恰好匹配主键相等时走到点查路径以加速查询。

- **删除支持**：实现了按整数主键删除（删除 leaf cell 并在需要时更新 parent key）。注：尚未实现完整的 B‑Tree 重新平衡（合并/重分配）和页回收。

实现取舍

- 实现聚焦于工程需要（记录、查询、删除），而非完整的 SQL 标准；一些特性如 `AUTO_INCREMENT`、`DEFAULT CURRENT_TIMESTAMP`、参数化查询等放在应用层处理或留待后续增强。

当前限制

- `INSERT` 与 DDL 的解析简化处理。
- `DELETE` 仅支持按第一列（整数主键）删除。
- 无事务 / WAL、并发控制或页回收机制。

---

# Let's Build a Simple Database

[View rendered tutorial](https://cstack.github.io/db_tutorial/) (with more details on what this is.)

## Notes to myself

Run site locally:
```
bundle exec jekyll serve
```

### implement/ — local DB engine (summary of enhancements)

The `implement/` directory contains an extended local database engine derived from the original tutorial `db.c`. It adds practical features useful for the project's backend while remaining compact and easy to reason about. Key highlights:

- **Multi-table catalog**: A persistent catalog page (page 0) stores table metadata (schema and root page). Multiple tables can coexist in one DB file and be selected with `use <table>`.

- **Dynamic, larger schemas**: Support for up to **100 columns** per table. Column types include `int`, `string` (varchar-like fixed length), and `timestamp` (8-byte epoch seconds). Row size is computed at runtime from the schema.

- **B‑Tree storage**: Persistent B‑Tree implementation for internal/leaf nodes with insertion and split logic. Pages are cached in memory and flushed to disk by the pager.

- **Dynamic row serialization**: `serialize_row_dynamic` writes row values according to the active schema; `timestamp` columns default to the current epoch time when omitted.

- **SQL parsing + AST for WHERE**: A basic lexer/parser produces a small AST for WHERE expressions (supports comparisons, `AND/OR/NOT`, `IN`, `BETWEEN`, `IS NULL`). The AST drives evaluation during queries.

- **Query features**: `SELECT` supports multi-column projection, `ORDER BY <col> [ASC|DESC]`, `LIMIT` / `OFFSET`, and an optimized point-lookup path when the WHERE clause matches the primary key equality.

- **Delete support**: `DELETE` by integer primary key is implemented (removes the leaf cell and updates parent keys when needed). Note: full B‑Tree rebalancing (merge/redistribute) and page free-list are not implemented yet.

- **Practical trade-offs**: The implementation focuses on the project needs (record, query, delete) rather than full SQL compatibility. Features like `AUTO_INCREMENT`, `DEFAULT CURRENT_TIMESTAMP`, and parameterized queries are intentionally handled by the application layer (Python) or left for later engine enhancement.

**Current limitations**

- `INSERT` and DDL parsing are simplified.
- `DELETE` supports only deletion by integer primary key (first column).
- No transaction/WAL, no concurrency control, and no page reclamation yet.