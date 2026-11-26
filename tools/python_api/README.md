# Python API

## Install from GitHub

Install a revision containing the root Python packaging files directly:

```bash
python -m pip install "git+https://github.com/KoreDB/koredb.git"
```

To select a branch, tag, or commit, append `@<revision>` to the URL. Existing
releases such as `0.11.3` predate this packaging support; use the release source
distribution instructions in [scripts/pip-package](../../scripts/pip-package/README.md)
for those versions. The GitHub command becomes available once the changes are
present in the selected revision.

The installation builds a native extension. It requires Python 3.9 or newer,
Git, GNU Make, CMake 3.15 or newer, Python development headers, and a C++20
compiler. On Windows, run from an MSVC developer environment with Ninja and GNU
Make on `PATH`. Set `NUM_THREADS` to limit parallel compilation. See the
[developer guide](https://koredb.github.io/docs/developer-guide/) for platform setup.

Release wheels target CPython 3.9–3.15 on Linux, macOS and Windows, for x86-64
and ARM64. Windows ARM64 wheels start at Python 3.11. Linux wheels use glibc
(`manylinux`); other environments may require a source build. Free-threaded
CPython is not currently included in the wheel matrix.

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
