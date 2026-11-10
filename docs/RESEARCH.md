# FM-1 research (2026-11-10)

Secondary sources only so far; nothing verified on hardware or in source yet.

## Hardware
- SoC JieLi AC791N (WL82), pi32v2 CPU, two cores, single-precision FPU. Clock reported as
  240 MHz (FM-1-RE) and 360 MHz (Melodee readme); unresolved.
- 1 MB flash, executes in place, ONE application slot of a little under 600 KB.
- Stock engine: msfa/Dexed DX7 port, 64-sample blocks at about 44.1 kHz.
- USB: USB-MIDI + USB audio (UAC1, stock is stereo 16-bit 44.1 kHz). Updates run over
  USB-MIDI SysEx. Also DIN MIDI in, BLE.
- Controls: 4 knobs, SELECT/MASTER/ALGORITHM/PRESETS/BPM, OCT+/-, PLAY/REC/STOP, page buttons,
  lit keys, colour LCD.

## Base firmware options
1. FM1 Open (official): https://github.com/m-vave/fm1_open — app is GPLv3 with an SDK linking
   exception; the device engine is a binary-only SDK. 8 tracks, engines: FM, sampler, 303, 808,
   PSG, plucked string. Builds on Windows with the JieLi toolchain 2.5.3 (pi32 clang 4.0.1) via
   `sdk/Build.ps1 -FirmwareOnly`. Unknown whether the binary engine lets the app change the USB
   audio channel count.
2. Felucca lineage (community, fully GPL-3.0): Melodee https://github.com/keremimo/melodee
   already ships "4 TRACKS" USB mode: four mono stems, 16/24-bit 44.1 kHz, no driver. Another
   firmware is reported to send 6 tracks + stereo master as 8 channels. Melodee has boot
   protection (interrupted write returns to USB update mode; hold OCT- at power on) and a
   "Return to official V15" path.

Multitrack over USB is therefore proven possible. Open question: how many channels Windows
accepts from a UAC1 device without a driver, and how Ableton sees them (WASAPI/ASIO4ALL/FlexASIO).

## Risk
- Single flash bank: an interrupted write can leave nothing bootable. Mask-ROM boot mode is
  reported reachable (FM-1 Transporter uses a XIAO RP2040 on the USB lines).
- Community tools are built around stock V15; current stock is V16.

## Tools
- Workbench: https://open.m-vave.com/fm1-workbench (Chrome/Edge, USB-MIDI).
- Emulator (third party): joakimeriksson/fm1-emu, runs FM-1 firmware in the browser.
- Lists: https://github.com/cicloid/awesome-fm-1 , https://github.com/RustyPawz/fm1-custom-fw
- Reverse engineering: AL-255/FM-1-RE.

## Source programs (Drelvarom Maschin3s, C:\Drelva\DRELVAROM\software\src\drelvarom\programs)
Acid Box, Bleeps, Hat Box, Mini Atlantis, Mini Parkoid, Phrase Box, Sismo Box, Stab Box,
Swarm Engine, Transe. On the o_C they emit CV/gates; here each needs its own voice.
