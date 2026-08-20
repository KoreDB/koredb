/**
 * @file index.js is the root file for the synchronous version of KoreDB
 * WebAssembly module. It exports the module's public interface.
 */
"use strict";

const KoreDBWasm = require("./koredb.js");
const Database = require("./database.js");
const Connection = require("./connection.js");
const PreparedStatement = require("./prepared_statement.js");
const QueryResult = require("./query_result.js");

/**
 * The synchronous version of KoreDB WebAssembly module.
 * @module koredb-wasm
 */
module.exports = {
  /**
   * Initialize the KoreDB WebAssembly module.
   * @memberof module:koredb-wasm
   * @returns {Promise<void>} a promise that resolves when the module is 
   * initialized. The promise is rejected if the module fails to initialize.
   */
  init: () => {
    return KoreDBWasm.init();
  },

  /**
   * Get the version of the KoreDB WebAssembly module.
   * @memberof module:koredb-wasm
   * @returns {String} the version of the KoreDB WebAssembly module.
   */
  getVersion: () => {
    return KoreDBWasm.getVersion();
  },

  /**
   * Get the storage version of the KoreDB WebAssembly module.
   * @memberof module:koredb-wasm
   * @returns {BigInt} the storage version of the KoreDB WebAssembly module.
   */
  getStorageVersion: () => {
    return KoreDBWasm.getStorageVersion();
  },
  
  /**
   * Get the standard emscripten filesystem module (FS). Please refer to the 
   * emscripten documentation for more information.
   * @memberof module:koredb-wasm
   * @returns {Object} the standard emscripten filesystem module (FS).
   */
  getFS: () => {
    return KoreDBWasm.getFS();
  },

  /**
   * Get the WebAssembly memory. Please refer to the emscripten documentation 
   * for more information.
   * @memberof module:koredb-wasm
   * @returns {Object} the WebAssembly memory object.
   */
  getWasmMemory: () => {
    return KoreDBWasm.getWasmMemory();
  },

  Database,
  Connection,
  PreparedStatement,
  QueryResult,
};
