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

- `INSERT` and DDL parsing are simplified. The backend (Flask) can generate simplified SQL that this engine expects (recommended for minimal integration effort).
- `DELETE` supports only deletion by integer primary key (first column).
- No transaction/WAL, no concurrency control, and no page reclamation yet.

If you want, I can update this README section with examples showing the minimal SQL forms the engine expects from the Flask backend.
