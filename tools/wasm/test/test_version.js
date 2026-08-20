const { assert } = require("chai");

describe("Get version", function () {
  it("should get the version of the library", async function () {
    const version = await koredb.getVersion();
    assert.isString(version);
    assert.notEqual(version, "");
  });

  it("should get the storage version of the library", async function () {
    const storageVersion = await koredb.getStorageVersion();
    assert.isTrue(storageVersion > 0);
  });
});
