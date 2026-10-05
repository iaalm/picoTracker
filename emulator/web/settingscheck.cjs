// Settings sweep for the web build.
//
// Earlier scripts pressed buttons and checked "did it crash". That is weak:
// if navigation silently fails, the test passes without ever reaching the
// screen it claims to test -- which is exactly what happened while chasing
// the GetVariable() trap, where several "clean" runs never left the song
// screen.
//
// So this drives the UI against the character grid the firmware actually
// rendered (pt_get_screen_text / pt_get_cursor_row). Every navigation step
// asserts where it landed, and every field is exercised by its real key
// path, derived from the view's ProcessButtonMask:
//
//   arrows        move focus / change value
//   ENTER+arrow   per-view shortcuts (table alloc, instrument tweaks)
//   EDIT+arrow    alternate edit action
//   EDIT+ENTER    clear / reset
//
// A wasm trap aborts the run, so an uncaught RuntimeError fails the suite
// with the step that produced it.
//
// Needs a bundled project that has instruments: the chain/phrase/instrument
// screens are unreachable from an empty song, so against the default
// `.untitled` this stops early and says so rather than reporting a pass it
// did not earn. Build with
//   -DPT_WEB_SDROOT=<root whose .current names a project with samples>
const puppeteer = require("puppeteer-core");

const URL = process.env.PT_URL || "http://127.0.0.1:8731/";
const EXE =
  "/home/simon/Projects/picoTracker/.browser/chrome-headless-shell-linux64/chrome-headless-shell";

const BITS = {
  left: 1 << 0, down: 1 << 1, right: 1 << 2, up: 1 << 3,
  alt: 1 << 4, edit: 1 << 5, enter: 1 << 6, nav: 1 << 7, play: 1 << 8,
};

(async () => {
  const browser = await puppeteer.launch({
    executablePath: EXE,
    headless: true,
    args: [
      "--no-sandbox",
      "--use-gl=swiftshader",
      "--enable-unsafe-swiftshader",
      "--autoplay-policy=no-user-gesture-required",
    ],
  });
  const page = await browser.newPage();
  await page.setViewport({ width: 420, height: 860, isMobile: true,
                           hasTouch: true, deviceScaleFactor: 2 });

  const errors = [];
  page.on("pageerror", (e) => errors.push("pageerror: " + e.message));
  page.on("console", (m) => {
    const t = m.text();
    if (/RuntimeError|out of bounds|call_indirect|abort\(/i.test(t)) {
      errors.push("console: " + t);
    }
  });

  await page.goto(URL, { waitUntil: "domcontentloaded", timeout: 60000 });
  await page.waitForSelector("#overlay", { timeout: 30000 });
  await page.click("#overlay");
  await new Promise((r) => setTimeout(r, 8000));

  // --- driving the device --------------------------------------------
  // Masks go straight to pt_set_touch_mask rather than through synthetic
  // touches: this suite is about firmware paths, and the pad's own input
  // handling is covered by touchcheck.cjs.
  const setMask = (m) =>
    page.evaluate((mask) => Module._pt_set_touch_mask(mask), m);

  const press = async (names, holdMs) => {
    let m = 0;
    for (const n of names) m |= BITS[n];
    await setMask(m);
    await new Promise((r) => setTimeout(r, holdMs || 130));
    await setMask(0);
    await new Promise((r) => setTimeout(r, 190));
  };

  const screen = () =>
    page.evaluate(() => {
      const cap = (32 + 1) * 24 + 1;
      const buf = Module._malloc(cap);
      Module._pt_get_screen_text(buf, cap);
      const s = Module.UTF8ToString(buf);
      Module._free(buf);
      return { text: s, row: Module._pt_get_cursor_row() };
    });

  const titleOf = (text) => text.split("\n")[0].trim();

  let failed = null;
  const check = async (label) => {
    if (failed) return true;
    if (errors.length) {
      failed = { label, error: errors[0] };
      return true;
    }
    // A dead page throws here rather than returning a screen.
    try {
      await screen();
    } catch (e) {
      failed = { label, error: "page unresponsive: " + e.message };
      return true;
    }
    return false;
  };

  const log = [];
  const step = async (label, fn) => {
    if (failed) return;
    try {
      await fn();
    } catch (e) {
      failed = { label, error: "threw: " + e.message };
      return;
    }
    if (await check(label)) return;
    const s = await screen();
    log.push(`  ok  ${label.padEnd(42)} [${titleOf(s.text)}] row=${s.row}`);
  };

  // Navigate until the screen title matches, so a failed hop is reported
  // where it happens instead of silently testing the wrong screen.
  const gotoScreen = async (want, keys, tries) => {
    for (let i = 0; i < (tries || 4); i++) {
      const s = await screen();
      if (new RegExp(want, "i").test(titleOf(s.text))) return true;
      await press(keys);
      if (errors.length) return false;
    }
    const s = await screen();
    return new RegExp(want, "i").test(titleOf(s.text));
  };

  // How each screen gets back towards song. Read off the views'
  // ProcessButtonMask rather than guessed -- the axis differs per screen:
  //   chain/phrase/instrument  NAV+LEFT   (horizontal chain)
  //   table                    NAV+UP     -> phrase
  //   mixer                    NAV+UP     -> song
  //   device/project           NAV+DOWN   (vertical chain)
  const BACK_KEY = [
    [/^chain/i, "left"],
    [/^phrase/i, "left"],
    [/^instrument/i, "left"],
    [/^table/i, "up"],
    [/^mixer/i, "up"],
    [/^device/i, "down"],
    [/^project/i, "down"],
  ];

  const backToSong = async () => {
    for (let i = 0; i < 12; i++) {
      const title = titleOf((await screen()).text);
      if (/^song/i.test(title)) return true;
      const hit = BACK_KEY.find(([re]) => re.test(title));
      if (!hit) throw new Error(`no route home from screen "${title}"`);
      await press(["nav", hit[1]]);
      if (errors.length) return false;
    }
    return /^song/i.test(titleOf((await screen()).text));
  };

  // --- the sweep -------------------------------------------------------
  const start = await screen();
  console.log(`start screen: [${titleOf(start.text)}]`);

  // Project screen: NAV+UP from song.
  await step("nav to project screen", async () => {
    const ok = await gotoScreen("project|device", ["nav", "up"], 3);
    if (!ok) throw new Error("never reached project/device screen");
  });

  // Walk every field on the screen and poke it with each edit gesture.
  const sweepFields = async (name, count) => {
    for (let i = 0; i < count; i++) {
      await step(`${name}: field ${i} value up`, () => press(["up"]));
      await step(`${name}: field ${i} value down`, () => press(["down"]));
      await step(`${name}: field ${i} right`, () => press(["right"]));
      await step(`${name}: field ${i} left`, () => press(["left"]));
      await step(`${name}: field ${i} ENTER+up`, () => press(["enter", "up"]));
      await step(`${name}: field ${i} ENTER+down`, () => press(["enter", "down"]));
      await step(`${name}: field ${i} EDIT+up`, () => press(["edit", "up"]));
      await step(`${name}: field ${i} EDIT+ENTER`, () => press(["edit", "enter"]));
      await step(`${name}: advance to field ${i + 1}`, () => press(["down"]));
      if (failed) return;
    }
  };

  await sweepFields("project", 6);

  // Device screen (MIDI device, sync, line out, remote UI, brightness...).
  await step("nav to device screen", async () => {
    const ok = await gotoScreen("device", ["nav", "up"], 3);
    if (!ok) throw new Error("never reached device screen");
  });
  await sweepFields("device", 6);

  // Instrument screen: song -> chain -> phrase -> instrument.
  await step("back to song", async () => {
    if (!(await backToSong())) throw new Error("could not return to song");
  });
  await step("nav to instrument screen", async () => {
    const ok = await gotoScreen("instrument", ["nav", "right"], 5);
    if (!ok) {
      throw new Error(
        "never reached instrument screen -- the bundled project has no " +
        "instruments? rebuild with -DPT_WEB_SDROOT pointing at one that does");
    }
  });
  await sweepFields("instrument", 12);

  // Instrument type switching rebuilds the whole field list -- the most
  // likely place for a stale UIField* to survive.
  for (let t = 0; t < 4 && !failed; t++) {
    await step(`instrument: type change ${t} (go to Type)`, async () => {
      for (let i = 0; i < 14; i++) await press(["up"]);
    });
    await step(`instrument: type change ${t} (next type)`, () => press(["right"]));
    await step(`instrument: type change ${t} (walk fields)`, async () => {
      for (let i = 0; i < 5; i++) await press(["down"]);
    });
    await step(`instrument: type change ${t} (ENTER+up)`, () => press(["enter", "up"]));
    await step(`instrument: type change ${t} (EDIT+ENTER)`, () => press(["edit", "enter"]));
  }

  // Table screen. Reached with NAV+DOWN from the instrument screen, not
  // NAV+RIGHT -- InstrumentView::ProcessButtonMask maps DOWN to VT_TABLE2.
  await step("nav to table", async () => {
    const ok = await gotoScreen("table", ["nav", "down"], 3);
    if (!ok) throw new Error("never reached table screen");
  });
  await sweepFields("table", 4);
  await step("nav to mixer", async () => {
    if (!(await backToSong())) throw new Error("could not return to song");
    await press(["nav", "down"]);
    // The mixer keeps the "Song" title -- it is an overlay on the song
    // screen, not a screen of its own -- so identify it by the per-channel
    // volume strip it draws along the bottom instead.
    const s = await screen();
    if (!/\b99(\s+99){3,}/.test(s.text)) {
      throw new Error("mixer channel strip not visible after NAV+DOWN");
    }
  });
  await sweepFields("mixer", 5);

  // Playback with the sweep's accumulated edits still applied. Leave the
  // mixer first: it shares the song screen's title, so backToSong() would
  // think it was already home.
  await step("leave mixer", () => press(["nav", "up"]));
  await step("start playback", () => press(["play"], 200));
  await step("let it run", () => new Promise((r) => setTimeout(r, 3000)));
  await step("stop playback", () => press(["play"], 200));

  console.log(log.join("\n"));
  await page.screenshot({ path: "/tmp/web_settings.png" });

  if (failed) {
    console.log(`\nFAILED at: ${failed.label}`);
    console.log(`  ${failed.error}`);
    // The last few steps matter more than the whole log: these failures
    // depend on accumulated state, so the path in is the repro.
    console.log("  preceding steps:");
    for (const l of log.slice(-8)) console.log("  " + l.trim());
    const s = await screen().catch(() => null);
    if (s) {
      console.log("  screen at failure:\n" + s.text.split("\n")
        .map((l) => "    " + l).join("\n"));
    }
  }
  console.log("\nsteps run:", log.length);
  console.log("RESULT:", failed ? "FAIL" : "PASS");

  await browser.close();
  process.exit(failed ? 1 : 0);
})().catch((e) => {
  console.error("FATAL", e);
  process.exit(2);
});
