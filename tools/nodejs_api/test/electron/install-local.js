/**
 * Installs a locally packaged KoreDB build into this Electron test app.
 *
 * KoreDB publishes a small main package plus one package per platform holding
 * the prebuilt addon. When this test runs the platform packages are not on the
 * registry yet, so both tarballs are installed straight out of the `dist`
 * directory produced by `node package`. npm records the platform package as a
 * direct dependency, which satisfies the main package's optional dependency on
 * it without any registry lookup.
 *
 * Usage: node install-local.js <dist-dir>
 */

const childProcess = require("child_process");
const fs = require("fs");
const path = require("path");

const distDir = path.resolve(process.argv[2] || "");
const manifestPath = path.join(distDir, "packages.json");

if (!fs.existsSync(manifestPath)) {
  console.error(
    `No packages.json found in ${distDir}. Run \`node package\` in ` +
      "tools/nodejs_api first, or point this script at the downloaded artifact."
  );
  process.exit(1);
}

const manifest = JSON.parse(fs.readFileSync(manifestPath, "utf-8"));
const target = `${process.platform}-${process.arch}`;
const platformEntry = manifest.platforms.find(
  (entry) => entry.platform === process.platform && entry.arch === process.arch
);

if (!platformEntry) {
  const available =
    manifest.platforms
      .map((entry) => `${entry.platform}-${entry.arch}`)
      .join(", ") || "(none)";
  console.error(
    `The packaged build has no prebuilt binary for ${target}.\n` +
      `Available targets: ${available}.`
  );
  process.exit(1);
}

const tarballs = [
  path.join(distDir, manifest.main.tarball),
  path.join(distDir, platformEntry.tarball),
];

for (const tarball of tarballs) {
  if (!fs.existsSync(tarball)) {
    console.error(`Missing tarball: ${tarball}`);
    process.exit(1);
  }
}

console.log(
  `Installing ${manifest.main.packageName} and ${platformEntry.packageName} ` +
    `at ${manifest.version} for ${target}.`
);

const npm = process.platform === "win32" ? "npm.cmd" : "npm";
// Node refuses to spawn `.cmd` files without a shell on Windows, and a shell
// re-parses the arguments, so quote the paths there.
const useShell = process.platform === "win32";
const args = ["install", ...tarballs.map((t) => (useShell ? `"${t}"` : t))];

childProcess.execFileSync(npm, args, {
  cwd: __dirname,
  stdio: "inherit",
  shell: useShell,
});
