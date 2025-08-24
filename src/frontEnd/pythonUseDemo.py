#!/usr/bin/env python3
import ctypes
from ctypes import c_void_p, c_char_p, POINTER, byref
import json
import sys
import os

# load library (adjust path if needed)
# Prefer environment override, otherwise expect the shared lib in ../bin relative to this script
env_path = os.environ.get("LIBMYDB_PATH")
if env_path:
    LIB_PATH = os.path.abspath(env_path)
else:
    LIB_PATH = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "bin", "libmydb.so"))

if not os.path.isfile(LIB_PATH):
    sys.stderr.write(f"Library not found at {LIB_PATH}\n")
    sys.stderr.write("Please build the library (make bin/libmydb.so from src/) or set LIBMYDB_PATH env to its path.\n")
    sys.exit(1)

lib = ctypes.CDLL(LIB_PATH)

# declare function signatures
lib.mydb_open.restype = c_void_p
lib.mydb_open.argtypes = [c_char_p]

lib.mydb_close.restype = None
lib.mydb_close.argtypes = [c_void_p]

lib.mydb_execute_json.restype = ctypes.c_int
lib.mydb_execute_json.argtypes = [c_void_p, c_char_p, POINTER(c_char_p)]

# load libc for free (try common names)
_libc = None
for name in ("libc.so.6", "libc.dylib", None):
    try:
        if name:
            _libc = ctypes.CDLL(name)
        else:
            _libc = ctypes.CDLL(None)
        break
    except Exception:
        _libc = None
if _libc is None:
    raise RuntimeError("Unable to load C runtime to free memory")

_libc.free.argtypes = [c_void_p]
_libc.free.restype = None

def free_c_string(c_ptr):
    """Free a char* returned by the C library (c_ptr is a ctypes.c_char_p)."""
    if not c_ptr:
        return
    addr = ctypes.cast(c_ptr, c_void_p).value
    if addr:
        _libc.free(c_void_p(addr))

def execute_sql(handle, sql):
    out = c_char_p()
    rc = lib.mydb_execute_json(handle, sql.encode("utf-8"), byref(out))
    result_text = None
    if ctypes.cast(out, c_void_p).value:
        # out.value gives the bytes read from the pointer
        try:
            result_text = out.value.decode("utf-8")
        except Exception:
            result_text = out.value.decode("latin-1", "replace")
        # NOTE: temporarily skip freeing here to help debug a crash that may be
        # triggered by freeing the C-allocated buffer. Uncomment the free when
        # debugging is complete.
        # free_c_string(out)
    return rc, result_text

def pretty_print_response(sql, rc, text):
    print("SQL:", sql)
    print("rc:", rc)
    if text is None:
        print("response: <null>")
        return
    try:
        j = json.loads(text)
        print("response (json):", json.dumps(j, ensure_ascii=False, indent=2))
    except Exception:
        print("response (raw):", text)
    print()

def main():
    # test sequence
    cmds = [
        "create table images (id int, file_path string, mime_type string, created_at timestamp)",
        "use images",
        "insert into images 1 /tmp/a.jpg image/jpeg 1690000000",
        "insert into images 2 /tmp/b.png image/png 1690000100",
        "select id, created_at, file_path from images order by id desc limit 10 offset 0",
        "delete from images where id = 1",
        "select * from images",
    ]

    h = lib.mydb_open(b"test2.db")
    if not h:
        print("Failed to open DB handle", file=sys.stderr)
        sys.exit(1)

    try:
        for sql in cmds:
            rc, text = execute_sql(h, sql)
            pretty_print_response(sql, rc, text)
    finally:
        lib.mydb_close(h)

if __name__ == "__main__":
    main()