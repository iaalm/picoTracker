// Does the on-screen pad actually drive the UI, not just set a mask?
//
// touchcheck.cjs asserts the bits reaching pt_set_touch_mask; this asserts
// the consequence. It compares canvas screenshots before and after a press,
// so a pad that updates the mask but never reaches the firmware fails here.
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

  const hashOf = (buf) => {
    let h = 2166136261;
    for (let i = 0; i < buf.length; i++) {
      h ^= buf[i];
      h = Math.imul(h, 16777619);
    }
    return (h >>> 0).toString(16);
  };
  const shot = async (name) => {
    const el = await page.$("#canvas");
    const buf = await el.screenshot({ encoding: "binary" });
    if (name) await el.screenshot({ path: `/tmp/pad_${name}.png` });
    return hashOf(buf);
  };

  const centerOf = (key) =>
    page.evaluate((k) => {
      const el = document.querySelector(`.key[data-key="${k}"]`);
      const r = el.getBoundingClientRect();
      return { x: r.left + r.width / 2, y: r.top + r.height / 2 };
    }, key);

  const touch = (type, points) =>
    page._client().send("Input.dispatchTouchEvent", {
      type,
      touchPoints: points.map((p, i) => ({ x: p.x, y: p.y, id: i })),
    });

  const tap = async (key, holdMs) => {
    const c = await centerOf(key);
    await touch("touchStart", [c]);
    await new Promise((r) => setTimeout(r, holdMs || 200));
    await touch("touchEnd", []);
    await new Promise((r) => setTimeout(r, 400));
  };

  const before = await shot("before");

  // RIGHT moves the song-view cursor one column.
  await tap("right");
  const afterRight = await shot("after_right");

  // PLAY starts the sequencer, which changes the screen continuously.
  await tap("play");
  await new Promise((r) => setTimeout(r, 1500));
  const afterPlay = await shot("after_play");

  console.log("before      :", before);
  console.log("after RIGHT :", afterRight, afterRight !== before ? "CHANGED" : "SAME");
  console.log("after PLAY  :", afterPlay, afterPlay !== afterRight ? "CHANGED" : "SAME");

  const ok = afterRight !== before && afterPlay !== afterRight;
  console.log("RESULT:", ok ? "PASS" : "FAIL");
  await browser.close();
  process.exit(ok ? 0 : 1);
})().catch((e) => {
  console.error("FATAL", e);
  process.exit(2);
});
