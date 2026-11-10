// SPDX-License-Identifier: GPL-3.0-only
// Drive the emulator build with a script of panel actions and save the screen.
//   node tools/shot.mjs OUT.rgb565 ACTION...
// Actions:  b<N>      press and release button N (0 OCT-, 1 OCT+, 2..7 FX SCL ENV LFO EDIT GLO,
//                     8 HOME, 9 SAVE, 10 ARP, 11 SEQ, 12 PLAY, 13 REC)
//           h<N>+b<M> press button M while N is held
//           e<N>:<D>  turn encoder N by D detents, one at a time (0 SELECT, 1 ALGORITHM,
//                     2..5 KNOB 1..4, 6 PRESETS)
//           k<N>      press and release key N (0..26)
//           r<S>      render S seconds
// The screen is 240 x 240 RGB565 in the panel's byte order (big-endian words).
import { readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const [out, ...actions] = process.argv.slice(2);
if (!out) throw new Error("usage: node tools/shot.mjs OUT.rgb565 ACTION...");
const stub = new Proxy({}, { get: () => () => 0 });
const { instance } = await WebAssembly.instantiate(readFileSync(join(root, "build/emu/drelva-vave.wasm")),
  { env: stub, wasi_snapshot_preview1: stub });
const ex = instance.exports;
ex._initialize();
ex.web_boot(1023);
const render = (s) => { for (let n = Math.round(s * 44100 / 128); n > 0; n--) ex.web_render(128); };
for (const a of actions) {
  let m;
  if ((m = /^h(\d+)\+b(\d+)$/.exec(a))) { ex.web_buttons(1 << m[1]); ex.web_buttons(1 << m[1] | 1 << m[2]); ex.web_buttons(0); }
  else if ((m = /^b(\d+)$/.exec(a))) { ex.web_buttons(1 << m[1]); ex.web_buttons(0); }
  else if ((m = /^e(\d+):(-?\d+)$/.exec(a))) { for (let i = Math.abs(m[2]); i > 0; i--) ex.web_encoder(+m[1], Math.sign(m[2])); }
  else if ((m = /^k(\d+)$/.exec(a))) { ex.web_keys(1 << m[1]); ex.web_keys(0); }
  else if ((m = /^r([\d.]+)$/.exec(a))) render(+m[1]);
  else throw new Error("unknown action " + a);
}
render(0.05);
writeFileSync(out, Buffer.from(new Uint8Array(ex.memory.buffer, ex.web_fb(), 240 * 240 * 2).slice()));
