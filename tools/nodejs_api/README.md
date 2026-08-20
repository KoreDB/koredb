
# KoreDB Node.js API

A high-performance graph database for knowledge-intensive applications. This Node.js wrapper enables interaction with the KoreDB database via JavaScript or TypeScript using either **CommonJS** or **ES Modules**.

---

## 📦 Installation

```bash
npm install koredb
```

---

## 🚀 Quick Start

### Example (ES Modules)

```js
// Import the KoreDB module (ESM)
import { Database, Connection } from "koredb";

const main = async () => {
  // Initialize database and connection
  const db = new Database("./test");
  const conn = new Connection(db);

  // Define schema
  await conn.query(`
    CREATE NODE TABLE User(name STRING, age INT64, PRIMARY KEY (name));
  `);
  await conn.query(`
    CREATE NODE TABLE City(name STRING, population INT64, PRIMARY KEY (name));
  `);
  await conn.query(`
    CREATE REL TABLE Follows(FROM User TO User, since INT64);
  `);
  await conn.query(`
    CREATE REL TABLE LivesIn(FROM User TO City);
  `);

  // Load data from CSV files
  await conn.query(`COPY User FROM "user.csv"`);
  await conn.query(`COPY City FROM "city.csv"`);
  await conn.query(`COPY Follows FROM "follows.csv"`);
  await conn.query(`COPY LivesIn FROM "lives-in.csv"`);

  // Run a query
  const result = await conn.query("MATCH (u:User) RETURN u.name, u.age;");

  // Fetch all results
  const rows = await result.getAll();

  // Output results
  for (const row of rows) {
    console.log(row);
  }
};

main().catch(console.error);
```
 ✅ The dataset used in this example can be found in the [official KoreDB repository](https://github.com/kuzudb/kuzu/tree/master/dataset/demo-db/csv).

---

## ⚡ Electron

The native addon is built against **Node-API (`NAPI_VERSION=6`)**, which is ABI-stable
across both Node.js and Electron. The prebuilt binaries shipped in the npm package are
therefore loaded as-is by Electron &ge; 11 — there is **no** `electron-rebuild` step and
no separate Electron build.

Prebuilt binaries are published for all six supported targets:

| Platform | amd64 | arm64 |
| -------- | ----- | ----- |
| Windows  | ✅    | ✅    |
| macOS    | ✅    | ✅    |
| Linux    | ✅    | ✅    |

### Packaging an Electron app

Native addons cannot be loaded from inside an `asar` archive, so unpack them when
bundling with `electron-builder`:

```json
{
  "build": {
    "asarUnpack": ["**/node_modules/koredb/**"]
  }
}
```

With `electron-forge`, add the equivalent `packagerConfig.asar.unpack` entry.

### Running the Electron smoke test

`test/electron` is a minimal Electron app that loads the published package and runs a
query in the main process. CI runs it on every platform/architecture before publishing:

```bash
cd test/electron
npm install
npm install <path-to>/koredb-source.tar.gz
npm test
```

On headless Linux, run it under `xvfb-run -a npm test`.

---

## 📈 Benchmarking

```bash
npm install --include=dev
npm run build          # build the addon into ../build
npm run bench:gen      # generate bench/bench.koredb (KOREDB_BENCH_SCALE=1 by default)
KOREDB_BENCH_DB=bench/bench.koredb npm run bench -- --isolated
```

`--isolated` forks one process per query so each query gets a clean RSS baseline;
omit it to reuse a single process. Add `--materialize` to measure full `getAll()`
client-side materialization instead of streaming.

The `Node.js Benchmark` GitHub Actions workflow runs exactly this sequence and uploads
the report as an artifact.

---

## 📚 API Overview

The `koredb` package exposes the following primary classes:

* `Database` – Initializes a database from a file path.
* `Connection` – Executes queries on a connected database.
* `QueryResult` – Provides methods like `getAll()` to retrieve results.

Both CommonJS (`require`) and ES Modules (`import`) are fully supported.

---

## 🛠️ Local Development (for Contributors)

### Install Dev Dependencies

```bash
npm install --include=dev
```

### Build Project

```bash
npm run build
```

### Run Tests

```bash
npm test
```

---

## 📦 Packaging and Binary Distribution

We bundle all prebuilt binaries directly into the npm package, inspired by the approach used by [prebuildify](https://github.com/prebuild/prebuildify).

>  All prebuilt binaries are shipped inside the package that is published to npm, which means there's no need for a separate download step like you find in [`prebuild`](https://github.com/prebuild/prebuild). The irony of this approach is that it is faster to download all prebuilt binaries for every platform when they are bundled than it is to download a single prebuilt binary as an install script.

### Requirements (for building from source)

If a prebuilt binary is unavailable for your platform, the module will be built from source during installation. Ensure the following tools are installed:

* **CMake** (≥ 3.15)
* **Python 3**
* A **C++20-compatible compiler**

### Packaging Prebuilt Binaries

1. Place your binaries inside the `prebuilt` directory.
2. Name them using the format:

   ```
   koredbjs-${platform}-${arch}.node
   ```
3. Run the packaging script:

```bash
node package
```

If no binaries are found, a source-only tarball will be generated.

---

## 🚀 Publishing

To publish the package to npm:

```bash
npm publish
```

Refer to the [npm documentation](https://docs.npmjs.com/cli/v9/commands/npm-publish) for full details on publishing and versioning.

---

## 🔗 Resources

* [KoreDB GitHub](https://github.com/kuzudb/kuzu)
* [KoreDB Documentation](https://docs.kuzudb.com)
* [Issue Tracker](https://github.com/kuzudb/kuzu/issues)
