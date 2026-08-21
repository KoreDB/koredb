// Electron shutdown probe: start, optionally load KoreDB, then exit.
//
// The KoreDB smoke test does its work correctly and then fails to let Electron
// exit. On its own that result is ambiguous, so this file brackets it:
//
//   (no flag)      bare Electron, no KoreDB at all. If this cannot start and
//                  exit, the CI environment is at fault, not the addon.
//   --load-addon   require the addon and exit without touching a database.
//                  Separates "loading koredbjs.node blocks shutdown" from
//                  "the Database lifecycle blocks shutdown" - very different
//                  bugs, and the log otherwise looks identical.
//
// Kept as .js on purpose: it must not depend on the TypeScript build, so that
// it still runs when compiling the smoke test is what broke.
const { app } = require("electron");

const loadAddon = process.argv.includes("--load-addon");

app.disableHardwareAcceleration();

app.whenReady().then(
  () => {
    try {
      if (loadAddon) {
        const koredb = require("@koredb/koredb");
        console.log(`probe: addon loaded, KoreDB ${koredb.VERSION}`);
      }
      console.log(
        `probe: Electron reached ready (addon ${loadAddon ? "loaded" : "not loaded"}), exiting`
      );
      app.exit(0);
    } catch (error) {
      console.error("probe: failed before reaching exit");
      console.error(error);
      app.exit(1);
    }
  },
  (error) => {
    console.error("probe: Electron failed to start");
    console.error(error);
    process.exit(1);
  }
);
