/**
 * This file is a customized loader for the koredbjs.node native module.
 * It is used to load the native module with the correct flags on Linux so that
 * extension loading works correctly.
 * @module koredb_native
 * @private
 */

const process = require("process");
const constants = require("constants");
const join = require("path").join;

const koredbNativeModule = { exports: {} };
const modulePath = join(__dirname, "koredbjs.node");
if (process.platform === "linux") {
  process.dlopen(
    koredbNativeModule,
    modulePath,
    constants.RTLD_LAZY | constants.RTLD_GLOBAL
  );
} else {
  process.dlopen(koredbNativeModule, modulePath);
}

module.exports = koredbNativeModule.exports;
