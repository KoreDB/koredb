# Python API

## Install from PyPI

```bash
pip install koredb
```

The package supports standard CPython 3.12 and later. Release wheels use the
CPython stable ABI. Free-threaded CPython is not currently supported.

## Install from GitHub

Install a revision containing the root Python packaging files directly:

```bash
python -m pip install "git+https://github.com/KoreDB/koredb.git"
```

To select a branch, tag, or commit, append `@<revision>` to the URL.

The source installation builds a native extension. It requires Python 3.12 or newer,
Git, GNU Make, CMake 3.15 or newer, Python development headers, and a C++20
compiler. On Windows, run from an MSVC developer environment with Ninja and GNU
Make on `PATH`. Set `NUM_THREADS` to limit parallel compilation. See the
[developer guide](https://koredb.github.io/docs/developer-guide/) for platform setup.

Linux wheels use glibc (`manylinux`); other environments may require a source
build.

## Quick start

```python
import koredb

db = koredb.Database()  # In-memory database
connection = koredb.Connection(db)
result = connection.execute("RETURN 1 AS number")
print(result.get_next())  # [1]
```

The API also supports persistent databases, parameterized queries, data frames,
Arrow, and graph integrations. See the [documentation](https://koredb.github.io/docs/).
