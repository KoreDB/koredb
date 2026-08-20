/**
 * @file koredb.js is the internal wrapper for the WebAssembly module.
 */
const koredb_wasm = require("../koredb/koredb_wasm.js");

class koredb {
  constructor() {
    this._koredb = null;
  }

  async init() {
    this._koredb = await koredb_wasm();
  }

  checkInit() {
    if (!this._koredb) {
      throw new Error("The WebAssembly module is not initialized.");
    }
  }

  getVersion() {
    this.checkInit();
    return this._koredb.getVersion();
  }

  getStorageVersion() {
    this.checkInit();
    return this._koredb.getStorageVersion();
  }

  getFS() {
    this.checkInit();
    return this._koredb.FS;
  }

  getWasmMemory() {
    this.checkInit();
    return this._koredb.wasmMemory;
  }
}

const koredbInstance = new koredb();
module.exports = koredbInstance;
