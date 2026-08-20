import os
import sys
from enum import Enum
from pathlib import Path

KOREDB_ROOT = Path(__file__).parent.parent.parent.parent
if sys.platform == "win32":
    # \ in paths is not supported by koredb's parser
    KOREDB_ROOT = str(KOREDB_ROOT).replace("\\", "/")

KOREDB_EXEC_PATH = os.path.join(
    KOREDB_ROOT,
    "build",
    "release",
    "tools",
    "shell",
    "koredb",
)


def _get_koredb_version():
    cmake_file = os.path.join(KOREDB_ROOT, "CMakeLists.txt")
    with open(cmake_file) as f:
        for line in f:
            if line.startswith("project(KoreDB VERSION"):
                return line.split(" ")[2].strip()
        return None


KOREDB_VERSION = _get_koredb_version()


class KEY_ACTION(Enum):
    KEY_NULL = "\0"  # NULL
    CTRL_A = "a"  # Ctrl-a
    CTRL_B = "b"  # Ctrl-b
    CTRL_C = "c"  # Ctrl-c
    CTRL_D = "d"  # Ctrl-d
    CTRL_E = "e"  # Ctrl-e
    CTRL_F = "f"  # Ctrl-f
    CTRL_G = "g"  # Ctrl-g
    CTRL_H = chr(8)  # Ctrl-h
    TAB = "\t"  # Tab
    CTRL_K = "k"  # Ctrl-k
    CTRL_L = "l"  # Ctrl-l
    ENTER = "\r"  # Enter
    CTRL_N = "n"  # Ctrl-n
    CTRL_P = "p"  # Ctrl-p
    CTRL_R = "r"  # Ctrl-r
    CTRL_S = "s"  # Ctrl-s
    CTRL_T = "t"  # Ctrl-t
    CTRL_U = "u"  # Ctrl-u
    CTRL_W = chr(23)  # Ctrl-w
    ESC = "\27"  # Escape
    BACKSPACE = chr(127)  # Backspace


def deleteIfExists(file) -> None:
    if os.path.exists(file):
        os.remove(file)
