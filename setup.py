"""Build the Python bindings directly from a KoreDB source checkout."""

import os
import importlib.machinery
from contextlib import contextmanager
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext

ROOT = Path(__file__).resolve().parent
version_match = re.search(
    r"^project\(KoreDB VERSION ([0-9]+\.[0-9]+\.[0-9]+)(?:\.([0-9]+))?",
    (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"),
    re.MULTILINE,
)
if not version_match:
    raise RuntimeError("Could not determine KoreDB version from CMakeLists.txt")
version = version_match.group(1)
if version_match.group(2):
    version += ".dev" + version_match.group(2)


@contextmanager
def temporary_source(build_temp):
    source = Path(tempfile.mkdtemp(prefix="koredb-", dir=build_temp))
    try:
        yield source
    finally:
        # Windows can temporarily hold build outputs open after compilation.
        shutil.rmtree(source, ignore_errors=True)


class CMakeBuild(build_ext):
    def build_extension(self, ext):
        # CMake writes the bindings into the source tree. Work on a private copy
        # so pip install . never cleans or overwrites the caller's checkout.
        build_temp = Path(self.build_temp).resolve()
        build_temp.mkdir(parents=True, exist_ok=True)
        with temporary_source(build_temp) as source:
            for name in (
                "CMakeLists.txt",
                "Makefile",
                "tools/CMakeLists.txt",
                "tools/python_api/CMakeLists.txt",
            ):
                destination = source / name
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(ROOT / name, destination)
            for name in (
                "cmake", "src", "third_party", "extension", "scripts",
                "tools/python_api/src_cpp", "tools/python_api/src_py",
            ):
                shutil.copytree(
                    ROOT / name,
                    source / name,
                    ignore=shutil.ignore_patterns(
                        ".git", ".local", ".venv", "build", "wheelhouse", "__pycache__", "*.egg-info"
                    ),
                )
            env = os.environ.copy()
            env["PYTHON_EXECUTABLE"] = sys.executable
            env["PYBIND11_PYTHON_VERSION"] = f"{sys.version_info.major}.{sys.version_info.minor}"
            threads = env.get("NUM_THREADS", str(min(4, os.cpu_count() or 1)))
            subprocess.run(["make", "python", f"NUM_THREADS={threads}"], cwd=source, env=env, check=True)
            built_package = source / "tools" / "python_api" / "build" / "koredb"
            candidates = [built_package / ("_koredb" + suffix) for suffix in importlib.machinery.EXTENSION_SUFFIXES]
            binary = next((path for path in candidates if path.is_file()), None)
            if binary is None:
                raise RuntimeError(f"Python extension not found in {built_package}")
            destination = Path(self.get_ext_fullpath(ext.name))
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(binary, destination)


setup(
    name="koredb",
    version=version,
    description="An embedded graph database for query speed and scalability",
    long_description=(ROOT / "README.md").read_text(encoding="utf-8"),
    long_description_content_type="text/markdown",
    url="https://github.com/KoreDB/koredb",
    license="MIT",
    python_requires=">=3.9",
    packages=["koredb"],
    package_dir={"koredb": "tools/python_api/src_py"},
    package_data={"koredb": ["py.typed"]},
    ext_modules=[Extension("koredb._koredb", sources=[])],
    cmdclass={"build_ext": CMakeBuild},
    zip_safe=False,
)
