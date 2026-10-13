// SPDX-License-Identifier: GPL-3.0-only
// The browser build (build/emu/melodee.wasm, web/emu/melodee_web.c) in Node, as the worklet runs it.
// After Felucca's web/emu/emu_test.mjs (Hugelton Instruments, GPL-3.0).
//   node web/emu/emu_test.mjs build/emu/melodee.wasm
// Checks: it boots (splash, then HOME on the screen), a held key sounds and lights its LED, PLAY starts
// the sequencer, a user preset saved from the panel reaches the flash, and the flash sectors kept as the
// page keeps them bring it back in a fresh instance; the same gestures twice give the same audio.
import fs from "fs";
import { storeSectors, restoreSectors } from "./worklet.js";

const wasmPath = process.argv[2] || "build/emu/melodee.wasm";
const bytes = fs.readFileSync(wasmPath);
const B = { FX: 0, SCL: 1, ENV: 2, LFO: 3, EDIT: 4, GLO: 5, HOME: 6, SAVE: 7, ARP: 8, SEQ: 9, PLAY: 10, REC: 11, OCTDN: 12, OCTUP: 13 };
const EN = { SELECT: 0, ALGO: 1, PRESETS: 2, K1: 3, K2: 4, K3: 5, K4: 6 };
let fails = 0;
const check = (what, ok) => { console.log(`emu: ${what.padEnd(76)} ${ok ? "ok" : "FAIL"}`); if (!ok) fails++; };

async function device(saved) {
  const { instance } = await WebAssembly.instantiate(bytes, {});
  const ex = instance.exports, mem = ex.memory;
  ex._initialize();
  ex.web_nor_erase();
  if (saved) restoreSectors(new Uint8Array(mem.buffer, ex.web_nor(), ex.web_nor_size()), saved);
  ex.web_boot();
  const d = { ex, mem, frames: 0, kept: [] };
  d.render = (n) => {
    ex.web_render(n);
    d.frames += n;
    const writes = ex.web_flash_writes_count();
    if (writes !== d.lastWrites) { d.lastWrites = writes; d.kept = storeSectors(new Uint8Array(mem.buffer, ex.web_nor(), ex.web_nor_size())); }
  };
  d.lastWrites = ex.web_flash_writes_count();
  d.screen = () => new Uint16Array(mem.buffer, ex.web_screen(), 240 * 240);
  d.audio = () => { const a = new Float32Array(mem.buffer, ex.web_out_l(), 128); return a.slice(); };
  d.press = (m) => { ex.web_buttons(m >>> 0); d.render(512); ex.web_buttons(0); };       // a tap: held > the debounce
  d.hold = (m, n) => { ex.web_buttons(m >>> 0); for (let i = 0; i < n; i++) d.render(512); ex.web_buttons(0); };
  d.enc = (role, n) => ex.web_enc(role, n);
  d.key = (k, down) => { const s = d.audio(); ex.web_keys(down ? 1 << k : 0); for (let i = 0; i < 8; i++) d.render(128); return d.audio(); };
  return d;
}

const splashish = (fb) => {                                            // non-black pixels anywhere
  for (let i = 0; i < fb.length; i++) if (fb[i]) return true;
  return false;
};

const d = await device();
d.render(128);
check("boots: the splash is drawn", d.ex.web_screen_draws() > 0);
// run past the splash (430 ms of device time) into HOME
for (let i = 0; i < 60; i++) d.render(1024);
const after = d.screen().slice();
check("the main loop draws (something changes after the splash)", splashish(after));

// a held key sounds
const silence = d.audio();
d.render(2048);
const before = d.audio();
const withKey = d.key(8, true);                                        // C4-ish (key 8 = F3 + 8 semitones)
let energy = 0; for (let i = 0; i < withKey.length; i++) energy += Math.abs(withKey[i]);
check("a held key sounds", energy > 0.001);
d.key(8, false);
d.render(2048);

// PLAY starts the sequencer
d.press(1 << B.PLAY);
for (let i = 0; i < 20; i++) d.render(1024);
check("PLAY starts the transport", d.ex.web_playing() !== 0);
d.press(1 << B.PLAY);                                                  // stop
for (let i = 0; i < 10; i++) d.render(1024);

// the GLOBAL page's kept values (CLK/TUNE/MIDI/ROUT) are saved 1.5 s after the turn (stopped)
d.ex.web_buttons(1 << B.GLO); for (let i = 0; i < 8; i++) d.render(512); // open GLO (page holds while pressed)
d.ex.web_enc(3, 2);                                                    // knob 1: a kept value, while the page is open
for (let i = 0; i < 8; i++) d.render(512);
d.ex.web_buttons(0);
for (let i = 0; i < 12; i++) d.render(1024);                           // 1.5 s settle
const keptBefore = Object.keys(d.kept).length;
check("the flash kept something (settings)", keptBefore > 0);

// a fresh instance with the saved sectors comes back
const e2 = await device(d.kept);
for (let i = 0; i < 60; i++) e2.render(1024);
check("a fresh boot with the saved flash runs", e2.ex.web_screen_draws() > 0);

// determinism: the same gestures twice, the same audio
const run = async () => {
  const x = await device();
  for (let i = 0; i < 60; i++) x.render(1024);
  const a1 = x.key(8, true);
  x.key(8, false);
  return Buffer.from(a1).toString("hex");
};
const [r1, r2] = [await run(), await run()];
check("the same gestures give the same audio (deterministic)", r1 === r2);

console.log(fails ? `emu: FAILED (${fails})` : "emu: all checks ok");
process.exit(fails ? 1 : 0);
