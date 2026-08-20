const { assert } = require("chai");
const tmp = require("tmp");
const path = require("path");
const { spawn } = require("child_process");

const MB = 1024 * 1024;

// The scan fixture holds ~85 bytes of string payload per row, so 500k rows put ~45 MB of
// string data on disk -- comfortably more than the small buffer pool tested below, which
// forces the buffer manager to evict rather than keep the whole column resident.
const SCAN_ROWS = 500000;
const SCAN_PAD = "x".repeat(80);

const SMALL_POOL = 16 * MB;
const LARGE_POOL = 256 * MB;

// Smallest max-db-size the buffer manager accepts: two page groups, one for the main data
// file and one for the shadow file, i.e. 2 * KOREDB_PAGE_SIZE * PAGE_GROUP_SIZE.
const MIN_MAX_DB_SIZE = 8 * MB;

const makeTmpDir = () =>
  new Promise((resolve, reject) => {
    tmp.dir({ unsafeCleanup: true }, (err, dirPath, _) => {
      if (err) {
        return reject(err);
      }
      return resolve(dirPath);
    });
  });

// Every heavy workload below runs in its own subprocess. That gives each measurement a
// clean RSS baseline, and it keeps the mocha process itself free of a large write
// workload -- writing a dataset this large and then spawning from the same process
// crashes the native module, which would take the whole test run down with it.
const runInSubprocess = (code) =>
  new Promise((resolve) => {
    const child = spawn(process.argv[0], ["-e", code]);
    let stdout = "";
    let stderr = "";
    child.stdout.on("data", (data) => {
      stdout += data;
    });
    child.stderr.on("data", (data) => {
      stderr += data;
    });
    child.on("close", (exitCode) => {
      resolve({ exitCode, stdout, stderr });
    });
  });

const requireKoredb = () =>
  "const koredb = require(" + JSON.stringify(koredbPath) + ");\n";

const buildScanFixture = (dbPath) =>
  requireKoredb() +
  "(async () => {\n" +
  "  const db = new koredb.Database(" + JSON.stringify(dbPath) + ", 512 * 1024 * 1024);\n" +
  "  const conn = new koredb.Connection(db);\n" +
  "  await conn.query('CREATE NODE TABLE t(id INT64, s STRING, PRIMARY KEY(id));');\n" +
  "  await conn.query(\"UNWIND RANGE(1, " + SCAN_ROWS + ") AS x " +
  "CREATE (:t {id: x, s: '" + SCAN_PAD + "' + CAST(x AS STRING)});\");\n" +
  "  await conn.close();\n" +
  "  await db.close();\n" +
  "  process.exit(0);\n" +
  "})();\n";

// Scan the fixture with a given buffer pool size, sampling RSS throughout so we capture the
// peak reached during execution rather than whatever happens to be resident at the end.
const scanWithPool = (dbPath, poolBytes) =>
  requireKoredb() +
  "(async () => {\n" +
  "  const readRss = () => process.memoryUsage.rss();\n" +
  "  const base = readRss();\n" +
  "  let peak = base;\n" +
  "  const sampler = setInterval(() => { const r = readRss(); if (r > peak) peak = r; }, 10);\n" +
  "  sampler.unref();\n" +
  "  const db = new koredb.Database(" + JSON.stringify(dbPath) + ", " + poolBytes + ", true, true);\n" +
  "  const conn = new koredb.Connection(db);\n" +
  "  const result = await conn.query('MATCH (n:t) RETURN COUNT(n.s);');\n" +
  "  const row = await result.getNext();\n" +
  "  await result.close();\n" +
  "  await conn.close();\n" +
  "  await db.close();\n" +
  "  clearInterval(sampler);\n" +
  "  peak = Math.max(peak, readRss());\n" +
  "  console.log('RESULT_JSON:' + JSON.stringify(" +
  "{ base: base, peak: peak, delta: peak - base, count: row['COUNT(n.s)'] }));\n" +
  "  process.exit(0);\n" +
  "})();\n";

const parseResult = ({ exitCode, stdout, stderr }) => {
  assert.equal(exitCode, 0, "subprocess failed (exit " + exitCode + "): " + stderr);
  const line = stdout.split(/\r?\n/).find((l) => l.startsWith("RESULT_JSON:"));
  assert.exists(line, "subprocess produced no result: " + stdout + stderr);
  return JSON.parse(line.slice("RESULT_JSON:".length));
};

describe("Buffer pool size", function () {
  // Building the fixture and scanning it twice takes well over the suite default.
  this.timeout(300000);

  let smallPool;
  let largePool;

  before(async function () {
    const dir = await makeTmpDir();
    const dbPath = path.join(dir, "scan.kz");
    const build = await runInSubprocess(buildScanFixture(dbPath));
    assert.equal(
      build.exitCode,
      0,
      "failed to build the scan fixture: " + build.stderr
    );
    smallPool = parseResult(await runInSubprocess(scanWithPool(dbPath, SMALL_POOL)));
    largePool = parseResult(await runInSubprocess(scanWithPool(dbPath, LARGE_POOL)));
  });

  it("should complete a scan that does not fit in the buffer pool", function () {
    // The dataset is several times the small pool, so finishing the scan at all proves the
    // buffer manager evicts and re-reads pages instead of requiring the table to be
    // resident.
    assert.equal(smallPool.count, SCAN_ROWS);
    assert.equal(largePool.count, SCAN_ROWS);
  });

  it("should use less memory with a smaller buffer pool", function () {
    // The scan touches more data than the small pool can hold, so the small pool has to cap
    // resident memory below what the large pool -- which can cache the whole column --
    // ends up using.
    assert.isBelow(
      smallPool.delta,
      largePool.delta,
      "small pool (" + SMALL_POOL / MB + " MB) grew RSS by " +
        Math.round(smallPool.delta / MB) + " MB, large pool (" + LARGE_POOL / MB +
        " MB) grew it by " + Math.round(largePool.delta / MB) + " MB"
    );
  });

  it("should keep resident memory within a small multiple of the buffer pool size", function () {
    // Guards against the buffer pool limit silently becoming a no-op. The bound is loose on
    // purpose: besides the pool itself a scan allocates vectors, frame-group metadata and
    // the V8 heap, which measured ~2x the pool size. Pulling the whole dataset into memory
    // would land well above this.
    const bound = 3 * SMALL_POOL;
    assert.isBelow(
      smallPool.delta,
      bound,
      "scan with a " + SMALL_POOL / MB + " MB buffer pool grew RSS by " +
        Math.round(smallPool.delta / MB) + " MB, over the " + bound / MB + " MB bound"
    );
  });
});

describe("Max DB size", function () {
  this.timeout(120000);

  it("should reject a max DB size below the minimum", async function () {
    const testDb = new koredb.Database(
      "",
      1 << 28 /* 256MB */,
      true /* compression */,
      false /* readOnly */,
      MIN_MAX_DB_SIZE / 2
    );
    try {
      await testDb.init();
      assert.fail("No error thrown when the max DB size is below the minimum.");
    } catch (e) {
      assert.equal(
        e.message,
        "Buffer manager exception: The given max db size should be at least " +
          MIN_MAX_DB_SIZE + " bytes."
      );
    }
  });

  it("should reject a max DB size that is not a power of two", async function () {
    const testDb = new koredb.Database(
      "",
      1 << 28 /* 256MB */,
      true /* compression */,
      false /* readOnly */,
      MIN_MAX_DB_SIZE + MIN_MAX_DB_SIZE / 2 /* 12MB: above the minimum, not a power of 2 */
    );
    try {
      await testDb.init();
      assert.fail("No error thrown when the max DB size is not a power of two.");
    } catch (e) {
      assert.equal(
        e.message,
        "Buffer manager exception: The given max db size should be a power of 2."
      );
    }
  });

  it("should reject a buffer pool size below one page", async function () {
    const testDb = new koredb.Database("", 1024 /* under the 4096 byte page size */);
    try {
      await testDb.init();
      assert.fail("No error thrown when the buffer pool size is below one page.");
    } catch (e) {
      assert.equal(
        e.message,
        "Buffer manager exception: The given buffer pool size should be at least 4096 bytes."
      );
    }
  });

  it("should fail a write that grows the database past the max DB size", async function () {
    // Run in a subprocess: the write fails partway through and leaves a half-written
    // database behind, which we do not want anywhere near the shared mocha connection.
    const dir = await makeTmpDir();
    const dbPath = path.join(dir, "capped.kz");
    const code =
      requireKoredb() +
      "(async () => {\n" +
      "  const db = new koredb.Database(" + JSON.stringify(dbPath) +
      ", 64 * 1024 * 1024, true, false, " + MIN_MAX_DB_SIZE + ");\n" +
      "  const conn = new koredb.Connection(db);\n" +
      "  await conn.query('CREATE NODE TABLE t(id INT64, s STRING, PRIMARY KEY(id));');\n" +
      "  let message = null;\n" +
      "  try {\n" +
      "    await conn.query(\"UNWIND RANGE(1, 300000) AS x " +
      "CREATE (:t {id: x, s: '" + SCAN_PAD + "'});\");\n" +
      "  } catch (e) { message = e.message; }\n" +
      "  console.log('RESULT_JSON:' + JSON.stringify({ message: message }));\n" +
      "  process.exit(0);\n" +
      "})();\n";
    const result = parseResult(await runInSubprocess(code));
    assert.exists(
      result.message,
      "writing past the max DB size unexpectedly succeeded"
    );
    assert.include(result.message, "Buffer manager exception");
  });
});


describe("Query memory limit", function () {
  this.timeout(300000);

  // The result is ROWS INT64s, i.e. ~4 MB. Budgets are picked so the verdict does not
  // depend on per-thread overhead (which varies with core count): TIGHT is half the raw
  // result size so it has to truncate, LOOSE is 8x it so it has to fit. MID only has to
  // land somewhere in between.
  const ROWS = 500000;
  const TIGHT_LIMIT = 2 * MB;
  const MID_LIMIT = 8 * MB;
  const LOOSE_LIMIT = 32 * MB;
  const QUERY = "UNWIND RANGE(1, " + ROWS + ") AS x RETURN x;";

  // Measured in a subprocess against a private database, because the limit is enforced
  // against whatever the buffer pool can still hand out. On the database shared by the
  // rest of the suite the answer depends on every test that ran before it -- once that
  // pool is full, even a 32 MiB budget truncates to zero rows.
  const measureLimits = (dbPath, limits) =>
    requireKoredb() +
    "(async () => {\n" +
    "  const db = new koredb.Database(" + JSON.stringify(dbPath) + ", 256 * 1024 * 1024);\n" +
    "  const query = " + JSON.stringify(QUERY) + ";\n" +
    "  const run = async (limit) => {\n" +
    "    const conn = new koredb.Connection(db);\n" +
    "    await conn.init();\n" +
    "    if (limit !== null) { await conn.query('CALL query_memory_limit=' + limit + ';'); }\n" +
    "    const r = await conn.query(query);\n" +
    "    const out = { limit: limit, truncated: r.isTruncated(), " +
    "reason: r.getTruncationReason(), tuples: r.getNumTuples() };\n" +
    "    await r.close();\n" +
    "    await conn.close();\n" +
    "    return out;\n" +
    "  };\n" +
    "  const tiers = [];\n" +
    "  for (const limit of " + JSON.stringify(limits) + ") { tiers.push(await run(limit)); }\n" +
    "  await run(" + limits[0] + ");\n" +
    "  const unlimited = await run(null);\n" +
    "  await db.close();\n" +
    "  console.log('RESULT_JSON:' + JSON.stringify({ tiers: tiers, unlimited: unlimited }));\n" +
    "  process.exit(0);\n" +
    "})();\n";

  let tiers;
  let unlimited;

  before(async function () {
    const dir = await makeTmpDir();
    const dbPath = path.join(dir, "limits.kz");
    const measured = parseResult(
      await runInSubprocess(
        measureLimits(dbPath, [TIGHT_LIMIT, MID_LIMIT, LOOSE_LIMIT])
      )
    );
    tiers = measured.tiers;
    unlimited = measured.unlimited;
  });

  const describeTiers = () =>
    tiers.map((t) => t.limit / MB + " MiB -> " + t.tuples).join(", ");

  it("should truncate when the limit is below the result size", function () {
    // The tightest budget is half the raw result size, so it cannot fit no matter how the
    // per-thread overhead shakes out.
    const tight = tiers[0];
    assert.isTrue(tight.truncated, "tightest limit did not truncate: " + describeTiers());
    assert.equal(tight.reason, "memory_limit");
    assert.isBelow(tight.tuples, ROWS);
  });

  it("should let more of the result through as the limit grows", function () {
    // A larger budget must never return fewer rows...
    for (let i = 1; i < tiers.length; i++) {
      assert.isAtLeast(
        tiers[i].tuples,
        tiers[i - 1].tuples,
        "raising the limit returned fewer rows: " + describeTiers()
      );
    }
    // ...and somewhere along the way it must return strictly more, otherwise the limit is
    // not what is actually driving truncation.
    assert.isAbove(
      tiers[tiers.length - 1].tuples,
      tiers[0].tuples,
      "the limit had no effect on how much of the result came back: " + describeTiers()
    );
  });

  it("should return the full result when the limit is large enough", function () {
    const loose = tiers[tiers.length - 1];
    assert.isFalse(loose.truncated, "loosest limit truncated: " + describeTiers());
    assert.equal(loose.tuples, ROWS);
  });

  it("should leave other connections unaffected by one connection's limit", function () {
    // query_memory_limit is set through a connection, so a tight budget on one connection
    // must not carry over to the next connection opened on the same database.
    assert.isFalse(unlimited.truncated);
    assert.equal(unlimited.tuples, ROWS);
  });
});
