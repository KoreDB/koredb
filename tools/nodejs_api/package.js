/**
 * Builds the npm artifacts for the Node.js API.
 *
 * KoreDB is distributed the way esbuild and rollup distribute their native
 * code: a small, dependency-free main package (`koredb`) that declares one
 * `optionalDependency` per platform/architecture, plus one tiny package per
 * target holding nothing but the prebuilt addon. npm, yarn and pnpm honour the
 * `os`/`cpu` fields of those packages and download only the one matching the
 * machine being installed on.
 *
 * Running `node package` produces `dist/`:
 *
 *   dist/koredb-<version>.tgz                        <- the main package
 *   dist/koredb-koredb-<platform>-<arch>-<ver>.tgz   <- one per prebuilt binary
 *   dist/packages.json                               <- manifest used by CI
 *
 * Platform packages are emitted only for the binaries present in `prebuilt/`,
 * and the main package declares exactly those as optional dependencies, so a
 * partial build never advertises a package that was not produced.
 */

const childProcess = require("child_process");
const fs = require("fs/promises");
const fsSync = require("fs");
const path = require("path");

const CURRENT_DIR = __dirname;
const KOREDB_ROOT = path.resolve(CURRENT_DIR, "..", "..");
const SRC_JS_DIR = path.join(CURRENT_DIR, "src_js");
const PREBUILT_DIR = path.join(CURRENT_DIR, "prebuilt");
const DIST_DIR = path.join(CURRENT_DIR, "dist");
const STAGING_DIR = path.join(DIST_DIR, "staging");
const KOREDB_VERSION_TEXT = "KoreDB VERSION";
const ADDON_FILE_NAME = "koredbjs.node";
const NPM = process.platform === "win32" ? "npm.cmd" : "npm";

const TARGETS = [
  { platform: "darwin", arch: "arm64" },
  { platform: "darwin", arch: "x64" },
  { platform: "linux", arch: "arm64" },
  { platform: "linux", arch: "x64" },
  { platform: "win32", arch: "arm64" },
  { platform: "win32", arch: "x64" },
];

const platformPackageName = (target) =>
  `@koredb/koredb-${target.platform}-${target.arch}`;

const readVersion = async () => {
  const cmakeLists = await fs.readFile(
    path.join(KOREDB_ROOT, "CMakeLists.txt"),
    { encoding: "utf-8" }
  );
  for (const line of cmakeLists.split("\n")) {
    if (!line.includes(KOREDB_VERSION_TEXT)) {
      continue;
    }
    const parts = line.split(" ")[2].trim().split(".");
    let version = parts.slice(0, 3).join(".");
    if (parts.length >= 4) {
      version += "-dev." + parts.slice(3).join(".");
    }
    return version;
  }
  throw new Error(`Could not find "${KOREDB_VERSION_TEXT}" in CMakeLists.txt.`);
};

const npmPack = async (packageDir) => {
  const stdout = childProcess.execFileSync(NPM, ["pack", "--loglevel", "warn"], {
    cwd: packageDir,
    encoding: "utf-8",
    // Node refuses to spawn `.cmd` files without a shell on Windows. Packing
    // into the package directory and moving the result afterwards keeps every
    // argument free of paths, so shell quoting never comes into play.
    shell: process.platform === "win32",
  });
  // `npm pack` prints the tarball name on the last non-empty line of stdout.
  const lines = stdout
    .split("\n")
    .map((line) => line.trim())
    .filter(Boolean);
  const tarball = lines[lines.length - 1];
  await fs.rename(
    path.join(packageDir, tarball),
    path.join(DIST_DIR, tarball)
  );
  return tarball;
};

const writeJson = (filePath, value) =>
  fs.writeFile(filePath, JSON.stringify(value, null, 2) + "\n");

(async () => {
  const version = await readVersion();
  console.log(`KoreDB version: ${version}`);

  const sourcePackageJson = JSON.parse(
    await fs.readFile(path.join(CURRENT_DIR, "package.json"), {
      encoding: "utf-8",
    })
  );
  const { description, license, author, homepage, repository, bugs, engines } =
    sourcePackageJson;
  const sharedManifestFields = {
    license,
    author,
    homepage,
    repository,
    bugs,
    engines,
  };

  await fs.rm(DIST_DIR, { recursive: true, force: true });
  await fs.mkdir(STAGING_DIR, { recursive: true });

  // -------------------------------------------------------- platform packages --
  const availableTargets = [];
  const tarballs = [];

  for (const target of TARGETS) {
    const name = `${target.platform}-${target.arch}`;
    const prebuiltPath = path.join(PREBUILT_DIR, `koredbjs-${name}.node`);
    if (!fsSync.existsSync(prebuiltPath)) {
      console.warn(`  ! no prebuilt binary for ${name}, skipping its package`);
      continue;
    }

    const packageName = platformPackageName(target);
    const packageDir = path.join(STAGING_DIR, `koredb-${name}`);
    await fs.mkdir(packageDir, { recursive: true });

    await writeJson(path.join(packageDir, "package.json"), {
      name: packageName,
      version,
      description: `Prebuilt KoreDB Node.js addon for ${name}.`,
      ...sharedManifestFields,
      os: [target.platform],
      cpu: [target.arch],
      // Yarn Plug-n-Play must not zip this package: a native addon has to exist
      // as a real file on disk for dlopen() to be able to load it.
      preferUnplugged: true,
      files: [ADDON_FILE_NAME],
    });

    await fs.copyFile(prebuiltPath, path.join(packageDir, ADDON_FILE_NAME));
    await fs.copyFile(
      path.join(KOREDB_ROOT, "LICENSE"),
      path.join(packageDir, "LICENSE")
    );
    await fs.writeFile(
      path.join(packageDir, "README.md"),
      `# ${packageName}\n\n` +
        `This package holds the prebuilt KoreDB Node.js addon for \`${name}\`.\n\n` +
        "It is an implementation detail of the " +
        "[@koredb/koredb](https://www.npmjs.com/package/@koredb/koredb) package " +
        "and is " +
        "installed automatically as an optional dependency. Do not depend on " +
        "it directly.\n"
    );

    const tarball = await npmPack(packageDir);
    console.log(`  + ${packageName} -> ${tarball}`);
    availableTargets.push({ ...target, packageName, tarball });
    tarballs.push(tarball);
  }

  if (availableTargets.length === 0) {
    console.warn(
      "No prebuilt binaries were found; the main package will declare no " +
        "optional dependencies and will not work without a local source build."
    );
  } else if (availableTargets.length !== TARGETS.length) {
    console.warn(
      `Only ${availableTargets.length} of ${TARGETS.length} platform packages were built.`
    );
  }

  // ------------------------------------------------------------ main package --
  const mainDir = path.join(STAGING_DIR, "koredb");
  await fs.mkdir(mainDir, { recursive: true });

  const jsFiles = (await fs.readdir(SRC_JS_DIR)).filter(
    (file) =>
      file.endsWith(".js") || file.endsWith(".mjs") || file.endsWith(".d.ts")
  );
  for (const file of jsFiles) {
    await fs.copyFile(path.join(SRC_JS_DIR, file), path.join(mainDir, file));
  }
  await fs.copyFile(
    path.join(KOREDB_ROOT, "LICENSE"),
    path.join(mainDir, "LICENSE")
  );
  await fs.copyFile(
    path.join(CURRENT_DIR, "README.md"),
    path.join(mainDir, "README.md")
  );

  const optionalDependencies = {};
  const sortedTargets = availableTargets
    .slice()
    .sort((a, b) => a.packageName.localeCompare(b.packageName));
  for (const target of sortedTargets) {
    // Pin exactly: a platform package is only ever compatible with the main
    // package it was built alongside.
    optionalDependencies[target.packageName] = version;
  }

  await writeJson(path.join(mainDir, "package.json"), {
    name: sourcePackageJson.name,
    version,
    description,
    keywords: [
      "graph",
      "database",
      "cypher",
      "embedded",
      "graph-database",
      "koredb",
    ],
    ...sharedManifestFields,
    main: sourcePackageJson.main,
    module: sourcePackageJson.module,
    types: sourcePackageJson.types,
    exports: sourcePackageJson.exports,
    type: sourcePackageJson.type,
    files: ["*.js", "*.mjs", "*.d.ts"],
    optionalDependencies,
  });

  const mainTarball = await npmPack(mainDir);
  console.log(`  + ${sourcePackageJson.name} -> ${mainTarball}`);
  tarballs.push(mainTarball);

  // The manifest lets CI publish in the right order (platform packages first,
  // main package last) and lets the Electron test find the tarballs it needs.
  await writeJson(path.join(DIST_DIR, "packages.json"), {
    version,
    main: { packageName: sourcePackageJson.name, tarball: mainTarball },
    platforms: sortedTargets.map(({ platform, arch, packageName, tarball }) => ({
      platform,
      arch,
      packageName,
      tarball,
    })),
  });

  await fs.rm(STAGING_DIR, { recursive: true, force: true });

  console.log(`\nWrote ${tarballs.length} tarball(s) to ${DIST_DIR}`);
})().catch((error) => {
  console.error(error);
  process.exit(1);
});
