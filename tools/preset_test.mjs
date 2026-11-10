// SPDX-License-Identifier: GPL-3.0-only
// Preset round trip on the emulator build, without a browser:
//   store the power-on state in slot 3, change parameters on two tracks and the tempo,
//   load slot 3 again, and check that the audio is the same as a fresh instrument's.
// usage: node tools/preset_test.mjs      (exit code 1 on failure)
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const wasm = readFileSync(join(root, "build/emu/drelva-vave.wasm"));
const stub = new Proxy({}, { get: () => () => 0 });
const BTN = { OCT_DN: 0, OCT_UP: 1, FX: 2, SCL: 3, SAVE: 9, PLAY: 12 }, ENC = { SELECT: 0, ALGORITHM: 1, KNOB1: 2, KNOB3: 4 };

async function boot(store) {
  const { instance } = await WebAssembly.instantiate(wasm, { env: stub, wasi_snapshot_preview1: stub });
  const ex = instance.exports;
  ex._initialize();
  if (store) new Uint8Array(ex.memory.buffer, ex.web_store(), store.length).set(store);
  ex.web_boot(1023);
  return ex;
}
const press = (ex, b) => { ex.web_buttons(1 << b); ex.web_buttons(0); };
function render(ex, seconds) {
  const total = Math.round(seconds * 44100), out = new Float32Array(total);
  for (let done = 0; done < total; done += 128) {
    ex.web_render(128);
    out.set(new Float32Array(ex.memory.buffer, ex.web_out_l(), Math.min(128, total - done)), done);
  }
  return out;
}
const same = (a, b) => a.length === b.length && a.every((x, i) => x === b[i]);

const fresh = await boot();
press(fresh, BTN.PLAY);
const reference = render(fresh, 3);

const ex = await boot();
press(ex, BTN.SAVE);                       // open
ex.web_encoder(ENC.SELECT, 1); ex.web_encoder(ENC.SELECT, 1);   // slot 3
press(ex, BTN.SAVE);                       // store
const stored = new Uint8Array(ex.memory.buffer, ex.web_store(), ex.web_store_size()).slice();
const used = stored[2 * 514] | stored[2 * 514 + 1] << 8;

ex.web_encoder(ENC.KNOB1, -3); ex.web_encoder(ENC.KNOB3, 3);    // Sismo Box: rumble, weight
press(ex, BTN.SCL);
ex.web_encoder(ENC.KNOB1, 3);              // Mini Atlantis: cutoff
ex.web_encoder(ENC.ALGORITHM, -3);         // tempo
press(ex, BTN.PLAY);
const changed = render(ex, 3);
press(ex, BTN.PLAY);                       // stop
render(ex, 4);                             // let the tails and the effects die away

// a new instrument that only has the stored block: load slot 3 and play
const again = await boot(stored);
ex.web_encoder(ENC.ALGORITHM, 9);          // (nothing: `again` is the one under test)
again.web_encoder(ENC.KNOB1, -3);          // start from a different state
again.web_encoder(ENC.ALGORITHM, 5);
press(again, BTN.SAVE);
again.web_encoder(ENC.SELECT, 1); again.web_encoder(ENC.SELECT, 1);
press(again, BTN.OCT_UP);                  // load
press(again, BTN.PLAY);
const restored = render(again, 3);

const ok = used > 100 && !same(reference, changed) && same(reference, restored);
console.log(`slot 3 holds ${used} bytes; changed state differs: ${!same(reference, changed)}; restored equals fresh: ${same(reference, restored)}`);
process.exit(ok ? 0 : 1);
