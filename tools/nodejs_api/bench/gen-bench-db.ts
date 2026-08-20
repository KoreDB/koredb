// Generates a reproducible on-disk KoreDB database for `npm run bench`.
//
// The benchmark fixtures that this script replaces (`test/demo_large.kuzu`)
// are large and git-ignored, so CI has nothing to benchmark against. This
// generator builds an equivalent-shaped database from scratch: several node
// tables (so that queries filtering on `internal_id(<table>, <offset>)` select a
// meaningful subset) joined by several rel tables.
//
//   node bench/dist/gen-bench-db.js [output-path]
//
// Environment:
//   KOREDB_BENCH_SCALE  multiplier applied to every table's cardinality (default 1)

import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";

// eslint-disable-next-line @typescript-eslint/no-var-requires
const koredb = require("../../build/") as typeof import("../src_js/koredb").default;

interface NodeTableSpec {
  readonly name: string;
  readonly count: number;
}

interface RelTableSpec {
  readonly name: string;
  readonly from: string;
  readonly to: string;
  readonly count: number;
}

const SCALE = Number(process.env.KOREDB_BENCH_SCALE ?? "1") || 1;
const scaled = (n: number): number => Math.max(1, Math.round(n * SCALE));

// Table declaration order fixes the table ids the benchmark queries rely on:
// Person=0, Post=1, Tag=2, Forum=3.
const NODE_TABLES: readonly NodeTableSpec[] = [
  { name: "Person", count: scaled(100_000) },
  { name: "Post", count: scaled(50_000) },
  { name: "Tag", count: scaled(5_000) },
  { name: "Forum", count: scaled(2_000) },
];

const REL_TABLES: readonly RelTableSpec[] = [
  { name: "Knows", from: "Person", to: "Person", count: scaled(400_000) },
  { name: "Likes", from: "Person", to: "Post", count: scaled(200_000) },
  { name: "HasTag", from: "Post", to: "Tag", count: scaled(100_000) },
  { name: "Contains", from: "Forum", to: "Post", count: scaled(50_000) },
];

// A cheap deterministic PRNG keeps successive runs comparable without pulling
// in a dependency.
function makeRandom(seed: number): () => number {
  let state = seed >>> 0;
  return () => {
    state = (state * 1664525 + 1013904223) >>> 0;
    return state / 0x1_0000_0000;
  };
}

function csvEscape(value: string): string {
  return `"${value.replace(/"/g, '""')}"`;
}

function writeNodeCsv(dir: string, spec: NodeTableSpec): string {
  const random = makeRandom(spec.name.length * 7919 + spec.count);
  const rows: string[] = [];
  for (let i = 0; i < spec.count; i++) {
    rows.push(
      [
        i,
        csvEscape(`${spec.name}-${i}`),
        Math.floor(random() * 100),
        (random() * 1000).toFixed(4),
      ].join(",")
    );
  }
  const path = join(dir, `${spec.name}.csv`);
  writeFileSync(path, rows.join("\n") + "\n");
  return path;
}

function writeRelCsv(
  dir: string,
  spec: RelTableSpec,
  cardinality: ReadonlyMap<string, number>
): string {
  const fromCount = cardinality.get(spec.from) ?? 1;
  const toCount = cardinality.get(spec.to) ?? 1;
  const random = makeRandom(spec.name.length * 104_729 + spec.count);
  const rows: string[] = [];
  for (let i = 0; i < spec.count; i++) {
    rows.push(
      [
        Math.floor(random() * fromCount),
        Math.floor(random() * toCount),
        Math.floor(random() * 10_000),
      ].join(",")
    );
  }
  const path = join(dir, `${spec.name}.csv`);
  writeFileSync(path, rows.join("\n") + "\n");
  return path;
}

// COPY FROM takes a filesystem path embedded in the query text; on Windows the
// backslashes would otherwise be read as escape sequences.
function cypherPath(path: string): string {
  return path.split("\\").join("/");
}

async function main(): Promise<void> {
  const outputPath = resolve(process.argv[2] ?? join(__dirname, "..", "bench.koredb"));
  const csvDir = mkdtempSync(join(tmpdir(), "koredb-bench-csv-"));

  console.log(`Scale factor  : ${SCALE}`);
  console.log(`Output database: ${outputPath}`);

  rmSync(outputPath, { recursive: true, force: true });
  rmSync(`${outputPath}.wal`, { force: true });

  const database = new koredb.Database(outputPath);
  const connection = new koredb.Connection(database);

  try {
    const cardinality = new Map<string, number>();
    for (const spec of NODE_TABLES) {
      cardinality.set(spec.name, spec.count);
      await connection.query(
        `CREATE NODE TABLE ${spec.name}(id INT64, name STRING, score INT64, weight DOUBLE, PRIMARY KEY(id))`
      );
    }
    for (const spec of REL_TABLES) {
      await connection.query(
        `CREATE REL TABLE ${spec.name}(FROM ${spec.from} TO ${spec.to}, weight INT64)`
      );
    }

    for (const spec of NODE_TABLES) {
      const csv = writeNodeCsv(csvDir, spec);
      console.log(`Loading ${spec.count} ${spec.name} nodes...`);
      await connection.query(`COPY ${spec.name} FROM '${cypherPath(csv)}'`);
    }
    for (const spec of REL_TABLES) {
      const csv = writeRelCsv(csvDir, spec, cardinality);
      console.log(`Loading ${spec.count} ${spec.name} rels...`);
      await connection.query(`COPY ${spec.name} FROM '${cypherPath(csv)}'`);
    }
  } finally {
    await connection.close();
    await database.close();
    rmSync(csvDir, { recursive: true, force: true });
  }

  console.log(`Done. Run the benchmark with KOREDB_BENCH_DB=${outputPath}`);
}

main().catch((error: unknown) => {
  console.error(error);
  process.exit(1);
});
