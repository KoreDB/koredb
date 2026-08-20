/**
 * This file is a customized loader for the koredbjs.node native module.
 *
 * The addon ships as one npm package per platform/architecture (declared in the
 * `optionalDependencies` of `@koredb/koredb`), so that installing it downloads a
 * single binary rather than one for every supported target. The loader resolves
 * the package matching the running platform and dlopen()s the addon out of it.
 *
 * A binary sitting next to this file always wins: that is what a local source
 * build (`make nodejs`) produces, and what vendored or `npm link`ed installs
 * rely on.
 *
 * @module koredb_native
 * @private
 */

const process = require("process");
const constants = require("constants");
const fs = require("fs");
const path = require("path");

const ADDON_FILE_NAME = "koredbjs.node";

// Spelled out as a literal map rather than built from a template string so that
// the package names stay greppable and visible to bundlers and audit tooling.
const PLATFORM_PACKAGES = {
  "darwin-arm64": "@koredb/koredb-darwin-arm64",
  "darwin-x64": "@koredb/koredb-darwin-x64",
  "linux-arm64": "@koredb/koredb-linux-arm64",
  "linux-x64": "@koredb/koredb-linux-x64",
  "win32-arm64": "@koredb/koredb-win32-arm64",
  "win32-x64": "@koredb/koredb-win32-x64",
};

const target = `${process.platform}-${process.arch}`;
const platformPackage = PLATFORM_PACKAGES[target];

const resolvePlatformPackageAddon = () => {
  if (!platformPackage) {
    return null;
  }
  try {
    // Resolve the manifest rather than the package entry point: a platform
    // package contains nothing but the addon, so it has no JavaScript main.
    const manifestPath = require.resolve(`${platformPackage}/package.json`);
    const addonPath = path.join(path.dirname(manifestPath), ADDON_FILE_NAME);
    return fs.existsSync(addonPath) ? addonPath : null;
  } catch (e) {
    return null;
  }
};

const buildNotFoundError = () => {
  const supported = Object.keys(PLATFORM_PACKAGES).sort().join(", ");
  const buildFromSource =
    "  git clone https://github.com/KoreDB/koredb.git\n" +
    "  cd koredb && make nodejs\n" +
    "  npm link tools/nodejs_api/build";
  if (!platformPackage) {
    return new Error(
      `KoreDB does not ship a prebuilt binary for '${target}'.\n` +
        `Supported platforms: ${supported}.\n` +
        "To use KoreDB here, build it from source and link the result:\n" +
        buildFromSource
    );
  }
  return new Error(
    `Could not find the KoreDB native addon for '${target}'.\n` +
      `The optional dependency '${platformPackage}' is not installed.\n` +
      "This usually means one of:\n" +
      "  - the install ran with --no-optional or --omit=optional;\n" +
      "  - a stale package-lock.json is missing the optional dependency\n" +
      "    (remove node_modules and package-lock.json, then reinstall);\n" +
      "  - the tree was installed for a different target, in which case install\n" +
      `    the binary explicitly: npm install ${platformPackage} --os=${process.platform} --cpu=${process.arch}`
  );
};

const localAddonPath = path.join(__dirname, ADDON_FILE_NAME);
const modulePath = fs.existsSync(localAddonPath)
  ? localAddonPath
  : resolvePlatformPackageAddon();

if (!modulePath) {
  throw buildNotFoundError();
}

const koredbNativeModule = { exports: {} };
try {
  if (process.platform === "linux") {
    // RTLD_GLOBAL is required so that dynamically loaded KoreDB extensions can
    // resolve symbols back into the addon.
    process.dlopen(
      koredbNativeModule,
      modulePath,
      constants.RTLD_LAZY | constants.RTLD_GLOBAL
    );
  } else {
    process.dlopen(koredbNativeModule, modulePath);
  }
} catch (e) {
  const message = String((e && e.message) || "");
  if (process.platform === "linux" && /musl|GLIBC/i.test(message)) {
    // The Linux binaries are built inside manylinux_2_28, so they need glibc.
    throw new Error(
      `Failed to load the KoreDB native addon from ${modulePath}.\n` +
        "The prebuilt Linux binaries are built against glibc (2.28 and newer) " +
        "and cannot run on musl-based distributions such as Alpine.\n" +
        "Build KoreDB from source on this system instead:\n" +
        "  git clone https://github.com/KoreDB/koredb.git\n" +
        "  cd koredb && make nodejs\n" +
        "  npm link tools/nodejs_api/build\n" +
        `Original error: ${message}`
    );
  }
  throw e;
}

module.exports = koredbNativeModule.exports;
