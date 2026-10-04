// Regression: ENTER+UP on a field that owns no Variable.
//
// InstrumentView C-cast GetFocus() to UIIntVarField* and called
// GetVariable() through it. On the Name field -- a UITextField, which has no
// such method -- that indexed past the end of the vtable. The device tolerated
// it; WebAssembly bounds- and signature-checks every indirect call, so it
// trapped with "Out of bounds call_indirect".
//
// Repro is the user's: on an instrument with a sample, DOWN twice to reach
// Name, then ENTER+UP.
//
// Needs a project containing instruments: build with
//   -DPT_WEB_SDROOT=<root whose .current names a project with samples>
// Against the default empty project this passes vacuously, since the
// instrument screen has no sample fields to land on.
const puppeteer = require("puppeteer-core");

const URL = process.env.PT_URL || "http://127.0.0.1:8731/";
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

  const errs = [];
  page.on("pageerror", (e) => errs.push("pageerror: " + e.message));
  page.on("console", (m) => {
    const t = m.text();
    if (/RuntimeError|out of bounds|call_indirect/i.test(t)) {
      errs.push("console: " + t);
    }
  });

  await page.goto(URL, { waitUntil: "domcontentloaded", timeout: 60000 });
  await page.waitForSelector("#overlay", { timeout: 30000 });
  await page.click("#overlay");
  await new Promise((r) => setTimeout(r, 8000));

  const centerOf = (k) =>
    page.evaluate((key) => {
      const r = document
        .querySelector(`.key[data-key="${key}"]`)
        .getBoundingClientRect();
      return { x: r.left + r.width / 2, y: r.top + r.height / 2 };
    }, k);
  const touch = (type, pts) =>
    page._client().send("Input.dispatchTouchEvent", {
      type, touchPoints: pts.map((p, i) => ({ x: p.x, y: p.y, id: i })),
    });
  const tap = async (k, ms) => {
    const c = await centerOf(k);
    await touch("touchStart", [c]);
    await new Promise((r) => setTimeout(r, ms || 170));
    await touch("touchEnd", []);
    await new Promise((r) => setTimeout(r, 300));
  };
  const combo = async (a, b) => {
    const x = await centerOf(a), y = await centerOf(b);
    await touch("touchStart", [x]);
    await new Promise((r) => setTimeout(r, 130));
    await touch("touchStart", [x, y]);
    await new Promise((r) => setTimeout(r, 380));
    await touch("touchEnd", [x]);
    await new Promise((r) => setTimeout(r, 160));
    await touch("touchEnd", []);
    await new Promise((r) => setTimeout(r, 550));
  };

  // song -> chain -> phrase -> instrument
  for (let i = 0; i < 3; i++) await combo("nav", "right");

  // Two DOWNs lands on Name; ENTER+UP is what used to trap.
  await tap("down", 180);
  await tap("down", 180);
  await combo("enter", "up");

  // And from the neighbouring fields, since which row Name occupies depends
  // on the instrument type.
  for (let i = 0; i < 3 && !errs.length; i++) {
    await combo("enter", "up");
    await tap("down", 170);
    await combo("enter", "up");
    await tap("up", 170);
  }

  await page.screenshot({ path: "/tmp/web_entercheck.png" });
  const alive = await page
    .evaluate(() => document.getElementById("status").textContent)
    .catch(() => "(page dead)");
  console.log("status:", alive);
  console.log("ERRORS:", errs.length ? errs.slice(0, 3).join("\n  ") : "(none)");
  console.log("RESULT:", errs.length ? "FAIL" : "PASS");

  await browser.close();
  process.exit(errs.length ? 1 : 0);
})().catch((e) => {
  console.error("FATAL", e);
  process.exit(2);
});
