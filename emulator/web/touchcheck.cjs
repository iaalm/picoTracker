// Multi-touch check for the on-screen pad.
//
// The point of the pad is that combos work: the tracker's UI is built around
// ALT+arrow and NAV+arrow, so two simultaneous contacts have to produce a
// mask with both bits set. CDP's Input.dispatchTouchEvent is used directly
// because puppeteer's touchscreen helper models only a single contact.
const puppeteer = require("puppeteer-core");

const URL = "http://127.0.0.1:8731/";
const EXE =
  "/home/simon/Projects/picoTracker/.browser/chrome-headless-shell-linux64/chrome-headless-shell";

// Must match SDLKeypadBits in sdlEventManager.h.
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
  // Phone-ish viewport with touch, so the coarse-pointer media query applies.
  await page.setViewport({
    width: 420,
    height: 860,
    isMobile: true,
    hasTouch: true,
    deviceScaleFactor: 2,
  });

  const logs = [];
  page.on("console", (m) => logs.push(m.text()));
  page.on("pageerror", (e) => logs.push("pageerror: " + e.message));

  // Record every mask the pad pushes into the runtime, so the test asserts
  // on what the firmware would actually receive.
  await page.evaluateOnNewDocument(() => {
    window.__masks = [];
    const install = () => {
      if (!window.Module || !window.Module._pt_set_touch_mask) return false;
      const orig = window.Module._pt_set_touch_mask;
      window.Module._pt_set_touch_mask = function (m) {
        window.__masks.push(m);
        return orig(m);
      };
      return true;
    };
    const iv = setInterval(() => {
      if (install()) clearInterval(iv);
    }, 50);
  });

  await page.goto(URL, { waitUntil: "domcontentloaded", timeout: 60000 });
  await page.waitForSelector("#overlay", { timeout: 30000 });
  await page.click("#overlay");
  await new Promise((r) => setTimeout(r, 6000));

  const padVisible = await page.evaluate(() => {
    const p = document.getElementById("pad");
    return getComputedStyle(p).display !== "none";
  });
  console.log("PAD_VISIBLE:", padVisible);

  const centerOf = (key) =>
    page.evaluate((k) => {
      const el = document.querySelector(`.key[data-key="${k}"]`);
      if (!el) return null;
      const r = el.getBoundingClientRect();
      return { x: r.left + r.width / 2, y: r.top + r.height / 2 };
    }, key);

  const touch = async (type, points) =>
    page._client().send("Input.dispatchTouchEvent", {
      type,
      touchPoints: points.map((p, i) => ({ x: p.x, y: p.y, id: i })),
    });

  const results = {};

  // 1. Single press: exactly one bit.
  {
    const r = await centerOf("right");
    await page.evaluate(() => (window.__masks = []));
    await touch("touchStart", [r]);
    await new Promise((res) => setTimeout(res, 250));
    const during = await page.evaluate(() => window.__masks.slice());
    await touch("touchEnd", []);
    await new Promise((res) => setTimeout(res, 250));
    const after = await page.evaluate(() => window.__masks.slice());
    results.single = { during, after };
  }

  // 2. The real test: ALT held while RIGHT is pressed -- both bits at once.
  {
    const alt = await centerOf("alt");
    const right = await centerOf("right");
    await page.evaluate(() => (window.__masks = []));
    await touch("touchStart", [alt]);
    await new Promise((res) => setTimeout(res, 150));
    await touch("touchStart", [alt, right]);
    await new Promise((res) => setTimeout(res, 300));
    const combo = await page.evaluate(() => window.__masks.slice());
    await touch("touchEnd", []);
    await new Promise((res) => setTimeout(res, 200));
    results.combo = combo;
  }

  // 3. Three contacts at once (NAV+ALT+DOWN).
  {
    const nav = await centerOf("nav");
    const alt = await centerOf("alt");
    const down = await centerOf("down");
    await page.evaluate(() => (window.__masks = []));
    await touch("touchStart", [nav]);
    await touch("touchStart", [nav, alt]);
    await touch("touchStart", [nav, alt, down]);
    await new Promise((res) => setTimeout(res, 300));
    const triple = await page.evaluate(() => window.__masks.slice());
    await touch("touchEnd", []);
    await new Promise((res) => setTimeout(res, 200));
    results.triple = triple;
  }

  const want2 = BITS.alt | BITS.right;
  const want3 = BITS.nav | BITS.alt | BITS.down;
  const sawSingle = results.single.during.includes(BITS.right);
  const released = results.single.after.slice(-1)[0] === 0;
  const sawCombo = results.combo.includes(want2);
  const sawTriple = results.triple.includes(want3);

  console.log("SINGLE_right:", sawSingle, JSON.stringify(results.single));
  console.log(
    "COMBO_alt+right:", sawCombo,
    "want", want2, "got", JSON.stringify(results.combo)
  );
  console.log(
    "TRIPLE_nav+alt+down:", sawTriple,
    "want", want3, "got", JSON.stringify(results.triple)
  );
  console.log("RELEASED_TO_ZERO:", released);

  await page.screenshot({ path: "/tmp/web_pad.png" });
  console.log(
    "LOGS:",
    logs.filter((l) => !/GL Driver|ScriptProcessor/.test(l)).slice(0, 15).join("\n")
  );

  await browser.close();
  const ok = padVisible && sawSingle && released && sawCombo && sawTriple;
  console.log("RESULT:", ok ? "PASS" : "FAIL");
  process.exit(ok ? 0 : 1);
})().catch((e) => {
  console.error("FATAL", e);
  process.exit(2);
});
