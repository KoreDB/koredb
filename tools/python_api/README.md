# Python APIs

## Installation

Use the [KoreDB 0.11.3 Release](https://github.com/KoreDB/koredb/releases/tag/0.11.3)
for Python downloads. If a compatible wheel is attached, download it and install the
local file with `python -m pip install`, using its actual path and filename.
Choose a wheel matching your Python version, operating system, and architecture.

The release currently has no Python wheels. Build and install from its source tag:

```bash
git clone --branch 0.11.3 --depth 1 https://github.com/KoreDB/koredb.git
cd koredb/scripts/pip-package
python -m pip install setuptools wheel
python package_tar.py
python -m pip install ./koredb-0.11.3.tar.gz
```

This requires Git, Make, CMake 3.15 or later, Python development headers, and a C++20
compiler. See the [developer guide](https://koredb.github.io/docs/developer-guide/)
for platform setup, including the compiler environment on Windows.

The Python import remains `import koredb`. See the
[installation guide](https://koredb.github.io/docs/installation/#python) for details.
