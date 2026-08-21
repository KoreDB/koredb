// Bare Electron main process: no KoreDB, no window, no work. Start, then exit.
//
// This exists to make the KoreDB smoke test's result interpretable. Electron on
// headless CI can fail to shut down for reasons that have nothing to do with a
// native addon (no D-Bus session, no X server, a GPU process that will not go
// away). When that happens the smoke test fails during teardown and looks
// exactly like an addon bug. Running this first separates the two: if the
// baseline cannot start and exit cleanly, the environment is at fault.
//
// Kept as .js on purpose - it must not depend on the TypeScript build, so that
// it still runs when compiling the smoke test is what broke.
const { app } = require("electron");

app.disableHardwareAcceleration();

app.whenReady().then(
  () => {
    console.log("baseline: Electron reached ready, exiting");
    app.exit(0);
  },
  (error) => {
    console.error("baseline: Electron failed to start");
    console.error(error);
    process.exit(1);
  }
);
