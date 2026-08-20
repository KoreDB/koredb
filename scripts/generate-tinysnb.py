import os
import shutil
import subprocess
import sys

KOREDB_ROOT = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))
# Datasets can only be copied from the root since copy.schema contains relative paths
os.chdir(KOREDB_ROOT)

# Define the build type from input
if len(sys.argv) > 1 and sys.argv[1].lower() == "release":
    build_type = "release"
else:
    build_type = "relwithdebinfo"

# Change the current working directory
if os.path.exists(f"{KOREDB_ROOT}/dataset/databases/tinysnb"):
    shutil.rmtree(f"{KOREDB_ROOT}/dataset/databases/tinysnb")
if sys.platform == "win32":
    koredb_shell_path = f"{KOREDB_ROOT}/build/{build_type}/src/koredb_shell"
else:
    koredb_shell_path = f"{KOREDB_ROOT}/build/{build_type}/tools/shell/koredb"
subprocess.check_call(
    [
        "python3",
        f"{KOREDB_ROOT}/benchmark/serializer.py",
        "TinySNB",
        f"{KOREDB_ROOT}/dataset/tinysnb",
        f"{KOREDB_ROOT}/dataset/databases/tinysnb",
        "--single-thread",
        "--koredb-shell",
        koredb_shell_path,
    ]
)
