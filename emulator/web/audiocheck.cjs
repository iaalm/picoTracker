// Audio check for the WebAssembly build: loads a project that actually has
// notes in it, presses PLAY, and taps the AudioContext destination to see
// whether real samples reach the output. Logging alone can't distinguish
// "mixer ran" from "mixer produced silence".
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
  await page.setViewport({ width: 800, height: 700 });
  const logs = [];
  page.on("console", (m) => logs.push(m.text()));
  page.on("pageerror", (e) => logs.push("pageerror: " + e.message));

  // Patch AudioNode.connect before any script runs: SDL wires its output to
  // destination during module startup, so a hook installed afterwards would
  // never see the connection and would report silence on a working build.
  await page.evaluateOnNewDocument(() => {
    window.Module = window.Module || {};
    window.Module.preRun = window.Module.preRun || [];
    window.Module.preRun.push(function () {
      FS.writeFile("/sdcard/.current", "lgpt_CTX2");
    });

    const origConnect = AudioNode.prototype.connect;
    window.__taps = [];
    AudioNode.prototype.connect = function (dest, ...rest) {
      try {
        if (dest instanceof AudioDestinationNode) {
          const ctx = dest.context;
          const an = ctx.createAnalyser();
          an.fftSize = 2048;
          origConnect.call(this, an);
          window.__taps.push(an);
          window.__ctx = ctx;
        }
      } catch (e) {
        window.__hookError = String(e);
      }
      return origConnect.call(this, dest, ...rest);
    };

    window.__tap = () => {
      let peak = 0,
        nonzero = 0,
        n = 0;
      for (const an of window.__taps || []) {
        const buf = new Float32Array(an.fftSize);
        an.getFloatTimeDomainData(buf);
        n += buf.length;
        for (const v of buf) {
          const a = Math.abs(v);
          if (a > peak) peak = a;
          if (a > 1e-5) nonzero++;
        }
      }
      return { peak, nonzero, n, taps: (window.__taps || []).length };
    };
  });

  await page.goto(URL, { waitUntil: "domcontentloaded", timeout: 60000 });
  await page.waitForSelector("#overlay", { timeout: 30000 });
  await page.click("#overlay");
  await new Promise((r) => setTimeout(r, 6000));

  await page.focus("#canvas");
  await page.keyboard.press("Space");

  let best = { peak: 0, nonzero: 0 };
  for (let i = 0; i < 30; i++) {
    await new Promise((r) => setTimeout(r, 400));
    const s = await page.evaluate(() =>
      window.__tap ? window.__tap() : { error: "no tap" }
    );
    if (s && s.peak > best.peak) best = s;
  }

  console.log(
    "AUDIO_CTX:",
    JSON.stringify(
      await page.evaluate(() => ({
        taps: (window.__taps || []).length,
        state: window.__ctx ? window.__ctx.state : null,
        rate: window.__ctx ? window.__ctx.sampleRate : null,
        hookError: window.__hookError || null,
      }))
    )
  );
  console.log("AUDIO_BEST:", JSON.stringify(best));
  console.log(
    "LOGS:",
    logs.filter((l) => !/GL Driver|ScriptProcessor/.test(l)).slice(0, 25).join("\n")
  );

  await page.screenshot({ path: "/tmp/web_audio.png" });
  await browser.close();
  process.exit(best.peak > 0.0005 ? 0 : 1);
})().catch((e) => {
  console.error("FATAL", e);
  process.exit(2);
});
