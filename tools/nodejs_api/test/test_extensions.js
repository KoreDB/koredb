const { assert } = require("chai");
const koredb = process.env.TEST_INSTALLED
  ? require("@koredb/koredb")
  : require("../build/index.js");

const BUILT_IN_EXTENSIONS = ["ALGO", "FTS", "JSON", "VECTOR"];
const SHOW_EXTENSIONS = "CALL SHOW_LOADED_EXTENSIONS() RETURN *";

for (const sync of [false, true]) {
  describe(`Built-in extensions (${sync ? "sync" : "async"} API)`, function () {
    let database;
    let connection;

    async function rows(statement) {
      const result = sync
        ? connection.querySync(statement)
        : await connection.query(statement);
      try {
        return sync ? result.getAllSync() : await result.getAll();
      } finally {
        result.close();
      }
    }

    async function createIndex() {
      await rows("CREATE NODE TABLE Document(id INT64 PRIMARY KEY, content STRING)");
      await rows("CREATE (:Document {id: 1, content: 'graph database search'})");
      await rows("CREATE (:Document {id: 2, content: 'fresh fruit salad'})");
      await rows("CALL CREATE_FTS_INDEX('Document', 'contentIdx', ['content'])");
    }

    async function assertSearchWorks() {
      const matches = await rows(
        "CALL QUERY_FTS_INDEX('Document', 'contentIdx', 'graph') " +
          "RETURN node.id AS id, score"
      );
      assert.lengthOf(matches, 1);
      assert.strictEqual(matches[0].id, 1);
      assert.isAbove(matches[0].score, 0);
    }

    beforeEach(function () {
      database = new koredb.Database(":memory:", 64 * 1024 * 1024);
      connection = new koredb.Connection(database, 2);
    });

    afterEach(async function () {
      if (sync) {
        connection.closeSync();
        database.closeSync();
      } else {
        await connection.close();
        await database.close();
      }
    });

    it("loads all four built-in extensions when opening a database", async function () {
      const extensions = await rows(SHOW_EXTENSIONS);
      for (const name of BUILT_IN_EXTENSIONS) {
        const matches = extensions.filter((ext) => ext["extension name"] === name);
        assert.lengthOf(matches, 1, `${name} must be loaded exactly once`);
        assert.strictEqual(matches[0]["extension source"], "STATIC LINK");
        assert.isEmpty(matches[0]["extension path"].trim());
      }
    });

    it("creates and searches an FTS index without INSTALL or LOAD", async function () {
      await createIndex();
      await assertSearchWorks();
    });

    it("skips repeated INSTALL and LOAD without contacting a repository", async function () {
      await createIndex();
      const original = await rows(SHOW_EXTENSIONS);
      for (let repeat = 0; repeat < 2; repeat++) {
        for (const name of BUILT_IN_EXTENSIONS) {
          // An inaccessible repository makes any accidental download fail.
          const installed = await rows(
            `INSTALL ${name.toLowerCase()} FROM 'http://127.0.0.1:1'`
          );
          assert.include(installed[0].result, "No need to INSTALL.");
          const loaded = await rows(`LOAD EXTENSION ${name}`);
          assert.include(loaded[0].result, "No need to LOAD.");
        }
      }
      assert.deepEqual(await rows(SHOW_EXTENSIONS), original);
      await assertSearchWorks();
    });

    it("skips INSTALL and LOAD in prepared statements", async function () {
      for (const statement of ["INSTALL FtS", "LOAD EXTENSION fTs", "LOAD FTS"]) {
        const prepared = sync
          ? connection.prepareSync(statement)
          : await connection.prepare(statement);
        assert.isTrue(prepared.isSuccess(), prepared.getErrorMessage());
        const result = sync
          ? connection.executeSync(prepared)
          : await connection.execute(prepared);
        try {
          const messages = sync ? result.getAllSync() : await result.getAll();
          assert.include(messages[0].result, "No need to");
        } finally {
          result.close();
        }
      }
    });

    it("continues a multi-statement query after INSTALL and LOAD", async function () {
      const statement = "INSTALL fts; LOAD EXTENSION fts; RETURN 42 AS value;";
      const results = sync
        ? connection.querySync(statement)
        : await connection.query(statement);
      try {
        assert.lengthOf(results, 3);
        const resultRows = [];
        for (const result of results) {
          resultRows.push(sync ? result.getAllSync() : await result.getAll());
        }
        assert.include(resultRows[0][0].result, "No need to INSTALL.");
        assert.include(resultRows[1][0].result, "No need to LOAD.");
        assert.deepEqual(resultRows[2], [{ value: 42 }]);
      } finally {
        for (const result of results) {
          result.close();
        }
      }
    });
  });
}
