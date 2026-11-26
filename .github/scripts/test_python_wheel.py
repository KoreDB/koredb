"""Install and exercise the wheel built once for this operating system and CPU."""

import pathlib
import subprocess
import sys

wheels = list(pathlib.Path(sys.argv[1]).glob("*.whl"))
if len(wheels) != 1 or "-cp312-abi3-" not in wheels[0].name:
    raise SystemExit(f"Expected one CPython 3.12 stable ABI wheel, found: {wheels}")

subprocess.run([sys.executable, "-m", "pip", "install", "--force-reinstall", str(wheels[0])], check=True)

import koredb  # noqa: E402

db = koredb.Database()
connection = koredb.Connection(db)
assert connection.execute("RETURN '漢字 🐟', 42").get_next() == ["漢字 🐟", 42]
