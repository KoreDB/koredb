def test_version() -> None:
    import koredb

    assert koredb.version != ""
    assert koredb.storage_version > 0
    assert koredb.version == koredb.__version__
