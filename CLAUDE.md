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
