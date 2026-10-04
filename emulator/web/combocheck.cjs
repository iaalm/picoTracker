// Does a two-finger combo produce the combo's effect in the UI?
//
// The bits reaching the firmware are checked by touchcheck.cjs. This goes
// one step further: NAV+UP switches the tracker to the project screen,
// which neither button does alone. If the pad serialised the two contacts
// into separate presses, that switch would not happen.
//
// NAV+UP rather than NAV+RIGHT on purpose: NAV+RIGHT opens the chain for
// the cell under the cursor and does nothing when that cell is empty
// (SongView.cpp checks *data != 0xFF), so on the bundled empty project it
// would look like a broken combo.
const puppeteer = require("puppeteer-core");

const URL = "http://127.0.0.1:8731/picoTrackerSDL.html";
const EXE =
  "/home/simon/Projects/picoTracker/.browser/chrome-headless-shell-linux64/chrome-headless-shell";

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
  await page.setViewport({
    width: 420, height: 860, isMobile: true, hasTouch: true,
    deviceScaleFactor: 2,
  });

  await page.goto(URL, { waitUntil: "domcontentloaded", timeout: 60000 });
  await page.waitForSelector("#overlay", { timeout: 30000 });
  await page.click("#overlay");
  await new Promise((r) => setTimeout(r, 7000));

  const hashOf = (b) => {
    let h = 2166136261;
    for (let i = 0; i < b.length; i++) { h ^= b[i]; h = Math.imul(h, 16777619); }
    return (h >>> 0).toString(16);
  };
  const shot = async (name) => {
    const el = await page.$("#canvas");
    const buf = await el.screenshot({ encoding: "binary" });
    if (name) await el.screenshot({ path: `/tmp/combo_${name}.png` });
    return hashOf(buf);
  };
  const centerOf = (k) =>
    page.evaluate((key) => {
      const r = document.querySelector(`.key[data-key="${key}"]`).getBoundingClientRect();
      return { x: r.left + r.width / 2, y: r.top + r.height / 2 };
    }, k);
  const touch = (type, pts) =>
    page._client().send("Input.dispatchTouchEvent", {
      type, touchPoints: pts.map((p, i) => ({ x: p.x, y: p.y, id: i })),
    });

  const base = await shot("base");

  // UP alone: moves the cursor, stays on the song screen.
  const up = await centerOf("up");
  await touch("touchStart", [up]);
  await new Promise((r) => setTimeout(r, 200));
  await touch("touchEnd", []);
  await new Promise((r) => setTimeout(r, 500));
  const afterUpOnly = await shot("up_only");

  // NAV held, then UP pressed with it: switches to the project screen.
  const nav = await centerOf("nav");
  await touch("touchStart", [nav]);
  await new Promise((r) => setTimeout(r, 250));
  await touch("touchStart", [nav, up]);
  await new Promise((r) => setTimeout(r, 400));
  // Release UP but keep NAV down, so the combo is a real two-contact press
  // rather than a pair of taps.
  await touch("touchEnd", [nav]);
  await new Promise((r) => setTimeout(r, 400));
  await touch("touchEnd", []);
  await new Promise((r) => setTimeout(r, 700));
  const afterCombo = await shot("nav_up");

  console.log("base          :", base);
  console.log("UP only       :", afterUpOnly);
  console.log("NAV+UP        :", afterCombo);

  const comboDistinct =
    afterCombo !== afterUpOnly && afterCombo !== base;
  console.log("COMBO_PRODUCED_DISTINCT_SCREEN:", comboDistinct);
  console.log("RESULT:", comboDistinct ? "PASS" : "FAIL");

  await browser.close();
  process.exit(comboDistinct ? 0 : 1);
})().catch((e) => { console.error("FATAL", e); process.exit(2); });
