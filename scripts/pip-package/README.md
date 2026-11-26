# Install the Python package from release source

Use the [KoreDB 0.11.3 Release](https://github.com/KoreDB/koredb/releases/tag/0.11.3)
as the package source. To build the Python source distribution for that release:

```bash
git clone --branch 0.11.3 --depth 1 https://github.com/KoreDB/koredb.git
cd koredb/scripts/pip-package
python -m pip install setuptools wheel
python package_tar.py
python -m pip install ./koredb-0.11.3.tar.gz
```

Run `package_tar.py` from `scripts/pip-package` in a Git checkout: it uses `git archive HEAD`
and reads packaging files relative to that directory. The generated tarball is a Python source
distribution, unlike GitHub's repository source archive.

Installing the current source requires standard CPython 3.12 or later, Git, Make,
CMake 3.26 or later, Python development headers,
and a C++20 compiler. See the [developer guide](https://koredb.github.io/docs/developer-guide/)
for platform setup. For other source versions, install the tarball name reported by the script.

## Publishing

The `Publish Python package` GitHub Actions workflow builds and tests wheels on
Linux, macOS, and Windows, builds a source distribution, and publishes to PyPI
when a tag is pushed. The tag must match the version in the root
`CMakeLists.txt`, with an optional `v` prefix. A manual workflow dispatch can
publish the selected revision to PyPI or TestPyPI; a selected tag is checked
against the package version as well. PyPI does not allow files to be replaced
for an existing release, so update the version before publishing a new release.

Each operating system and CPU architecture gets one `cp312-abi3` wheel. The
same wheel is installed and tested with each supported standard CPython minor
version. Free-threaded CPython builds require a different ABI and are not
currently supported.

Configure a PyPI Trusted Publisher for the `koredb` project (or a pending
publisher before the first release), repository `KoreDB/koredb`, workflow
`publish-python.yml`, and GitHub environment `pypi`. Configure the equivalent
publisher on TestPyPI with environment `testpypi` for test uploads. See the
[PyPI Trusted Publishing guide](https://docs.pypi.org/trusted-publishers/).
