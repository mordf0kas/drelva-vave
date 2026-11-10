// SPDX-License-Identifier: GPL-3.0-only
// Random panel input on the emulator build for a simulated while: every button, key and
// encoder, on every page. Fails if the audio is ever not a number or leaves -1..1.
// usage: node tools/fuzz_test.mjs [seconds] [seed]
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const seconds = Number(process.argv[2] || 120);
let seed = Number(process.argv[3] || 1) >>> 0;
const rnd = (n) => { seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0; return Math.floor(seed / 4294967296 * n); };
const stub = new Proxy({}, { get: () => () => 0 });
const { instance } = await WebAssembly.instantiate(readFileSync(join(root, "build/emu/drelva-vave.wasm")),
  { env: stub, wasi_snapshot_preview1: stub });
const ex = instance.exports;
ex._initialize();
ex.web_boot(1023);
ex.web_buttons(1 << 12); ex.web_buttons(0);
let worst = 0, actions = 0;
for (let block = 0; block < seconds * 44100 / 128; block++) {
  if (rnd(6) === 0) {
    const kind = rnd(10);
    if (kind < 5) ex.web_encoder(rnd(7), rnd(2) ? rnd(6) + 1 : -rnd(6) - 1);
    else if (kind < 8) { const b = rnd(14); if (b !== 12 || rnd(8) === 0) { ex.web_buttons((rnd(4) === 0 ? 1 << 8 : 0) | 1 << b); ex.web_buttons(0); } }
    else { ex.web_keys(1 << rnd(27)); if (rnd(2)) ex.web_keys(0); }
    actions++;
  }
  ex.web_render(128);
  for (const ptr of [ex.web_out_l(), ex.web_out_r(), ...[0, 1, 2, 3, 4, 5].map((t) => ex.web_stem(t))]) {
    for (const x of new Float32Array(ex.memory.buffer, ptr, 128)) {
      if (!(Math.abs(x) <= 1.0001)) { console.log(`bad sample ${x} at block ${block} after ${actions} actions`); process.exit(1); }
      if (Math.abs(x) > worst) worst = Math.abs(x);
    }
  }
}
console.log(`${seconds} s, ${actions} random actions, peak ${worst.toFixed(3)}: ok`);
