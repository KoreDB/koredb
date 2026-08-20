// Electron main-process smoke test for the KoreDB Node.js addon.
//
// The addon is built against Node-API (NAPI_VERSION=6), which is ABI-stable
// across both Node.js and Electron runtimes. This test loads the *published*
// package (installed from the release tarball) inside a real Electron main
// process and runs a small end-to-end workload, so that a regression in
// Electron compatibility fails the release pipeline instead of user apps.

import { app } from "electron";
import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

import koredb, { QueryResult } from "@koredb/koredb";

const TABLE = "Person";

// This smoke test never opens a window, so the GPU process and the sandbox
// helper are pure liability on headless CI machines (the SUID sandbox is not
// usable under the restricted user namespaces of modern CI images).
app.commandLine.appendSwitch("no-sandbox");
app.disableHardwareAcceleration();

async function runSmokeTest(): Promise<void> {
  const workDir = mkdtempSync(join(tmpdir(), "koredb-electron-"));
  const dbPath = join(workDir, "smoke.kz");

  console.log(`KoreDB version: ${koredb.VERSION}`);
  console.log(`KoreDB storage version: ${koredb.STORAGE_VERSION}`);
  console.log(`Database path: ${dbPath}`);

  const database = new koredb.Database(dbPath);
  const connection = new koredb.Connection(database);

  try {
    await connection.query(
      `CREATE NODE TABLE ${TABLE}(name STRING, age INT64, PRIMARY KEY(name))`
    );
    await connection.query(
      `CREATE (:${TABLE} {name: 'Alice', age: 30}), (:${TABLE} {name: 'Bob', age: 41})`
    );

    const result = (await connection.query(
      `MATCH (p:${TABLE}) RETURN p.name AS name, p.age AS age ORDER BY p.name`
    )) as QueryResult;
    const rows = await result.getAll();
    result.close();

    console.log(`Query returned ${rows.length} row(s): ${JSON.stringify(rows)}`);

    if (rows.length !== 2) {
      throw new Error(`Expected 2 rows, got ${rows.length}`);
    }
    if (rows[0].name !== "Alice" || rows[1].name !== "Bob") {
      throw new Error(`Unexpected rows: ${JSON.stringify(rows)}`);
    }
  } finally {
    await connection.close();
    await database.close();
    rmSync(workDir, { recursive: true, force: true });
  }
}

// Electron initialises its GUI subsystem before the main-process script may use
// most APIs; the addon itself only needs a running Node context.
app.whenReady().then(
  async () => {
    try {
      await runSmokeTest();
      console.log("Electron smoke test PASSED");
      app.exit(0);
    } catch (error) {
      console.error("Electron smoke test FAILED");
      console.error(error);
      app.exit(1);
    }
  },
  (error: unknown) => {
    console.error("Electron failed to start");
    console.error(error);
    process.exit(1);
  }
);
