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
- Machines chosen: Mini Atlantis, Mini Parkoid, Phrase Box, Sismo Box, Swarm Engine; Stab Box
  only if it fits. Done so far: Sismo Box.

## Owed at the end (asked by Diogo, 2026-11-10)
- A PDF with the instructions (every control, every machine, clock, USB channels).
- A vector file (SVG/PDF) with the control names laid out to print as stickers for the FM-1
  panel, since our functions differ from the printed legends. Measure the real panel first.
