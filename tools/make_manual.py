# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Drelva Audio
"""Build docs/Drelva-Vave-Manual.pdf.

Screens are taken from the emulator build (tools/shot.mjs), so run tools/build-emu.sh first.
Needs Python with reportlab, numpy and Pillow, and node on the PATH.

  python tools/make_manual.py
"""
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image
from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.units import mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (BaseDocTemplate, Flowable, Frame, Image as RLImage, KeepTogether, PageBreak,
                                PageTemplate, Paragraph, Spacer, Table, TableStyle)

ROOT = Path(__file__).resolve().parent.parent
SHOTS = ROOT / "build" / "manual"
OUT = ROOT / "docs" / "Drelva-Vave-Manual.pdf"

INK = colors.HexColor("#14181d")
DIM = colors.HexColor("#5b6675")
RULE = colors.HexColor("#c9d0d9")
LIME = colors.HexColor("#5f8a2a")       # the house lime, darkened to read on paper
PAPER_LIME = colors.HexColor("#eef5e3")
NOTE = colors.HexColor("#fff6dd")

pdfmetrics.registerFont(TTFont("Michroma", str(ROOT / "assets" / "Michroma-Regular.ttf")))

S = {
    "title": ParagraphStyle("title", fontName="Michroma", fontSize=30, leading=36, textColor=INK),
    "sub": ParagraphStyle("sub", fontName="Helvetica", fontSize=12, leading=17, textColor=DIM),
    "h1": ParagraphStyle("h1", fontName="Michroma", fontSize=15, leading=20, textColor=INK, spaceBefore=6, spaceAfter=6),
    "h2": ParagraphStyle("h2", fontName="Michroma", fontSize=10.5, leading=14, textColor=LIME, spaceBefore=10, spaceAfter=4),
    "body": ParagraphStyle("body", fontName="Helvetica", fontSize=9.5, leading=13.5, textColor=INK, spaceAfter=5),
    "cell": ParagraphStyle("cell", fontName="Helvetica", fontSize=8.6, leading=11.2, textColor=INK),
    "cellb": ParagraphStyle("cellb", fontName="Helvetica-Bold", fontSize=8.6, leading=11.2, textColor=INK),
    "head": ParagraphStyle("head", fontName="Helvetica-Bold", fontSize=8, leading=10, textColor=DIM),
    "note": ParagraphStyle("note", fontName="Helvetica", fontSize=8.8, leading=12.2, textColor=INK),
    "cap": ParagraphStyle("cap", fontName="Helvetica", fontSize=7.8, leading=10, textColor=DIM, alignment=TA_CENTER),
}


# ---------------------------------------------------------------- screenshots
def shot(name, *actions):
    SHOTS.mkdir(parents=True, exist_ok=True)
    raw = SHOTS / f"{name}.rgb565"
    subprocess.run(["node", str(ROOT / "tools" / "shot.mjs"), str(raw), *actions], check=True)
    data = np.fromfile(raw, dtype=">u2").reshape(240, 240)
    rgb = np.stack([(data >> 11) * 255 // 31, ((data >> 5) & 63) * 255 // 63, (data & 31) * 255 // 31], -1).astype(np.uint8)
    png = SHOTS / f"{name}.png"
    Image.fromarray(rgb).resize((720, 720), Image.NEAREST).save(png)
    return png


def picture(name, *actions, size=44 * mm):
    return RLImage(str(shot(name, *actions)), width=size, height=size)


def captioned(image, text):
    return Table([[image], [Paragraph(text, S["cap"])]], style=TableStyle(
        [("ALIGN", (0, 0), (-1, -1), "CENTER"), ("TOPPADDING", (0, 0), (-1, -1), 1), ("BOTTOMPADDING", (0, 0), (-1, -1), 1)]))


# --------------------------------------------------------------------- pieces
def p(text):
    return Paragraph(text, S["body"])


def table(rows, widths, header=True):
    data = [[Paragraph(str(c), S["head"] if header and r == 0 else S["cellb"] if i == 0 else S["cell"])
             for i, c in enumerate(row)] for r, row in enumerate(rows)]
    style = [("VALIGN", (0, 0), (-1, -1), "TOP"), ("LINEBELOW", (0, 0), (-1, -1), 0.4, RULE),
             ("TOPPADDING", (0, 0), (-1, -1), 3), ("BOTTOMPADDING", (0, 0), (-1, -1), 3),
             ("LEFTPADDING", (0, 0), (-1, -1), 3), ("RIGHTPADDING", (0, 0), (-1, -1), 5)]
    if header:
        style.append(("LINEBELOW", (0, 0), (-1, 0), 0.8, DIM))
    return Table(data, colWidths=[w * mm for w in widths], style=TableStyle(style), repeatRows=1 if header else 0)


def note(text, colour=NOTE):
    return Table([[Paragraph(text, S["note"])]], colWidths=[170 * mm], style=TableStyle(
        [("BACKGROUND", (0, 0), (-1, -1), colour), ("BOX", (0, 0), (-1, -1), 0.4, RULE),
         ("LEFTPADDING", (0, 0), (-1, -1), 8), ("RIGHTPADDING", (0, 0), (-1, -1), 8),
         ("TOPPADDING", (0, 0), (-1, -1), 6), ("BOTTOMPADDING", (0, 0), (-1, -1), 6)]))


DEVICE = "<b>On the device.</b> "


def machine(title, name, actions, intro, pages, extra):
    """One machine: text and its screen side by side, then the pages table."""
    left = [Paragraph(title, S["h2"]), p(intro)] + [p(e) for e in extra]
    top = Table([[left, picture(name, *actions)]], colWidths=[122 * mm, 48 * mm],
                style=TableStyle([("VALIGN", (0, 0), (-1, -1), "TOP"), ("LEFTPADDING", (0, 0), (-1, -1), 0),
                                  ("RIGHTPADDING", (0, 0), (0, 0), 8), ("RIGHTPADDING", (1, 0), (1, 0), 0)]))
    rows = [["Page", "KNOB 1", "KNOB 2", "KNOB 3", "KNOB 4"]] + pages
    return KeepTogether([top, Spacer(1, 3), table(rows, [22, 37, 37, 37, 37]), Spacer(1, 8)])


class Panel(Flowable):
    """The FM-1 panel in the emulator's 1000 x 640 layout, with the Drelva Vave function of each control."""

    def __init__(self, width):
        super().__init__()
        self.width, self.height = width, width * 0.64

    def draw(self):
        c, k = self.canv, self.width / 1000.0

        def box(x, y, w, h, r=6, fill=None, stroke=RULE):
            c.setStrokeColor(stroke)
            c.setFillColor(fill or colors.white)
            c.roundRect(x * k, self.height - (y + h) * k, w * k, h * k, r * k, stroke=1, fill=1)

        def text(x, y, s, size=5.6, font="Helvetica", colour=INK):
            c.setFont(font, size)
            c.setFillColor(colour)
            c.drawCentredString(x * k, self.height - y * k, s)

        c.setLineWidth(0.6)
        box(0, 0, 1000, 640, 28, colors.HexColor("#f4f6f8"), DIM)
        box(300, 44, 236, 236, 20, colors.HexColor("#1b1f25"), INK)
        text(418, 150, "SCREEN", 7, "Michroma", colors.white)
        text(418, 172, "240 x 240", 5.5, "Helvetica", colors.HexColor("#aab3bf"))
        knobs = [(105, 100, "MASTER", "volume"), (215, 100, "SELECT", "track / preset slot"),
                 (105, 205, "PRESETS", "track level"), (215, 205, "ALGORITHM", "tempo")]
        knobs += [(610 + i * 104, 100, f"KNOB {i + 1}", f"parameter {i + 1}") for i in range(4)]
        for x, y, name, does in knobs:
            c.setStrokeColor(DIM)
            c.setFillColor(colors.white)
            c.circle(x * k, self.height - y * k, 22 * k, stroke=1, fill=1)
            text(x, y - 32, name, 5, "Helvetica", DIM)
            text(x, y + 40, does, 5.8, "Helvetica-Bold", LIME)
        buttons = [(72, 268, 82, 38, "OCT-", "page back"), (170, 268, 82, 38, "OCT+", "page on")]
        for i, (name, does) in enumerate(zip(["FX", "SCL", "ENV", "LFO", "EDIT", "GLO"], [f"track {n}" for n in range(1, 7)])):
            buttons.append((582 + i * 63, 174, 54, 48, name, does))
        for i, (name, does) in enumerate(zip(["HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC"],
                                             ["mute (hold)", "presets", "master", "action", "start/stop", "free"])):
            buttons.append((582 + i * 63, 236, 54, 48, name, does))
        for x, y, w, h, name, does in buttons:
            box(x, y, w, h, 7)
            text(x + w / 2, y + h / 2 - 4, name, 4.8, "Helvetica", DIM)
            text(x + w / 2, y + h / 2 + 11, does, 5, "Helvetica-Bold", LIME)
        pitch = (928 - 24) / 16
        kw = pitch * 0.78
        for i in range(16):
            box(48 + i * pitch + (pitch - kw) / 2, 340 + 270 * 0.43, kw, 270 * 0.52, 20)
        for a in [0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14]:
            box(48 + (a + 1) * pitch - kw / 2, 340 + 270 * 0.06, kw, 270 * 0.33, 20, colors.HexColor("#e3e7ec"))
        text(500, 628, "KEYS: go to the selected track's machine (transpose, gate). The first C plays a pattern as written.",
             5.8, "Helvetica-Bold", LIME)


# ----------------------------------------------------------------------- pages
def page_frame(canvas, doc):
    canvas.saveState()
    canvas.setFont("Michroma", 7)
    canvas.setFillColor(DIM)
    canvas.drawString(20 * mm, 12 * mm, "DRELVA VAVE")
    canvas.drawRightString(190 * mm, 12 * mm, str(doc.page))
    canvas.setStrokeColor(RULE)
    canvas.line(20 * mm, 16 * mm, 190 * mm, 16 * mm)
    canvas.restoreState()


def build():
    story = []
    add = story.append
    PLAY = ["b12"]

    # ---- cover
    add(Spacer(1, 40 * mm))
    add(Paragraph("DRELVA VAVE", S["title"]))
    add(Spacer(1, 4))
    add(Paragraph("Six machines, one screen. A Drelva Audio instrument for the M-VAVE FM-1.", S["sub"]))
    add(Spacer(1, 10 * mm))
    add(Table([[picture("cover_a", *PLAY, "b2", "r3.3", size=54 * mm), picture("cover_b", *PLAY, "b3", "r3.25", size=54 * mm),
                picture("cover_c", *PLAY, "b10", "r3.3", size=54 * mm)]], colWidths=[57 * mm] * 3))
    add(Spacer(1, 10 * mm))
    add(note("<b>Where this stands (10 November 2026).</b> Everything in this manual runs today in the browser "
             "emulator, and the screens shown are taken from it. It has not run on an FM-1 yet. Anything that needs the "
             "hardware (Bluetooth, the USB audio channels, MIDI clock out, the panel lights, presets kept in the "
             "unit's memory) is marked <b>On the device</b>: it is designed and has its setting, but it does nothing "
             "until the firmware is built for the FM-1. Those sections will be completed after the first hardware test."))
    add(Spacer(1, 6 * mm))
    add(p("Operating manual. Drelva Audio, 2026."))
    add(PageBreak())

    # ---- 1
    add(Paragraph("1. What it is", S["h1"]))
    add(p("Drelva Vave turns the FM-1 into a six-track techno instrument. Each track holds one <b>machine</b>: a "
          "sound engine with its own sequencer or pattern, playing by itself in time with the clock. You shape it "
          "with the four knobs under the screen, one page of four parameters at a time."))
    add(table([
        ["Part", "What it does"],
        ["6 tracks", "One machine each. Every track has its own level, mute, and sends to the delay and the reverb."],
        ["7 machines", "Elastico, Perc Box, Phrase Box, Sismo Box, Stab Box, Swarm Engine, Textura. Any machine can "
                       "be loaded on any track; each exists once."],
        ["Master", "Tempo-synced delay, reverb, a one-knob filter and a drive on the stereo mix."],
        ["Presets", "8 slots. A preset holds everything: which machine is on each track, every parameter, the mix, "
                    "the master and the tempo."],
        ["Clock", "Its own tempo (60 to 200 BPM), or it follows incoming MIDI clock."],
        ["USB audio", DEVICE + "Each track on its own channel, dry, plus the stereo mix with the effects."],
    ], [28, 142]))
    add(Spacer(1, 4))
    add(p("Tracks when you switch on: 1 Sismo Box, 2 Elastico, 3 Textura, 4 Swarm Engine, 5 Phrase Box, "
          "6 Perc Box. Stab Box is not on a track at first; load it where you want it (section 5)."))
    add(p("The switch-on settings are tuned to work together as techno at 134 BPM: everything sits in G (the kick "
          "at G1, the elastic line, the drone and the stabs on G), the kick is the loudest thing, "
          "the Master ducks the other tracks on every beat, and Phrase Box marks every 8 bars with a different "
          "transition."))

    # ---- 2
    add(Paragraph("2. The panel", S["h1"]))
    add(p("The names printed on the FM-1 belong to its original firmware. In grey: the printed name. "
          "In green: what the control does in Drelva Vave."))
    add(Panel(170 * mm))
    add(Spacer(1, 6))
    add(table([
        ["Control", "Does", "While PRESETS is open"],
        ["PLAY", "Start and stop. Starting always begins every pattern from its first step.", "the same"],
        ["FX SCL ENV LFO EDIT GLO", "Select track 1 to 6. With HOME held: mute or unmute that track.", "-"],
        ["ARP", "Master section. Again: System. Again: back to the track you were on.", "-"],
        ["SELECT", "Step through tracks 1 to 6, Master, System.", "Pick the slot, 1 to 8"],
        ["OCT- / OCT+", "Previous / next page of what is selected.", "OCT- closes; OCT+ loads the slot"],
        ["KNOB 1 to 4", "The four parameters shown on the page. Turn fast to move in bigger steps.", "the same"],
        ["ALGORITHM", "Tempo, 60 to 200 BPM. Does nothing while an outside clock is followed.", "the same"],
        ["PRESETS", "Level of the selected track.", "the same"],
        ["SEQ", "The machine's action: a new pattern, or a hit (see each machine).", "-"],
        ["SAVE", "Opens Presets.", "Stores into the slot and closes"],
        ["HOME", "Held with a track button: mute.", "-"],
        ["REC", "Not used.", "-"],
        ["MASTER", "Output volume.", "the same"],
        ["Keys", "Sent to the selected track's machine.", "the same"],
    ], [38, 82, 50]))

    # ---- 3
    add(Paragraph("3. The screen", S["h1"]))
    screen = picture("screen", *PLAY, "b2", "r3.3", size=62 * mm)
    add(Table([[screen, table([
        ["Area", "Shows"],
        ["Top line", "Name of the selected machine. Right: tempo. <b>BPM</b> means its own clock; <b>EXT</b> in green "
                     "means it is following an outside clock and the number is the measured tempo. After a preset is "
                     "stored or loaded the name is replaced for a moment by SAVED, LOADED or EMPTY."],
        ["Four bars", "The beat. The lit bar is the current quarter of the bar."],
        ["1 to 6", "The tracks. Filled: selected. The small bar is its level meter. <b>M</b>: muted. All six outlined "
                   "in green: Master or System is selected."],
        ["Picture", "The machine's own display: its pattern and where it is playing."],
        ["Page line", "Page name, one mark per page (the lit one is where you are), and the track's level."],
        ["Four dials", "The parameters of KNOB 1 to 4: value in the middle, name below. A dial that goes below zero "
                       "grows from the top. A dark dial has no parameter."],
    ], [22, 80], header=True)]], colWidths=[66 * mm, 104 * mm], style=TableStyle([("VALIGN", (0, 0), (-1, -1), "TOP")])))

    # ---- 4
    add(Paragraph("4. Playing", S["h1"]))
    add(table([
        ["To", "Do this"],
        ["Start", "Press PLAY. All machines start together on step 1."],
        ["Work on a track", "Press its button (FX = 1 ... GLO = 6) or turn SELECT."],
        ["Change the sound", "Turn KNOB 1 to 4. OCT+ and OCT- move through the machine's pages."],
        ["Mute", "Hold HOME and press the track's button. Again to unmute."],
        ["Set a level", "Turn PRESETS for the selected track, or use LEVEL on its MIX page."],
        ["Change tempo", "Turn ALGORITHM."],
        ["Get a new pattern", "Press SEQ on Elastico or Sismo Box. On Phrase Box and Stab Box, SEQ fires a hit."],
        ["Change key", "Select a melodic track and press a key. The first C (eighth key from the left) is the "
                       "pattern as written; other keys move it up or down by semitones. (On the two "
                       "STAGE pages of Elastico the white keys pick a stage instead.)"],
    ], [38, 132]))
    add(Spacer(1, 4))
    add(note("Each machine keeps what it is doing when you look at another track. Knobs only ever act on what the "
             "screen shows.", PAPER_LIME))

    # ---- 5
    add(Paragraph("5. The MIX page and loading machines", S["h1"]))
    add(Table([[[p("The last page of every track is <b>MIX</b>. It is the same on all six."),
                 table([
                     ["Knob", "Does"],
                     ["LEVEL", "Track level (the same value PRESETS turns)."],
                     ["DELAY", "How much of the track goes to the master delay."],
                     ["REVERB", "How much goes to the master reverb."],
                     ["LOAD", "Which machine the track holds. Turn it to load another one, or NONE to empty the track."],
                 ], [22, 96]),
                 Spacer(1, 4),
                 p("Each machine exists once. If you load a machine that another track is holding, the two tracks "
                   "<b>change places</b>: the other track gets the machine you had. Nothing is lost, and every "
                   "machine keeps its settings while it is not on a track."),
                 p("Order on the dial: NONE, ELAST, PERC, PHRASE, SISMO, STAB, SWARM, TEXTUR.")],
                picture("mix", *PLAY, "b4", "b1", "b1", "r2")]], colWidths=[122 * mm, 48 * mm],
              style=TableStyle([("VALIGN", (0, 0), (-1, -1), "TOP"), ("LEFTPADDING", (0, 0), (-1, -1), 0),
                               ("RIGHTPADDING", (0, 0), (0, 0), 8)])))
    add(PageBreak())

    # ---- 6
    add(Paragraph("6. The machines", S["h1"]))
    add(p("In alphabetical order. Each table lists the pages in the order OCT+ reaches them; MIX always comes after."))

    add(machine("Elastico", "elastico", PLAY + ["b3", "r3.25"],
                "The elastic voice of Parkoid without its click, for hypnotic lines. An FM pair into a filter, "
                "bent by a spring: every note plucks the spring, and its swing stretches the pitch and, with "
                "FLEX, the brightness, then snaps back and settles. The spring starts from rest and is pushed, "
                "so the pitch leaves the note smoothly instead of jumping, and the level fades in over SOFT: "
                "that is why it does not click. It is played by an eight-stage sequencer in the manner of the "
                "ML-185.",
                [["ELASTIC", "STRETCH how far the pitch swings, in semitones; negative swings down first",
                  "SPEED how fast the spring swings", "DAMP how soon it comes to rest", "FLEX how much the swing "
                  "also opens the tone"],
                 ["TONE", "RATIO of the FM pair: 0.5 to 7", "INDEX FM amount", "FOLD wavefolder", "CUTOFF"],
                 ["SHAPE", "SOFT attack, from sharp to 60 ms", "DECAY", "RESO", "GLIDE between notes"],
                 ["MOVE", "DRIFT slow change of FM amount and filter", "RATE of the drift: 4, 8, 16 or 32 bars",
                  "TUNE semitones", "-"],
                 ["STAGE", "STAGE which stage you are editing, 1 to 8", "PITCH semitones, -12 to +24",
                  "PULSES how long the stage lasts, 1 to 8", "GATE Rest, Single, Multi, Hold"],
                 ["STAGE 2", "STAGE", "SLIDE the note glides in without a new attack", "ACCENT a harder pluck",
                  "SKIP the stage is passed over"],
                 ["SEQ", "LAST how many stages play, 1 to 8", "MODE Fwd, Rev, Pend, Random, Drunk",
                  "DIV a pulse is 1/16, 1/8 or 1/4", "GATE note length, % of a pulse"],
                 ["GEN", "SCALE Parker, minor, Phrygian, pentatonic, octaves", "OCTS 1 or 2",
                  "SLIDE % chance per stage", "ACCENT% chance per stage"]],
                ["<b>Gate modes.</b> REST: silent for all its pulses. SINGLE: one note on the first pulse. MULTI: "
                 "a note on every pulse. HOLD: one note held through all the pulses.",
                 "<b>MODE.</b> FWD 1 to LAST. REV backwards. PEND there and back. RANDOM any stage. DRUNK mostly "
                 "forward, sometimes staying or stepping back.",
                 "<b>Keys.</b> On the STAGE pages the first eight white keys pick the stage to edit. On every "
                 "other page the keys transpose. <b>SEQ</b> rolls a whole new pattern using the GEN page; it "
                 "replaces all eight stages, which you can then edit. Presets keep the stages. The PARKER scale "
                 "is mostly the root, with the odd octave, fifth, flat seventh and flat third: a figure rather "
                 "than a tune."]))
    add(note("<b>Reading the picture.</b> Eight columns, one per stage. Left of each: the pitch slider (a white "
             "cap is an accent). Right: eight lamps, the first pulse at the bottom; lit lamps are the stage's "
             "pulses, bright where a note starts, joined for HOLD, hollow for REST. A line under a stage is a "
             "SLIDE. Greyed stages are skipped or beyond LAST. The frame marks the stage being edited; the "
             "white lamp is the pulse playing now, and the cap of the playing stage rides the spring.", PAPER_LIME))
    add(Spacer(1, 4))
    add(note("<b>Starting points.</b> Rubber: STRETCH 9, SPEED 45, DAMP 40. Slow wobble under the beat: SPEED 15, "
             "DAMP 20. A tight zip: STRETCH 18, SPEED 80, DAMP 75. Falling instead of rising: negative STRETCH. "
             "For a figure that never repeats with the bar, set LAST to 5 or 7.", PAPER_LIME))
    add(Spacer(1, 8))

    add(machine("Perc Box", "perc", PLAY + ["b7", "r3.3"],
                "Hat, clap and a metallic FM perc in one track. Each has its own Euclidean pattern: HITS are spread "
                "as evenly as 16 steps allow, and ROTATE turns the pattern round the bar.",
                [["HAT", "HITS 0 to 16", "ROTATE 0 to 15", "DECAY", "OPEN how long the off-beat hats ring"],
                 ["CLAP", "HITS", "ROTATE", "DECAY tail", "TONE"],
                 ["PERC", "HITS", "ROTATE", "PITCH semitones", "DECAY"],
                 ["SHAPE", "HAT TON brightness", "FM amount", "RATIO 1.41, 2.00, 2.76, 3.50", "ACCENT how much the "
                  "strong steps stand out"],
                 ["LEVELS", "HAT", "CLAP", "PERC", "-"]],
                ["<b>Picture:</b> three rows of 16 steps, H, C and P. White: playing now.",
                 "Useful starts: hat 4 hits rotated 2 is the off-beat; 16 hits is straight sixteenths. Clap 2 hits "
                 "rotated 4 falls on beats 2 and 4. SEQ and the keys do nothing here."]))

    add(machine("Phrase Box", "phrase", PLAY + ["b6", "r6.3"],
                "Counts phrases and plays a transition at every phrase line: a build before it, a hit on it, a fall "
                "after it. Leave it running and the track marks its own sections.",
                [["PHRASE", "PHRASE length in steps: 16, 32, 64, 128, 256", "FX which transition", "LEAD how many "
                  "steps the build and the fall take: 4 to 64", "RNDLEN with VARY: also vary the lead"],
                 ["RISE", "RESO of the sweep", "TONE noise to saws", "PITCH semitones", "TAIL of the hit"],
                 ["HIT", "CRASH noise in the hit", "DRIVE", "-", "-"]],
                ["<b>FX:</b> HIT, RISE, R+HIT, ROLL (snare roll), DOWN (downlifter), SWELL (reverse cymbal), SIREN, "
                 "KICKS (kick roll), ZAP, SUB (sub drop), ALL, or VARY, which picks a new combination every phrase.",
                 "<b>Picture:</b> the next transition by name, the step count, and the phrase as a bar with a mark "
                 "where the build starts. <b>SEQ:</b> a hit now."]))

    add(machine("Sismo Box", "sismo", PLAY + ["b2", "r3.3"],
                "Kick and rumble, after the Sismo plugin. A synthesized kick on every beat, and under it a low "
                "burst on every sixteenth, shaped by a groove, filtered, saturated and ducked by the kick.",
                [["RUMBLE", "RUMBLE level", "GROOVE Roll, Offbeat, Gallop, Triplet, Warehouse, or GEN (generated)",
                  "WEIGHT how dark or open", "GRIT saturation"],
                 ["KICK", "PUMP how hard the kick ducks the rumble", "PITCH semitones", "DECAY of the kick",
                  "TAIL of the rumble"],
                 ["PUNCH", "CLICK attack", "SNAP depth of the pitch drop", "SEED which generated groove", "-"]],
                ["<b>Picture:</b> the groove, 16 bars grouped by beat; a dot under each kick. White: playing now.",
                 "<b>SEQ:</b> switches GROOVE to GEN and rolls the next one. The keys do nothing here."]))

    add(machine("Stab Box", "stab", PLAY + ["b4", "b1", "b1", "e5:-2", "b0", "b0", "b0", "r3.3"],
                "Dub techno chord stabs: four saws on a chord, a resonant filter that pings open on each stab. "
                "Not on a track at switch-on; load it with LOAD. It wants the delay: turn up DELAY on its MIX page.",
                [["CHORD", "CHORD Min, Min7, Min9, Dub, Sus4, 5th, Min11", "RHYTHM Off, Dub, Push, 3/16, One, Man "
                  "(only SEQ)", "PROG chord per bar: Stay, i-VI, i-iv, i-VII, Fall", "ROOT semitones"],
                 ["FILTER", "CUTOFF", "ENV how far the stab opens it", "DECAY", "RESO"],
                 ["VOICE", "DETUNE", "-", "-", "-"]],
                ["<b>Picture:</b> the chord as four bars, the four bars of the progression, and the rhythm.",
                 "<b>SEQ:</b> a stab now. <b>Keys:</b> move the root."]))

    add(machine("Swarm Engine", "swarm", PLAY + ["b5", "r3.3"],
                "A drone of 16 oscillators: four voices at an interval, each a centre oscillator and three detuned "
                "ones.",
                [["SWARM", "TUNE semitones", "SPREAD detune", "INTVL unison, octaves, fifths, fourths, cluster",
                  "WAVE saw, pulse, fold, XOR"],
                 ["SHAPE", "FOLD (for WAVE fold)", "MODE Play, Keys, Sync", "TONE low-pass on the whole swarm",
                  "MOVE slow sweep of TONE, one turn every four bars"]],
                ["<b>MODE:</b> PLAY sounds while the transport runs. KEYS sounds only while a key is held. SYNC is "
                 "PLAY with every oscillator restarted on each beat.",
                 "<b>Picture:</b> one ring per voice with its four oscillators going round. <b>Keys:</b> set the pitch."]))

    add(machine("Textura", "textura", PLAY + ["b4", "r3.3"],
                "The air around the groove: a band of noise whose colour drifts, a low hum and some crackle, all "
                "pushed down on every beat and rising back, so it breathes with the kick.",
                [["TEXTURE", "COLOR where the band sits, low to high", "WIDTH wide and soft to narrow and whistling",
                  "DRIFT how far the colour wanders", "RATE how fast"],
                 ["MOTION", "PUMP how far each beat pushes it down", "RELEASE how slowly it comes back",
                  "CRACKLE", "HUM"]],
                ["It sounds while the transport runs, and while a key is held. <b>Picture:</b> the band, low to "
                 "high, sinking on each beat."]))
    add(PageBreak())

    # ---- 7
    add(Paragraph("7. Master", S["h1"]))
    add(Table([[[p("Press <b>ARP</b>. The Master works like a track: three pages, four knobs. Only the stereo mix "
                   "goes through it; the separate track outputs stay dry."),
                 table([
                     ["Page", "KNOB 1", "KNOB 2", "KNOB 3", "KNOB 4"],
                     ["DELAY", "TIME 1/16, 1/8, 3/16, 1/4, locked to the tempo", "FEEDBK repeats",
                      "TONE how dark the repeats get", "RETURN delay level"],
                     ["REVERB", "SIZE", "DAMP", "RETURN reverb level", "-"],
                     ["OUTPUT", "FILTER left of centre closes a low-pass; right opens a high-pass; centre is off",
                      "DRIVE", "PUMP how far each beat ducks every track except the one holding Sismo Box", "-"],
                 ], [18, 27, 24, 27, 24]),
                 Spacer(1, 4),
                 p("What reaches the delay and the reverb is set per track, on its MIX page. With all sends at 0 "
                   "the effects are silent whatever RETURN says. PUMP is the sidechain of a techno mix; it acts on "
                   "the stereo mix only, so the separate track outputs are not ducked. The picture shows the six track meters and, on "
                   "the right, the left and right output.")],
                picture("master", *PLAY, "b10", "r3.3")]], colWidths=[122 * mm, 48 * mm],
              style=TableStyle([("VALIGN", (0, 0), (-1, -1), "TOP"), ("LEFTPADDING", (0, 0), (-1, -1), 0),
                               ("RIGHTPADDING", (0, 0), (0, 0), 8)])))

    # ---- 8
    add(Paragraph("8. Presets", S["h1"]))
    add(Table([[[table([
        ["To", "Do this"],
        ["Open", "Press SAVE. The eight slots appear; a dot marks the ones in use."],
        ["Pick a slot", "Turn SELECT."],
        ["Store", "Press SAVE again. The top line says SAVED. A slot in use is overwritten without asking."],
        ["Load", "Press OCT+. The top line says LOADED, or EMPTY if the slot has nothing."],
        ["Leave", "Press OCT-."],
    ], [24, 94]),
        Spacer(1, 4),
        p("A preset holds: the machine on each track, every parameter of those machines, level, sends and mute "
          "of each track, the Master, and the tempo. It does not hold the System settings."),
        p("Loading while playing is fine; patterns carry on from where they are."),
        p("In the emulator, presets are kept by the browser on that computer. " + DEVICE +
          "They will be kept in the FM-1's memory; that part is not written yet.")],
        picture("presets", "b9", "e0:1", "b9", "b12", "b9", "r1")]], colWidths=[122 * mm, 48 * mm],
        style=TableStyle([("VALIGN", (0, 0), (-1, -1), "TOP"), ("LEFTPADDING", (0, 0), (-1, -1), 0),
                               ("RIGHTPADDING", (0, 0), (0, 0), 8)])))
    add(PageBreak())

    # ---- 9
    add(Paragraph("9. System settings", S["h1"]))
    add(p("Press <b>ARP twice</b> (once for Master, again for System), or turn SELECT all the way right. These "
          "settings belong to the instrument, not to a preset. They are written to memory when you leave System."))
    add(Table([[captioned(picture("system1", "b10", "b10", "r0.5", size=52 * mm), "CLOCK"),
                captioned(picture("system2", "b10", "b10", "b1", "r0.5", size=52 * mm), "LINK"),
                captioned(picture("system3", "b10", "b10", "b1", "b1", "r0.5", size=52 * mm), "PANEL")]],
              colWidths=[56 * mm] * 3))
    add(Spacer(1, 6))
    add(table([
        ["Page", "Setting", "Values", "What it does"],
        ["CLOCK", "SOURCE", "AUTO, INT", "AUTO follows MIDI clock whenever it arrives and goes back to its own tempo "
                                         "one second after it stops. INT ignores outside clock. Works now."],
        ["", "OFFSET", "-40 to +40 ms", "Moves the instrument against the clock it follows. Negative plays earlier: "
                                        "use it to take out the delay of a Bluetooth link. Works now."],
        ["", "SEND", "OFF, ON", DEVICE + "Sends its own MIDI clock, Start and Stop."],
        ["LINK", "BT MIDI", "OFF, ON", DEVICE + "Switches Bluetooth MIDI on. This is where Bluetooth is turned on; "
                                       "see section 10."],
        ["", "USB OUT", "STEREO, TRACKS", DEVICE + "What the computer sees: the stereo mix only, or six tracks plus "
                                          "the mix (8 channels). See section 11."],
        ["", "BITS", "16, 24", DEVICE + "Bit depth of the USB audio."],
        ["PANEL", "BRIGHT", "10 to 100", "Screen brightness. The emulator dims its screen with it."],
        ["", "KEY LED", "OFF, ON", DEVICE + "Lights in the keys."],
    ], [16, 20, 28, 106]))
    add(Spacer(1, 4))
    add(p("The picture on this screen is a status board: whether the clock is its own or followed, what the USB "
          "output is set to, and whether Bluetooth MIDI is on."))

    # ---- 10
    add(Paragraph("10. Clock and Bluetooth", S["h1"]))
    add(p("With SOURCE on AUTO, Drelva Vave follows MIDI clock from any input. The tempo display turns green and "
          "reads EXT. MIDI Start makes it play from step 1, Stop stops it, Continue carries on. Its own PLAY still "
          "works. ALGORITHM is ignored while it follows."))
    add(p("A Bluetooth clock does not arrive evenly. Drelva Vave does not play each tick as it comes: it measures "
          "the tempo over the last two beats, runs its own steady clock at that tempo, and corrects itself gently "
          "towards the master. Measured in the emulator with a simulated uneven clock:"))
    add(table([
        ["Master tempo", "Ticks late by up to", "Kick wanders by", "Average lateness"],
        ["134 BPM", "0 ms", "0.15 ms", "1.3 ms"],
        ["126 BPM", "10 ms", "0.9 ms", "6 ms"],
        ["140 BPM", "20 ms", "1.6 ms", "11 ms"],
        ["90 BPM", "30 ms", "2.2 ms", "15 ms"],
    ], [34, 44, 44, 48]))
    add(Spacer(1, 4))
    add(p("The average lateness is about half the unevenness, and it is steady, so OFFSET removes it: for a link "
          "that is up to 20 ms late, try OFFSET -10. With a very uneven clock the first two or three beats after "
          "Start can be off by a few tens of milliseconds before it settles."))
    add(note(DEVICE + "<b>Bluetooth is not active yet.</b> The plan: set LINK &gt; BT MIDI to ON; the FM-1 then "
             "appears as a Bluetooth MIDI device; connect to it from the sending device's Bluetooth MIDI list "
             "(on a Mac: Audio MIDI Setup; on an iPhone or iPad: the Bluetooth MIDI screen of the app). The base "
             "this firmware is built on has been used that way with a Mac and an iPhone by its author; a Windows "
             "PC has not been tried. The exact pairing steps, and what the real unevenness of the link is, will be "
             "written here after the first test on hardware. Until then, clock can be tried in the emulator "
             "through the MIDI in menu under the drawn panel."))

    # ---- 11
    add(Paragraph("11. USB audio", S["h1"]))
    add(note(DEVICE + "Not active yet. This is the layout the firmware is designed to present, with USB OUT on TRACKS."))
    add(Spacer(1, 4))
    add(table([
        ["Channel", "Carries"],
        ["1 to 6", "Tracks 1 to 6, mono, after the track's level and mute, without delay, reverb, filter or drive."],
        ["7 and 8", "The stereo mix, left and right, with the Master effects: what the headphone output plays."],
    ], [24, 146]))
    add(Spacer(1, 4))
    add(p("The channels follow the tracks, not the machines: if you load another machine on track 3, channel 3 "
          "carries it. In a DAW, make six mono audio tracks with inputs 1 to 6, and one stereo track on 7/8 if you "
          "want the effects. Do not monitor 7/8 together with 1 to 6, or the dry sound is heard twice."))
    add(p("On Windows a device with more than two channels usually needs an ASIO layer for Ableton Live to see "
          "them all (ASIO4ALL or FlexASIO). How many channels Windows accepts from the FM-1 without a driver is "
          "not known yet; if it is fewer than eight, the fallback is the stereo mix over USB and recording tracks "
          "one at a time."))

    # ---- 12
    add(Paragraph("12. Quick reference", S["h1"]))
    add(table([
        ["Printed on the FM-1", "Drelva Vave", "Printed on the FM-1", "Drelva Vave"],
        ["PLAY", "Start / stop", "KNOB 1 to 4", "Page parameters"],
        ["FX", "Track 1", "SELECT", "Track / preset slot"],
        ["SCL", "Track 2", "ALGORITHM", "Tempo"],
        ["ENV", "Track 3", "PRESETS", "Track level"],
        ["LFO", "Track 4", "MASTER", "Volume"],
        ["EDIT", "Track 5", "OCT-", "Page back / close presets"],
        ["GLO", "Track 6", "OCT+", "Page on / load preset"],
        ["HOME + track", "Mute", "SAVE", "Presets / store"],
        ["ARP", "Master, then System", "SEQ", "New pattern / hit"],
        ["REC", "Not used", "Keys", "Transpose / gate"],
    ], [38, 47, 38, 47]))

    # ---- 13
    add(Paragraph("13. Credits and licence", S["h1"]))
    add(p("Drelva Vave is free software under the GNU General Public License, version 3. Whoever gives you the "
          "firmware must also offer you its source code."))
    add(p("Its hardware layer is built on Melodee by Kerem Kilic (Ellic Studio), itself based on Felucca by "
          "Leo Kuroshita and Hugelton Instruments. The page of the browser emulator is adapted from Melodee's, "
          "with the web emulator build by David Konsumer. The machines come from Drelva Audio's Drelvarom "
          "firmware, except three new ones: Elastico (after the elastic voice of the Parkoid plugin), Perc Box and "
          "Textura. The typeface is Michroma (SIL Open Font License)."))
    add(p("M-VAVE and FM-1 are trademarks of their owner. Drelva Vave is not made, endorsed or supported by "
          "M-VAVE. Installing other firmware on an FM-1 is at your own risk: the unit has a single firmware area, "
          "and an update that is interrupted can leave it unable to start."))

    OUT.parent.mkdir(parents=True, exist_ok=True)
    doc = BaseDocTemplate(str(OUT), pagesize=A4, leftMargin=20 * mm, rightMargin=20 * mm, topMargin=18 * mm,
                          bottomMargin=22 * mm, title="Drelva Vave - Manual", author="Drelva Audio")
    doc.addPageTemplates(PageTemplate(id="page", frames=[Frame(20 * mm, 22 * mm, 170 * mm, 257 * mm, id="main",
                                                               leftPadding=0, rightPadding=0, topPadding=0, bottomPadding=0)],
                                      onPage=page_frame))
    doc.build(story)
    print(OUT)


if __name__ == "__main__":
    build()
