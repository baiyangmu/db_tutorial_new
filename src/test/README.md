# MyDB Unit Tests

## 测试文件结构

```
test/
├── test_main.c       # 测试套件主程序
├── test_schema.c     # Schema 模块测试
├── test_util.c       # 工具函数测试
└── README.md         # 本文件
```

## 运行测试

### 编译所有测试

```bash
cd src
make test
```

### 运行测试

```bash
make run-test
```

或者单独运行：

```bash
./test/test_schema
./test/test_util
```

## 测试覆盖

### Schema Tests (test_schema.c)
- ✓ parse_column_type() - 列类型解析
- ✓ schema_col_index() - 列索引查找
- ✓ compute_row_size() - 行大小计算
- ✓ schema_col_offset() - 列偏移量计算

### Util Tests (test_util.c)
- ✓ parse_int() - 整数解析
- ✓ parse_int64() - 64位整数解析
- ✓ String Buffer 操作
- ✓ JSON 转义

## 添加新测试

1. 在 `test/` 目录创建新的测试文件 `test_xxx.c`
2. 包含需要测试的头文件
3. 编写测试函数
4. 在 `main()` 函数中调用测试
5. 更新 Makefile 中的测试目标

## 测试约定

- 测试函数命名：`test_功能名称()`
- 使用 `assert()` 进行断言
- 每个测试打印开始和结束消息
- 测试应该独立，不依赖执行顺序

