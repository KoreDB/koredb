# KoreDB
KoreDB is an embedded graph database built for query speed and scalability. KoreDB is optimized for handling complex analytical workloads 
on very large databases and provides a set of retrieval features, such as a full text search and vector indices. Our core feature set includes:

- Flexible Property Graph Data Model and Cypher query language
- Embeddable, serverless integration into applications
- Native full text search and vector index
- Columnar disk-based storage
- Columnar sparse row-based (CSR) adjacency list/join indices
- Vectorized and factorized query processor
- Novel and very fast join algorithms
- Multi-core query parallelism
- Serializable ACID transactions
- Wasm (WebAssembly) bindings for fast, secure execution in the browser

KoreDB is derived from [Kuzu](https://github.com/kuzudb/kuzu), which was initially developed by
Kùzu Inc. and released under the [MIT License](LICENSE). We thank the Kuzu team and
contributors for their work on the database engine and its documentation.

## Storage Format Compatibility

**KoreDB will always maintain compatibility with the Kuzu 0.11.x storage format.**
This commitment applies to all future KoreDB releases. Storage changes must preserve
compatibility with existing Kuzu 0.11.x database files.

## Docs and Blog

To learn more about KoreDB, see our [Documentation](https://koredb.github.io/docs/).
For background on the upstream project, see the [Kuzu Blog](https://kuzudb.github.io/blog).

## Installation

Install the Node.js package with `npm install @koredb/koredb` and import it with
`require("@koredb/koredb")` or `import { Database, Connection } from "@koredb/koredb"`.

Install the Python package directly from this GitHub repository:

The Python package supports standard CPython 3.12 and later. Published wheels use
the CPython stable ABI and are built once for each supported operating system and
CPU architecture. Free-threaded CPython builds are not currently supported.

```bash
python -m pip install "git+https://github.com/KoreDB/koredb.git"
```

This compiles the native extension and requires Git, Make, CMake 3.15 or newer,
Python development headers, and a C++20 compiler. On Windows, use an MSVC
developer environment with Ninja and GNU Make available. See the
[Python build instructions](tools/python_api/README.md) for details and examples.
The GitHub command installs a repository revision containing this packaging support;
older releases may require their own installation instructions.
See the [installation guide](https://koredb.github.io/docs/installation/) for all interfaces.

## Getting Started

Refer to our [Getting Started](https://koredb.github.io/docs/get-started/) page for your first example.

## Extensions
KoreDB has an extension framework that users can dynamically load the functionality you need at runtime.
See the list of [available extensions](https://koredb.github.io/docs/extensions/#available-extensions) that you can use to extend KoreDB's functionality.

Extensions that are not built in must be installed before loading and using them.
Note that KoreDB no longer provides the official extension server, where you can directly install any official extensions.

KoreDB 0.11.3 and default native source builds, including Release builds and the Node.js
package, statically link four extensions: `algo`, `fts`, `json`, and `vector`. They load
automatically when a database opens. Existing `INSTALL` and `LOAD EXTENSION` statements
for these names succeed without downloading or loading them again.

For KoreDB versions before v0.11.3, or to install extensions that haven't been pre-installed, you have to set up a local extension server.
The instructions of setting up a local extension server can be found below.

### Host your own extension server

The extension server is based on NGINX and is hosted on [GitHub](https://ghcr.io/kuzudb/extension-repo). You can pull the Docker image and run it in your environment:

```bash
docker pull ghcr.io/kuzudb/extension-repo:latest
docker run -d -p 8080:80 ghcr.io/kuzudb/extension-repo:latest
```

In this example, the extension server will be available at `http://localhost:8080`. You can then install extensions from your server by appending the `FROM` clause to the `INSTALL` command:

```cypher
INSTALL <EXTENSION_NAME> FROM 'http://localhost:8080/';
```

## Build from Source

The test fixtures live in a separate repository,
[KoreDB/dataset](https://github.com/KoreDB/dataset), mounted as a git submodule
at `dataset/`. Clone recursively so the test suite has data to run against:

```bash
git clone --recurse-submodules https://github.com/KoreDB/koredb.git
```

For an existing clone, run `git submodule update --init dataset`. Building the
library itself does not need the submodule — only the tests do.

You can build from source using the instructions provided in the [developer guide](https://koredb.github.io/docs/developer-guide/).

## License
KoreDB is licensed under the [MIT License](LICENSE).
