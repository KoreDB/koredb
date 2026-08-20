
# KoreDB Node.js API

A high-performance graph database for knowledge-intensive applications. This Node.js wrapper enables interaction with the KoreDB database via JavaScript or TypeScript using either **CommonJS** or **ES Modules**.

---

## 📦 Installation

```bash
npm install @koredb/koredb
```

The `@koredb/koredb` package is pure JavaScript. The native addon lives in a separate
package per platform and architecture, declared as `optionalDependencies`, so a
normal install downloads exactly one binary — the one matching your machine:

| Package | Contents |
| ------- | -------- |
| `@koredb/koredb` | JavaScript API and the addon loader (~60 KB) |
| `@koredb/koredb-linux-x64` | prebuilt addon for `linux-x64` |
| `@koredb/koredb-linux-arm64` | prebuilt addon for `linux-arm64` |
| `@koredb/koredb-darwin-x64` | prebuilt addon for `darwin-x64` |
| `@koredb/koredb-darwin-arm64` | prebuilt addon for `darwin-arm64` |
| `@koredb/koredb-win32-x64` | prebuilt addon for `win32-x64` |
| `@koredb/koredb-win32-arm64` | prebuilt addon for `win32-arm64` |

Never depend on a `@koredb/*` package directly — npm, yarn and pnpm select the
right one from the `os` and `cpu` fields.

There is no install script and no compilation step. Platforms outside that
matrix (musl-based Linux such as Alpine, armv7, FreeBSD) have no prebuilt
binary; see [Building from source](#🛠-building-from-source).

## 🚀 Quick Start

### Example (ES Modules)

```js
// Import the KoreDB module (ESM)
import { Database, Connection } from "@koredb/koredb";

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
 ✅ The dataset used in this example can be found in the [official KoreDB repository](https://github.com/KoreDB/koredb/tree/main/dataset/demo-db/csv).

---

## ⚡ Electron

The native addon is built against **Node-API (`NAPI_VERSION=6`)**, which is ABI-stable
across both Node.js and Electron. The addon package installed for your platform is
therefore loaded as-is by Electron &ge; 11 — there is **no** `electron-rebuild` step and
no separate Electron build. Neither the main package nor the addon packages contain a
`binding.gyp` or an install script, so `@electron/rebuild` and `electron-builder`'s
`npmRebuild` skip them, which is exactly what you want.

Prebuilt binaries are published for all six supported targets:

| Platform | x64 | arm64 | Minimum |
| -------- | --- | ----- | ------- |
| Windows  | ✅ | ✅ | Windows 10 |
| macOS    | ✅ | ✅ | macOS 11 |
| Linux    | ✅ | ✅ | glibc 2.28 (RHEL 8 / Ubuntu 20.04) |

These floors are at or below Electron's own platform requirements, so any Electron
version that runs on a machine can load the addon on it.

### Packaging for another platform or architecture

An Electron app is routinely packaged for a target other than the build machine.
Because the addon is chosen at *install* time, install the dependency tree for
the target before packaging:

```bash
# building a macOS arm64 app from any machine
npm install --os=darwin --cpu=arm64
```

`--os` / `--cpu` redirect optional-dependency resolution at the requested target
(npm &ge; 10.6). The equivalents for other package managers:

| Package manager | How to select a foreign target |
| --------------- | ------------------------------ |
| npm &ge; 10.6 | `npm install --os=darwin --cpu=arm64` |
| pnpm | `pnpm.supportedArchitectures` in `package.json` |
| yarn &ge; 3 | `supportedArchitectures` in `.yarnrc.yml` |

For a macOS universal build, or any build that needs two architectures at once,
install both addon packages explicitly and let the loader pick at runtime:

```bash
npm install --os=darwin --cpu=x64
npm install --os=darwin --cpu=arm64
```

### Packaging an Electron app

Unpack the addon from the `asar` archive. Electron *can* load a `.node` from
inside an archive by extracting it to a temporary directory first, but that is
slower and breaks under hardened sandboxes and read-only temp directories.

`electron-builder`:

```json
{
  "build": {
    "asarUnpack": ["**/node_modules/@koredb/**"]
  }
}
```

`electron-forge` (`forge.config.js`):

```js
module.exports = {
  packagerConfig: {
    asar: { unpack: "**/node_modules/@koredb/**" },
  },
};
```

Nothing needs excluding: the main package is JavaScript only, and the single
addon package that gets installed contains just the binary.

### Bundlers

The addon is loaded with `process.dlopen()` against a path derived from `__dirname`, so
`@koredb/koredb` must stay **external**. A bundler that inlines it will emit code that looks for
`koredbjs.node` next to the bundle instead of inside the package.

| Bundler | Configuration |
| ------- | ------------- |
| webpack | `externals: { "@koredb/koredb": "commonjs @koredb/koredb" }` |
| Vite / electron-vite | `build.rollupOptions.external: ["@koredb/koredb"]` |
| esbuild | `--external:@koredb/koredb` |

### Main process vs. renderer process

Use KoreDB from the **main process** (or a `utilityProcess`) and expose the results to
renderers over IPC. Loading a native addon in a renderer requires `nodeIntegration: true`
together with `contextIsolation: false`, which Electron discourages for security reasons.
Queries run on a libuv worker thread, so they do not block the main process event loop.

### Running the Electron smoke test

`test/electron` is a minimal Electron app that loads the published package and runs a
query in the main process. Two GitHub Actions workflows drive it:

* **`Node.js Electron CI`** runs on every push and pull request. It builds the addon
  from source on Linux x64, packages it, and runs the smoke test — the fast guard on
  the addon, the packaging layout and the JS entry points.
* **`Node.js Electron Test`** runs the same smoke test on all six
  platform/architecture combinations against the prebuilt release binaries. It is the
  gate the release pipeline must pass before anything is published.

Both share the `.github/actions/electron-smoke-test` composite action, so what runs
locally, on every commit, and before a release is the same sequence:

```bash
cd test/electron
npm install
node install-local.js ../../dist   # installs the locally packaged tarballs
npm test
```

`install-local.js` reads `dist/packages.json`, then installs the main package
together with the addon package for the current target. It records both in this
app’s `package.json`; run `git checkout package.json` afterwards to discard
that.

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

The `@koredb/koredb` package exposes the following primary classes:

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

## 🛠 Building from source

If your platform has no prebuilt binary, build the addon and link it:

```bash
git clone https://github.com/KoreDB/koredb.git
cd koredb
make nodejs
npm link tools/nodejs_api/build
```

`make nodejs` writes the addon and the JavaScript files to
`tools/nodejs_api/build/`. The loader prefers a `koredbjs.node` sitting next to
its own JavaScript, so a linked build is used ahead of any installed
`@koredb/*` package.

Requirements: **CMake** (≥ 3.15), **Python 3**, and a **C++20** compiler.

---

## 📦 Packaging and Binary Distribution

The npm artifacts are produced by `node package`, which reads the prebuilt
binaries from `prebuilt/` and writes tarballs to `dist/`:

1. Place the binaries in `prebuilt/`, named `koredbjs-${platform}-${arch}.node`.
2. Run the packaging script:

   ```bash
   node package
   ```

That emits one tarball per platform binary found, one tarball for the main
package, and a `dist/packages.json` manifest. The main package declares exactly
the platform packages that were built, pinned to the exact version read from
the root `CMakeLists.txt`, so a partial build never advertises a package that
does not exist.

The `Release Node.js Packages` GitHub Actions workflow builds all six binaries, runs
`node package`, and gates publication on the Electron smoke test.

---

## 🚀 Publishing

```bash
node publish.js --tag latest            # or --tag next for nightlies
node publish.js --tag latest --dry-run
```

`publish.js` reads `dist/packages.json` and publishes the platform packages
**before** the main package, so `@koredb/koredb` is never resolvable on the registry
before the addon it pins as an optional dependency is.

Publishing is never automatic. It happens by dispatching the `Release Node.js
Packages` workflow and ticking **Publish to npm?**; leaving that box unchecked runs
the full build, the six-platform Electron matrix and a `--dry-run` publish, which is
also the way to exercise the whole release path without shipping anything.

---

## 🔗 Resources

* [KoreDB GitHub](https://github.com/KoreDB/koredb)
* [KoreDB Documentation](https://koredb.github.io/docs/)
* [Issue Tracker](https://github.com/KoreDB/koredb/issues)
