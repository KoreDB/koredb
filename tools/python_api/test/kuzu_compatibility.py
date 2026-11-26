"""Verify that Kuzu 0.11.3 and the installed KoreDB can exchange database files."""

from __future__ import annotations

import hashlib
import importlib
import math
import subprocess
import sys
import unittest
from importlib.metadata import version
from pathlib import Path
from tempfile import TemporaryDirectory
from typing import TYPE_CHECKING, Any, cast

if TYPE_CHECKING:
    from koredb import Connection


KUZU_VERSION = "0.11.3"


def _execute(connection: Connection, statement: str) -> list[list[Any]]:
    result = connection.execute(statement)
    assert not isinstance(result, list), statement
    try:
        return cast("list[list[Any]]", result.get_all())
    finally:
        result.close()


def _create_graph(connection: Connection) -> None:
    _execute(connection, "CREATE NODE TABLE Doc(id INT64, body STRING, PRIMARY KEY(id))")
    _execute(connection, "CREATE REL TABLE LINKS(FROM Doc TO Doc, weight INT64)")
    _execute(connection, "CREATE (:Doc {id: 1, body: 'quantum telescope'})")
    _execute(connection, "CREATE (:Doc {id: 2, body: 'forest canopy'})")
    _execute(connection, "MATCH (a:Doc {id: 1}), (b:Doc {id: 2}) CREATE (a)-[:LINKS {weight: 1}]->(b)")


def _check_graph(connection: Connection, *, updated: bool) -> None:
    body = "forest observatory" if updated else "forest canopy"
    documents = _execute(connection, "MATCH (d:Doc) RETURN d.id, d.body ORDER BY d.id")
    assert documents == [[1, "quantum telescope"], [2, body]], documents
    links = _execute(connection, "MATCH (a:Doc)-[r:LINKS]->(b:Doc) RETURN a.id, b.id, r.weight")
    assert links == [[1, 2, 2 if updated else 1]], links


def _check_fts(connection: Connection, term: str, expected_ids: list[int]) -> None:
    rows = _execute(connection, f"CALL QUERY_FTS_INDEX('Doc', 'body_idx', '{term}') RETURN node.id, score")
    assert [node_id for node_id, _ in rows] == expected_ids, rows
    assert all(math.isfinite(score) and score > 0 for _, score in rows), rows


def _run_stage(engine: str, stage: str, scenario: str, path: Path) -> None:
    module = importlib.import_module(engine)
    if stage != "create":
        assert path.is_file(), path
    with (
        module.Database(path, read_only=stage in {"read", "reopen"}, max_num_threads=2) as database,
        module.Connection(database) as connection,
    ):
        if scenario == "empty":
            assert _execute(connection, "CALL SHOW_TABLES() RETURN name") == []
        else:
            if stage == "create":
                _create_graph(connection)
                if scenario == "fts":
                    _execute(connection, "CALL CREATE_FTS_INDEX('Doc', 'body_idx', ['body'])")

            if stage == "write":
                _execute(connection, "MATCH (d:Doc {id: 2}) SET d.body = 'forest observatory'")
                _execute(connection, "MATCH (:Doc {id: 1})-[r:LINKS]->(:Doc {id: 2}) SET r.weight = 2")

            updated = stage in {"write", "reopen"}
            _check_graph(connection, updated=updated)
            if scenario == "fts":
                _check_fts(connection, "quantum", [1])
                _check_fts(connection, "canopy", [] if updated else [2])
                if updated:
                    _check_fts(connection, "observatory", [2])

        if stage in {"create", "write"}:
            _execute(connection, "CHECKPOINT")


def _file_hash(path: Path) -> str:
    with path.open("rb") as database:
        return hashlib.file_digest(database, "sha256").hexdigest()


class KuzuCompatibilityTest(unittest.TestCase):
    """Exercise persisted files through two independent Python bindings."""

    @classmethod
    def setUpClass(cls) -> None:
        kuzu_version = version("kuzu")
        if kuzu_version != KUZU_VERSION:
            message = f"Expected kuzu=={KUZU_VERSION}, found {kuzu_version}"
            raise RuntimeError(message)

    def _verify(self, scenario: str) -> None:
        for source, peer in (("kuzu", "koredb"), ("koredb", "kuzu")):
            with self.subTest(source=source, peer=peer), TemporaryDirectory() as directory:
                path = Path(directory) / "graph.db"
                for engine, stage in ((source, "create"), (peer, "read"), (peer, "write"), (source, "reopen")):
                    read_only = stage in {"read", "reopen"}
                    before = _file_hash(path) if read_only else None
                    # Each stage exits before the next engine opens the same file.
                    result = subprocess.run(
                        [sys.executable, __file__, "--stage", engine, stage, scenario, str(path)],
                        capture_output=True,
                        text=True,
                        timeout=60,
                        check=False,
                    )
                    assert result.returncode == 0, (
                        f"{engine} {stage} exited with {result.returncode}\n{result.stdout}{result.stderr}"
                    )
                    assert path.is_file(), path
                    if read_only:
                        assert before == _file_hash(path), f"{engine} {stage} modified the database file"

    def test_empty_database(self) -> None:
        self._verify("empty")

    def test_graph_read_write(self) -> None:
        self._verify("graph")

    def test_full_text_index_read_write(self) -> None:
        self._verify("fts")


if __name__ == "__main__":
    if sys.argv[1:2] == ["--stage"]:
        engine, stage, scenario, filename = sys.argv[2:]
        _run_stage(engine, stage, scenario, Path(filename))
    else:
        unittest.main(verbosity=2)
