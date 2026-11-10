# Changelog

## Unreleased
- Project created; research notes in `docs/RESEARCH.md`.
- Melodee v1.6.1 imported unmodified in `base/` as the platform to build on.
- Drelva core (`src/`): six tracks, one machine per track, one mono output per track, clock,
  one-screen UI in the house look (Michroma, lime on dark).
- First machine: Sismo Box, ported from Drelvarom to float DSP at 44.1 kHz.
- Browser emulator build (`tools/build-emu.sh`) and a headless render test (`tools/render_test.mjs`).
- Screen redrawn: anti-aliased rounded cards, house knobs with value arcs, beat lamps,
  a level meter on every track.
- External MIDI clock: tempo and transport follow incoming clock (meant for Bluetooth);
  the follower averages link jitter instead of playing it.
- Second machine: Mini Atlantis (SH-101 style voice with the stage sequencer and pattern
  generator of Atlantis Seq); the keyboard transposes it.
- Sismo Box: kick, rumble and ducking times now match Drelvarom (they were 4.6 times too long).
- Three more machines ported from Drelvarom: Phrase Box, Stab Box, Swarm Engine.
- Master section (ARP): tempo-synced dub delay, reverb, one-knob filter and drive. Every
  track has a MIX page with level, delay send and reverb send. Track outputs stay dry.
- Presets: 8 slots holding every parameter, the mix and the tempo (SAVE button).
- Sismo Box: the generated groove is now a SEED parameter, so presets recall it.
