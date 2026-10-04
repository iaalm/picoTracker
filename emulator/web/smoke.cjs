// Headless smoke test for the WebAssembly emulator build.
// Loads the page, waits for the canvas to show non-black pixels, sends a few
// keys, and writes screenshots. Exits non-zero if the UI never renders.
const puppeteer = require("puppeteer-core");

const URL = "http://127.0.0.1:8731/";
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
      "--window-size=800,700",
    ],
  });
  const page = await browser.newPage();
  await page.setViewport({ width: 800, height: 700 });

  const logs = [];
  page.on("console", (m) => logs.push(`[${m.type()}] ${m.text()}`));
  page.on("pageerror", (e) => logs.push(`[pageerror] ${e.message}`));

  await page.goto(URL, { waitUntil: "domcontentloaded", timeout: 60000 });

  // Dismiss the click-to-start overlay (also resumes the AudioContext).
  await page.waitForSelector("#overlay", { timeout: 30000 });
  await page.click("#overlay");

  // The real check: does the canvas contain non-black pixels? A wasm module
  // that loads but never renders would otherwise look like a pass.
  //
  // Screenshot the element rather than reading the canvas back: the WebGL
  // context has preserveDrawingBuffer false, so a readback after present
  // returns an empty buffer even while the UI is drawing fine.
  const probe = async () => {
    const el = await page.$("#canvas");
    if (!el) return { ready: false };
    const buf = await el.screenshot({ encoding: "binary" });
    let lit = 0;
    // Count bytes that are clearly not black background. Crude but enough
    // to tell "rendering" from "blank", and to diff two frames.
    for (let i = 0; i < buf.length; i++) {
      if (buf[i] > 12) lit++;
    }
    const size = await page.evaluate(() => {
      const c = document.getElementById("canvas");
      return { w: c.width, h: c.height };
    });
    return { ready: true, ...size, lit, bytes: buf.length, hash: hashOf(buf) };
  };

  const hashOf = (buf) => {
    let h = 2166136261;
    for (let i = 0; i < buf.length; i++) {
      h ^= buf[i];
      h = Math.imul(h, 16777619);
    }
    return (h >>> 0).toString(16);
  };

  let shot = null;
  for (let i = 0; i < 40; i++) {
    await new Promise((r) => setTimeout(r, 500));
    shot = await probe();
    if (shot.ready && shot.lit > 200) break;
  }

  console.log("CANVAS:", JSON.stringify(shot));
  await page.screenshot({ path: "/tmp/web_boot.png" });

  // Drive the UI: NAV-ish navigation plus PLAY, then re-screenshot.
  await page.focus("#canvas");
  for (const k of ["ArrowRight", "ArrowRight", "ArrowDown"]) {
    await page.keyboard.press(k);
    await new Promise((r) => setTimeout(r, 180));
  }
  const afterKeys = await probe();
  console.log("AFTER_KEYS:", JSON.stringify(afterKeys));

  await page.keyboard.press("Space");
  await new Promise((r) => setTimeout(r, 2500));
  const afterPlay = await probe();
  console.log("AFTER_PLAY:", JSON.stringify(afterPlay));
  await page.screenshot({ path: "/tmp/web_play.png" });

  console.log("--- console ---");
  console.log(logs.slice(0, 60).join("\n"));

  // A frame that never changes after input means the UI rendered once and
  // then froze -- report that rather than calling a static image a pass.
  const changed = afterKeys.hash !== shot.hash || afterPlay.hash !== shot.hash;
  console.log("FRAME_CHANGED_AFTER_INPUT:", changed);

  await browser.close();
  const ok = shot && shot.ready && shot.lit > 200 && changed;
  process.exit(ok ? 0 : 1);
})().catch((e) => {
  console.error("FATAL", e);
  process.exit(2);
});
