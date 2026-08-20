import sys
from pathlib import Path

KOREDB_ROOT = Path(__file__).parent.parent.parent.parent

if sys.platform == "win32":
    # \ in paths is not supported by koredb's parser
    KOREDB_ROOT = str(KOREDB_ROOT).replace("\\", "/")
