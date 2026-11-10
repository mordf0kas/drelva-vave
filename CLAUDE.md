# Drelva Vave

Custom firmware for the M-Vave FM-1 pocket synth (Diogo calls it "FM-01"). Goal: bring the
Drelvarom "Maschin3s" programs over as machines that each have their own sound engine, and
send every track to the computer as its own USB audio channel (class compliant, shows up in
Ableton as separate inputs). For Diogo and friends; builds are GPL, so the source goes with
every firmware file handed out.

Read `docs/RESEARCH.md` before any work: hardware limits, base-firmware options, flashing risk.

## Rules
- Nothing is flashed to Diogo's unit without a GO, and never before the build ran in an emulator.
- Before the first custom flash: note the stock version, export a backup, confirm the rollback path.
- Never flash one custom firmware over another; go through stock V15 (or TransitFM1) first.
- Audio code: no allocation, locks or logging in the render path; 44.1 kHz, small blocks.
- One application slot of just under 600 KB: track flash size on every build.
- Changes go under `## Unreleased` in `CHANGELOG.md`. Product text in English.

## Layout
- `src/` Drelva core in C99 (no libm, no allocation): `dv_core.c` clock/tracks/UI, `dv_gfx.c`
  drawing, `dv_dsp.h` helpers, `machines/` one file per machine (`dv_machine_t` in `dv.h`).
- `emu/` browser harness; `tools/build-emu.sh` builds `build/emu` (serve it with
  `python -m http.server`), `tools/build-emu.sh --base` builds the untouched Melodee emulator.
- `tools/render_test.mjs OUT [seconds]` renders mix, per-track stems and the screen headless.
- `base/` is Melodee, unmodified: the future platform layer (USB audio, LCD, keys, boot,
  recovery). Its UI and engines are not used; Diogo found Melodee's navigation confusing and
  chose a Drelva-only interface. Not yet wired to the core; needs the JieLi Linux toolchain.
- Default tracks: 1 Sismo Box, 2 Elastico, 3 Textura, 4 Swarm Engine, 5 Phrase Box, 6 Perc Box;
  Mini Atlantis and Stab Box loadable (Diogo asked to keep Mini Atlantis, 2026-11-11).
  Machines are singletons (static state): LOAD swaps tracks. Machine ids in presets are fixed
  (see each `dv_machine_t`); never renumber or reuse.
- Diogo's taste (2026-11-11): hypnotic techno. He rejected Mini Atlantis (its SH-101 voice)
  and Mini Parkoid as it was (too clicky); he likes Parkoid's sounds and wants them elastic.
  Elastico is the answer: the spring starts at rest and is pushed, the attack is soft, the
  oscillators are never restarted. Keep new voices free of jumps at note onsets.
- Elastico's sequencer follows the ML-185 (he asked): per-stage editing; the stages are
  parameters (the layout rows of the STAGE pages are rewritten in update()).
- `src/dv_system.c`: settings read by the platform through `dv_setting()`; only clock source,
  clock offset and brightness act in the emulator.
- Manual: `python tools/make_manual.py` (drelva-cc venv) after `tools/build-emu.sh`; rebuild it
  whenever a control, page or parameter changes. English, like all product text.
- `src/dv_fx.c` is the master section (delay 64 KB + reverb 48 KB of RAM: check against the
  device before wiring). Presets go through `dv_store_read/write`, which the platform provides
  (emulator: browser storage; device: flash, not written yet).
- Drelvarom times given to `CoefForMs` mean "settles to -40 dB in ms": use `dv_settle_ms`.
- Tests: `node tools/preset_test.mjs` (round trip), `node tools/fuzz_test.mjs 240 <seed>`, `node tools/render_test.mjs OUT 16 0 0 <track>`.

## Owed at the end (asked by Diogo, 2026-11-10)
- The PDF manual exists (docs/); complete its "On the device" parts after the first hardware test.
- A vector file (SVG/PDF) with the control names laid out to print as stickers for the FM-1
  panel, since our functions differ from the printed legends. Measure the real panel first.
