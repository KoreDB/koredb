# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Bighorn is a fork of [Kuzu](https://github.com/kuzudb/kuzu), an embedded graph database written in C++ (C++20). It implements the Cypher query language over a property-graph data model, with columnar disk-based storage, a vectorized + factorized query processor, and serializable ACID transactions. The core engine lives in `src/`; language bindings (Python, Java, Node.js, Rust, Wasm, shell) live in `tools/`; pluggable functionality lives in `extension/`.

## Build & Test Commands

The `Makefile` is a thin wrapper over CMake. All targets build into `build/<build-type>/` (e.g. `build/release`, `build/relwithdebinfo`, `build/debug`).

```bash
make release            # optimized build (default goal)
make debug              # debug build
make relwithdebinfo     # optimized + debug info
make all                # everything (all bindings, extensions, tests, benchmark)
make clean              # remove build/ and binding build dirs
```

Useful build knobs are passed as environment variables (translated to CMake flags in the Makefile): `NUM_THREADS`, `RUNTIME_CHECKS=1` (enables asserts/coherency checks — CI runs with this on), `WERROR=1`, `ASAN=1`, `TSAN=1`, `UBSAN=1`, `LTO=1`. Extension list is controlled by `EXTENSION_LIST`.

### Tests

```bash
make test               # builds RelWithDebInfo with tests, runs full ctest suite
make test TEST_JOBS=10  # control test parallelism (default 10)
```

The bulk of correctness testing is **e2e `.test` files** under `test/test_files/`, executed by the `e2e_test` runner (`test/runner/e2e_test.cpp`). The `.test` format is custom: `-DATASET` declares the dataset, `-CASE` names a case, `-STATEMENT` gives a Cypher query, and `---- N` introduces N expected result lines. To run a **single** test file/directory after building tests, invoke the runner binary directly with a path relative to `test/test_files`:

```bash
# after `make test-build`
./build/relwithdebinfo/test/runner/e2e_test agg/hash.test
```

Other test entrypoints: `make pytest` (Python), `make javatest`, `make nodejstest`, `make rusttest`, `make wasmtest`, `make shell-test`, `make extension-test` (extension e2e; uses `EXTENSION_TEST_EXCLUDE_FILTER` to skip groups), `make lcov` (coverage).

### Lint / Format / Sanity Checks (run before pushing)

CI enforces these — replicate them locally:

```bash
python3 scripts/run-clang-format.py --in-place -r src/   # also: test/ tools/ extension/
make tidy               # run-clang-tidy over src/extension/tools (slow; needs full configure)
make tidy-analyzer      # clang static analyzer pass
./scripts/check-include-guards.sh src/include             # also test/include, extension dirs
./scripts/check-no-std-assert.sh src                      # std assert() is banned; use project macros
```

Formatting rules live in `.clang-format`; lint config in `.clang-tidy` / `.clang-tidy-analyzer`.

## Architecture

### Query execution pipeline

A Cypher string flows through these stages, each a top-level directory under `src/` (and mirrored in `src/include/`):

1. **`parser/`** — ANTLR4-generated grammar (`src/antlr4/`, generated from the Cypher `.g4`) parses text into a parsed-statement AST. Grammar regenerates automatically on change unless `AUTO_UPDATE_GRAMMAR` is off.
2. **`binder/`** — resolves names against the catalog, type-checks, and produces a bound statement with `expression/` trees.
3. **`planner/`** — builds a logical plan (logical operators).
4. **`optimizer/`** — rewrites the logical plan (join order, filter pushdown, factorization, etc.).
5. **`processor/`** — maps the logical plan to physical operators (`processor/operator/`, `processor/map/`) and executes them. The processor is **vectorized** (data flows in column vectors, capacity set by `KUZU_VECTOR_CAPACITY_LOG2`) and **factorized**.

### Supporting subsystems

- **`storage/`** — disk-based columnar storage. Key pieces: `buffer_manager/` (paged buffer pool), `table/` (node & rel tables; rels use CSR adjacency lists), `index/` (hash index, etc.), `compression/`, `wal/` (write-ahead log), `local_storage/` (uncommitted transaction-local data), `checkpointer.cpp`, `page_manager.cpp`. Page size, node-group size, and segment size are compile-time constants (`KUZU_PAGE_SIZE_LOG2`, `KUZU_NODE_GROUP_SIZE_LOG2`, `KUZU_MAX_SEGMENT_SIZE_LOG2`).
- **`catalog/`** — schema metadata (tables, properties, indexes, functions).
- **`transaction/`** — MVCC, undo buffer, serializable ACID semantics.
- **`function/`** — scalar, aggregate, table, and cast functions.
- **`graph/`** — in-memory graph abstraction used by graph-algorithm functions.
- **`common/`** — shared types, value/vector representations, exceptions, utilities used across all layers.
- **`main/`** — public embedding surface: `Database`, `Connection`, `ClientContext`, `PreparedStatement`, `QueryResult`, attached/external databases (`attached_database.cpp`, `database_manager.cpp`), settings.
- **`c_api/`** — C ABI wrapping `main/`; the basis for several language bindings.

### Extensions (`extension/`)

Each subdirectory (`json`, `fts`, `vector`, `algo`, `httpfs`, `duckdb`, `postgres`, `sqlite`, `delta`, `iceberg`, `azure`, `neo4j`, `unity_catalog`, `llm`) is an independently buildable module that registers functions/storage at load time. Build with `make extension-build EXTENSION_LIST="json;fts;..."`. Extensions can be dynamically loaded (`INSTALL`/`LOAD` in Cypher) or statically linked (`EXTENSION_STATIC_LINK_LIST`).

### Language bindings (`tools/`)

`python_api/`, `java_api/`, `nodejs_api/`, `rust_api/`, `wasm/`, `shell/` (interactive CLI), `benchmark/`. Each wraps the C++/C API and has its own build/test flow surfaced through the Makefile targets above.

## Conventions

- Do not commit directly to `master`; use a fork + PR (see `CONTRIBUTING.md`). New features and bug fixes must come with tests — prefer adding `.test` e2e cases under `test/test_files/`.
- `assert()` from `<cassert>` is forbidden in `src/` and `extension/` (enforced by `check-no-std-assert.sh`); use the project's assertion/exception macros instead.
- Header include guards are checked by `check-include-guards.sh`.
- The single-file amalgamated header is generated during build (`BUILD_SINGLE_FILE_HEADER`); pass `SKIP_SINGLE_FILE_HEADER=1` to skip it during fast iteration.
