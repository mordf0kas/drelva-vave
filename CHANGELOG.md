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
- Two new machines: Perc Box (hat, clap, FM perc on Euclidean patterns) and Textura.
- LOAD on the MIX page puts any machine on any track; presets remember it.
- SYSTEM settings (ARP twice): clock source and offset, MIDI clock send, Bluetooth MIDI,
  USB layout and bit depth, brightness, key lights.
- Manual: `docs/Drelva-Vave-Manual.pdf`, built by `tools/make_manual.py` from emulator screens.
- Tuned for techno at switch-on: all machines in G, a louder kick, a five-stage Phrygian line
  on Mini Atlantis, sparser Perc Box, Phrase Box on 8-bar phrases with VARY, darker repeats.
- Swarm Engine: TONE (low-pass) and MOVE (slow sweep). Master: PUMP ducks every track but the
  kick's on each beat (mix only).
- Mini Atlantis: the sequencer now works like an ML-185. Each of the 8 stages is edited on
  its own (pitch, pulses, gate mode, slide, accent, skip; white keys pick the stage), with
  LAST, playing order (forward, reverse, pendulum, random, drunk), clock division and gate
  length. The picture shows eight sliders and pulse lamps. Presets keep the stages. The
  FILTER page and ROOT are gone (use PLAY, TUNE; KEYTRK moved to LFO). Older presets keep the
  machine's current settings.
- `tools/fuzz_test.mjs`: random panel input, fails on any bad sample.
- Elastico replaces Mini Atlantis: Parkoid's elastic FM voice with a spring that starts from
  rest (no jump, so no click), a soft attack and a slow drift, on the ML-185 style stage
  sequencer. Mini Atlantis is removed; presets that held it keep the track's current machine.
