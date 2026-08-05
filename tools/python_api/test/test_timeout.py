from __future__ import annotations

import pytest
from type_aliases import ConnDB


def test_timeout(conn_db_readonly: ConnDB) -> None:
    conn, _ = conn_db_readonly
    # Disable partial-result-on-timeout (on by default) to exercise the abort-on-timeout path.
    conn.execute("CALL enable_partial_result_on_timeout=false;")
    conn.set_query_timeout(1000)
    with pytest.raises(RuntimeError, match=r"Interrupted."):
        conn.execute("UNWIND RANGE(1,100000) AS x UNWIND RANGE(1, 100000) AS y RETURN COUNT(x + y);")


def test_timeout_partial_results(conn_db_readonly: ConnDB) -> None:
    # With partial-result-on-timeout enabled (the default), a read-only query that times out returns
    # the tuples produced so far, flagged via QueryResult.is_truncated(), instead of raising.
    conn, _ = conn_db_readonly
    conn.set_query_timeout(1)
    result = conn.execute("UNWIND RANGE(1, 100000) AS x UNWIND RANGE(1, 100000) AS y RETURN x, y;")
    assert result.is_truncated()
