// SPDX-License-Identifier: GPL-3.0-only
// Run the emulator build without a browser: press PLAY, render some seconds, and write
//   OUT/mix.f32        mono float32 mix at 44.1 kHz
//   OUT/stemN.f32      each track's own channel
//   OUT/screen.rgb565  the last frame, 240 x 240, panel byte order
// usage: node tools/render_test.mjs OUT [seconds] [extBpm] [jitterMs] [track]
//   extBpm: send MIDI Start and clock at that tempo instead of pressing PLAY, each tick late
//   by a random 0..jitterMs (a Bluetooth link), on top of the 2.9 ms render granularity
import { readFileSync, writeFileSync, mkdirSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const out = process.argv[2];
const seconds = Number(process.argv[3] || 4);
if (!out) throw new Error("usage: node tools/render_test.mjs OUT [seconds]");
mkdirSync(out, { recursive: true });

const stub = new Proxy({}, { get: () => () => 0 });
const wasm = readFileSync(join(root, "build/emu/drelva-vave.wasm"));
const { instance } = await WebAssembly.instantiate(wasm, { env: stub, wasi_snapshot_preview1: stub });
const ex = instance.exports;
ex._initialize();
ex.web_boot(1023);
const track = Number(process.argv[6] || 1);   // track shown on the screen
ex.web_buttons(track > 6 ? 1 << 10 : 1 << (1 + track)); ex.web_buttons(0);   // 7: the master (ARP)
const extBpm = Number(process.argv[4] || 0), jitter = Number(process.argv[5] || 0) * 44.1;
let rnd = 12345;
const rand = () => ((rnd = (rnd * 1103515245 + 12345) >>> 0) / 4294967296);
const tickLen = extBpm ? 44100 * 60 / extBpm / 24 : 0;
let nextTick = 4410, due = nextTick, started = false;   // the master starts 100 ms in
if (!extBpm) {
  ex.web_buttons(1 << 12);   // PLAY down
  ex.web_buttons(0);
}

const total = Math.round(seconds * 44100), tracks = 6;
const mix = new Float32Array(total);
const stems = Array.from({ length: tracks }, () => new Float32Array(total));
for (let done = 0; done < total; done += 128) {
  const n = Math.min(128, total - done);
  while (extBpm && due <= done) {
    if (!started) { ex.web_midi_in(0xFA, 0, 0); started = true; }
    ex.web_midi_in(0xF8, 0, 0);
    nextTick += tickLen;
    due = nextTick + rand() * jitter;
  }
  ex.web_render(n);
  mix.set(new Float32Array(ex.memory.buffer, ex.web_out_l(), n), done);
  for (let t = 0; t < tracks; t++) stems[t].set(new Float32Array(ex.memory.buffer, ex.web_stem(t), n), done);
}
writeFileSync(join(out, "mix.f32"), Buffer.from(mix.buffer));
stems.forEach((s, t) => writeFileSync(join(out, `stem${t + 1}.f32`), Buffer.from(s.buffer)));
writeFileSync(join(out, "screen.rgb565"), Buffer.from(new Uint8Array(ex.memory.buffer, ex.web_fb(), 240 * 240 * 2).slice()));
console.log(`rendered ${total} samples, ms=${ex.web_now()}, leds=${ex.web_led_buttons()}`);
