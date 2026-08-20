const { assert } = require("chai");

describe("Get version", function () {
  it("should get the version of the library", function () {
    assert.isString(koredb.VERSION);
    assert.notEqual(koredb.VERSION, "");
  });

  it("should get the storage version of the library", function () {
    assert.isNumber(koredb.STORAGE_VERSION);
    assert.isAtLeast(koredb.STORAGE_VERSION, 1);
  });
});
