/**
 * Publishes the npm artifacts produced by `node package`.
 *
 * Ordering matters: the main package pins every platform package as an exact
 * optional dependency, so publishing it first would open a window in which
 * `npm install @koredb/koredb` resolves the main package but cannot fetch
 * its addon.
 * Platform packages therefore go out first, the main package last.
 *
 * Usage:
 *   node publish.js --tag latest
 *   node publish.js --tag next
 *   node publish.js --tag latest --dry-run
 */

const childProcess = require("child_process");
const fs = require("fs");
const path = require("path");

const DIST_DIR = path.join(__dirname, "dist");
const MANIFEST_PATH = path.join(DIST_DIR, "packages.json");
const NPM = process.platform === "win32" ? "npm.cmd" : "npm";

const parseArgs = (argv) => {
  const options = { tag: null, dryRun: false };
  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    if (arg === "--dry-run") {
      options.dryRun = true;
    } else if (arg === "--tag") {
      options.tag = argv[++i];
    } else if (arg.startsWith("--tag=")) {
      options.tag = arg.slice("--tag=".length);
    } else {
      throw new Error(`Unknown argument: ${arg}`);
    }
  }
  if (!options.tag) {
    throw new Error("Missing required --tag <dist-tag> (e.g. latest or next).");
  }
  return options;
};

const publish = (tarballPath, { tag, dryRun }) => {
  const args = [
    "publish",
    tarballPath,
    // Scoped packages are private by default; every KoreDB package is public.
    "--access",
    "public",
    "--tag",
    tag,
  ];
  if (dryRun) {
    args.push("--dry-run");
  }
  childProcess.execFileSync(NPM, args, {
    cwd: __dirname,
    stdio: "inherit",
    shell: process.platform === "win32",
  });
};

const main = () => {
  const options = parseArgs(process.argv.slice(2));

  if (!fs.existsSync(MANIFEST_PATH)) {
    throw new Error(
      `No ${MANIFEST_PATH}. Run \`node package\` before publishing.`
    );
  }
  const manifest = JSON.parse(fs.readFileSync(MANIFEST_PATH, "utf-8"));

  if (manifest.platforms.length === 0) {
    throw new Error(
      "The packaged build contains no platform packages; refusing to publish " +
        "a main package that has no addon to install."
    );
  }

  const ordered = [
    ...manifest.platforms.map((entry) => ({
      packageName: entry.packageName,
      tarball: entry.tarball,
    })),
    manifest.main,
  ];

  console.log(
    `Publishing ${ordered.length} package(s) at version ${manifest.version} ` +
      `under the '${options.tag}' tag${options.dryRun ? " (dry run)" : ""}.`
  );

  for (const entry of ordered) {
    const tarballPath = path.join(DIST_DIR, entry.tarball);
    if (!fs.existsSync(tarballPath)) {
      throw new Error(`Missing tarball for ${entry.packageName}: ${tarballPath}`);
    }
    console.log(`\n==> ${entry.packageName}@${manifest.version}`);
    publish(tarballPath, options);
  }

  console.log("\nDone.");
};

try {
  main();
} catch (error) {
  console.error(error.message || error);
  process.exit(1);
}
