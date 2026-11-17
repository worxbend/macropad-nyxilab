#!/usr/bin/env python3
"""Illustrated build guide for the Nyxilab macropad.

Writes docs/nyxilab-build-guide.pdf. Facts follow docs/printing.md,
docs/assembly.md, docs/soldering.md, docs/wiring.md and docs/build-guide.md.
Regenerate after a BOM, pin, or print-setting change:

    .venv/bin/pip install reportlab
    python3 tools/build_manual.py
"""

from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
IMG = REPO / "docs" / "images"
OUT = REPO / "docs" / "nyxilab-build-guide.pdf"
STEP_COUNT = 17

NIGHT = "#12111a"
VIOLET = "#6a46c9"
VIOLET_SOFT = "#9b7bf0"
INK = "#1d1d1f"
SOFT = "#6e6e73"
LINE = "#e4e2ea"
PAPER = "#f7f6f8"
WARN_BG = "#fff6e5"
WARN = "#8a5a00"
STOP_BG = "#fdecec"
STOP = "#9b2c2c"
TEST_BG = "#e7f6ee"
TEST = "#1b6b45"
NOTE_BG = "#f3eefe"


def require_reportlab():
    try:
        import reportlab  # noqa: F401
    except ImportError:
        sys.exit("reportlab is not installed. Run: .venv/bin/pip install reportlab")


def build() -> int:
    require_reportlab()
    from reportlab.lib.colors import HexColor, white
    from reportlab.lib.enums import TA_CENTER, TA_LEFT
    from reportlab.lib.pagesizes import A4
    from reportlab.lib.styles import ParagraphStyle
    from reportlab.lib.units import mm
    from reportlab.pdfbase import pdfmetrics
    from reportlab.pdfbase.ttfonts import TTFont
    from reportlab.pdfgen import canvas as pdfcanvas
    from reportlab.platypus import (
        BaseDocTemplate,
        CondPageBreak,
        Flowable,
        Frame,
        HRFlowable,
        Image,
        KeepTogether,
        PageTemplate,
        Paragraph,
        Spacer,
        Table,
        TableStyle,
    )
    from reportlab.platypus.tableofcontents import TableOfContents

    pdfmetrics.registerFont(TTFont("DejaVu", "/usr/share/fonts/TTF/DejaVuSans.ttf"))
    pdfmetrics.registerFont(TTFont("DejaVuBold", "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf"))
    pdfmetrics.registerFont(TTFont("DejaVuOblique", "/usr/share/fonts/TTF/DejaVuSans-Oblique.ttf"))
    pdfmetrics.registerFontFamily(
        "DejaVu", normal="DejaVu", bold="DejaVuBold", italic="DejaVuOblique", boldItalic="DejaVuBold"
    )

    page_w, page_h = A4
    left, right, top, bottom = 14 * mm, 14 * mm, 16 * mm, 14 * mm
    content_w = page_w - left - right
    frame_h = page_h - top - bottom
    violet = HexColor(VIOLET)
    ink = HexColor(INK)
    soft = HexColor(SOFT)
    line = HexColor(LINE)

    styles = {
        "H1": ParagraphStyle(
            "H1", fontName="DejaVuBold", fontSize=20, leading=24, textColor=ink,
            spaceBefore=0, spaceAfter=2 * mm, keepWithNext=True,
        ),
        "H2": ParagraphStyle(
            "H2", fontName="DejaVuBold", fontSize=12, leading=15, textColor=violet,
            spaceBefore=3.5 * mm, spaceAfter=1.5 * mm, keepWithNext=True,
        ),
        "Body": ParagraphStyle(
            "Body", fontName="DejaVu", fontSize=9.5, leading=13, textColor=ink, spaceAfter=1.6 * mm,
        ),
        "Lede": ParagraphStyle(
            "Lede", fontName="DejaVu", fontSize=10.5, leading=14.5, textColor=ink, spaceAfter=2 * mm,
        ),
        "Step": ParagraphStyle(
            "Step", fontName="DejaVu", fontSize=10.5, leading=13.5, textColor=ink, spaceBefore=0, spaceAfter=0,
        ),
        "Td": ParagraphStyle("Td", fontName="DejaVu", fontSize=8, leading=10.4, textColor=ink),
        "Th": ParagraphStyle("Th", fontName="DejaVuBold", fontSize=8, leading=10.4, textColor=ink),
        "Cap": ParagraphStyle(
            "Cap", fontName="DejaVuOblique", fontSize=8, leading=10.5, textColor=soft,
            alignment=TA_CENTER, spaceBefore=1 * mm, spaceAfter=2.5 * mm,
        ),
        "CallTitle": ParagraphStyle(
            "CallTitle", fontName="DejaVuBold", fontSize=8.5, leading=11, textColor=ink, spaceAfter=0.6 * mm,
        ),
        "CallBody": ParagraphStyle("CallBody", fontName="DejaVu", fontSize=8.5, leading=11.2, textColor=ink),
        "Badge": ParagraphStyle(
            "Badge", fontName="DejaVuBold", fontSize=8, leading=10, textColor=white, alignment=TA_CENTER,
        ),
        "Footer": ParagraphStyle("Footer", fontName="DejaVu", fontSize=8, leading=10, textColor=soft),
        "TOC": ParagraphStyle("TOC", fontName="DejaVu", fontSize=11, leading=18, textColor=ink),
    }

    class TocMark(Flowable):
        def __init__(self, key: str, title: str):
            super().__init__()
            self.key = key
            self.title = title
            self.width = 0
            self.height = 0.1

        def wrap(self, aw, ah):
            return (0, 0.1)

        def draw(self):
            return

    class CoverPage(Flowable):
        def __init__(self):
            super().__init__()
            self.width = content_w
            self.height = frame_h - 1

        def draw(self):
            return

    class PhaseStrip(Flowable):
        def __init__(self):
            super().__init__()
            self.width = content_w
            self.height = 16 * mm

        def draw(self):
            labels = ("1  Print", "2  Plate", "3  Solder", "4  Flash", "5  Close")
            gap = 2 * mm
            box_w = (content_w - gap * 4) / 5
            c = self.canv
            for i, label in enumerate(labels):
                x = i * (box_w + gap)
                c.setFillColor(HexColor(NOTE_BG))
                c.roundRect(x, 0, box_w, 12 * mm, 1.6 * mm, fill=1, stroke=0)
                c.setFillColor(violet)
                c.rect(x, 0, box_w, 1.1 * mm, fill=1, stroke=0)
                c.setFillColor(ink)
                c.setFont("DejaVuBold", 8)
                c.drawCentredString(x + box_w / 2, 4.6 * mm, label)

    class BootKeys(Flowable):
        """Top view of the 12 keys. USB is at the back (top of the drawing)."""

        def __init__(self):
            super().__init__()
            self.width = content_w
            self.height = 62 * mm

        def draw(self):
            c = self.canv
            key, gap = 11 * mm, 2.2 * mm
            col_x = [0, key + gap, 2 * (key + gap)]
            # Shift the block toward the centre of the text column.
            origin_x = 8 * mm
            origin_y = 6 * mm
            marks = {
                (0, 0): ("BOOT", True),
                (0, 3): ("FN", True),
                (2, 3): ("PAIR", True),
            }
            c.setFillColor(soft)
            c.setFont("DejaVu", 7.5)
            c.drawString(origin_x, origin_y + 4 * (key + gap) + 1.2 * mm, "Back, USB side")
            for col in range(3):
                for row in range(4):
                    # Row 0 is the back row, drawn at the top.
                    x = origin_x + col_x[col]
                    y = origin_y + (3 - row) * (key + gap)
                    on = (col, row) in marks
                    c.setFillColor(violet if on else HexColor("#f3f2f6"))
                    c.setStrokeColor(violet if on else line)
                    c.setLineWidth(0.6)
                    c.roundRect(x, y, key, key, 1.3 * mm, fill=1, stroke=1)
                    c.setFillColor(white if on else ink)
                    c.setFont("DejaVuBold", 6.5)
                    text = marks.get((col, row), ("", False))[0]
                    if text:
                        c.drawCentredString(x + key / 2, y + key / 2 - 2, text)
            note_x = origin_x + 3 * (key + gap) + 6 * mm
            c.setFillColor(ink)
            c.setFont("DejaVu", 8)
            lines = (
                (note_x, origin_y + 46 * mm, "Left column is column 0,"),
                (note_x, origin_y + 42 * mm, "next to the bar screen."),
                (note_x, origin_y + 36 * mm, "Plug-in boot: hold BOOT"),
                (note_x, origin_y + 32 * mm, "and PAIR, then plug in."),
                (note_x, origin_y + 26 * mm, "FN + BOOT: hold FN,"),
                (note_x, origin_y + 22 * mm, "press BOOT."),
                (note_x, origin_y + 16 * mm, "Front edge is toward you."),
            )
            for x, y, text in lines:
                c.drawString(x, y, text)

    def P(text: str, style: str = "Body") -> Paragraph:
        return Paragraph(text, styles[style])

    def chapter(key: str, title: str, lede: str, keep: float = 80 * mm) -> list:
        return [
            CondPageBreak(keep),
            TocMark(key, title),
            P(title, "H1"),
            HRFlowable(width="100%", thickness=1.25, color=violet, spaceBefore=0, spaceAfter=3 * mm),
            P(lede, "Lede"),
        ]

    def h2(text: str) -> Paragraph:
        return P(text, "H2")

    def cells(row: list[str], header: bool = False) -> list:
        style = "Th" if header else "Td"
        return [P(item, style) for item in row]

    def grid(headers: list[str], rows: list[list[str]], widths: list[float]) -> Table:
        data = [cells(headers, header=True)] + [cells(row) for row in rows]
        table = Table(data, colWidths=widths, repeatRows=1)
        table.setStyle(TableStyle([
            ("BACKGROUND", (0, 0), (-1, 0), HexColor(NOTE_BG)),
            ("TEXTCOLOR", (0, 0), (-1, 0), ink),
            ("LINEBELOW", (0, 0), (-1, 0), 1, violet),
            ("LINEBELOW", (0, 1), (-1, -2), 0.3, line),
            ("VALIGN", (0, 0), (-1, -1), "TOP"),
            ("LEFTPADDING", (0, 0), (-1, -1), 3),
            ("RIGHTPADDING", (0, 0), (-1, -1), 3),
            ("TOPPADDING", (0, 0), (-1, -1), 2.5),
            ("BOTTOMPADDING", (0, 0), (-1, -1), 2.5),
        ]))
        return table

    def callout(kind: str, title: str, body: str) -> Table:
        palette = {
            "test": (TEST_BG, TEST),
            "stop": (STOP_BG, STOP),
            "warn": (WARN_BG, WARN),
            "note": (NOTE_BG, VIOLET),
        }
        bg, accent = palette[kind]
        inner = [P(title, "CallTitle"), P(body, "CallBody")]
        table = Table([[inner]], colWidths=[content_w])
        table.setStyle(TableStyle([
            ("BACKGROUND", (0, 0), (-1, -1), HexColor(bg)),
            ("LINEBEFORE", (0, 0), (0, 0), 3, HexColor(accent)),
            ("LEFTPADDING", (0, 0), (-1, -1), 8),
            ("RIGHTPADDING", (0, 0), (-1, -1), 8),
            ("TOPPADDING", (0, 0), (-1, -1), 6),
            ("BOTTOMPADDING", (0, 0), (-1, -1), 6),
            ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ]))
        return table

    def figure(path: Path, caption: str, max_height: float = 108 * mm) -> list:
        if not path.is_file():
            raise SystemExit(f"missing figure: {path}")
        probe = Image(str(path))
        width = content_w
        height = width * float(probe.imageHeight) / float(probe.imageWidth)
        if height > max_height:
            height = max_height
            width = height * float(probe.imageWidth) / float(probe.imageHeight)
        image = Image(str(path), width=width, height=height)
        image.hAlign = "CENTER"
        return [KeepTogether([image, P(caption, "Cap")])]

    def pair(left: Path, right: Path, left_cap: str, right_cap: str, height: float = 52 * mm) -> Table:
        images = []
        captions = []
        gap = 4 * mm
        col = (content_w - gap) / 2
        for path, caption in ((left, left_cap), (right, right_cap)):
            if not path.is_file():
                raise SystemExit(f"missing figure: {path}")
            probe = Image(str(path))
            w = col
            h = w * float(probe.imageHeight) / float(probe.imageWidth)
            if h > height:
                h = height
                w = h * float(probe.imageWidth) / float(probe.imageHeight)
            image = Image(str(path), width=w, height=h)
            image.hAlign = "CENTER"
            images.append(image)
            captions.append(P(caption, "Cap"))
        table = Table(
            [[images[0], "", images[1]], [captions[0], "", captions[1]]],
            colWidths=[col, gap, col],
        )
        table.setStyle(TableStyle([
            ("VALIGN", (0, 0), (-1, 0), "MIDDLE"),
            ("ALIGN", (0, 0), (-1, -1), "CENTER"),
            ("LEFTPADDING", (0, 0), (-1, -1), 0),
            ("RIGHTPADDING", (0, 0), (-1, -1), 0),
            ("TOPPADDING", (0, 0), (-1, -1), 0),
            ("BOTTOMPADDING", (0, 0), (-1, -1), 0),
        ]))
        return table

    class Steps:
        def __init__(self):
            self.n = 0

        def __call__(self, title: str, *blocks) -> list:
            self.n += 1
            head = P(f'<font color="{VIOLET}"><b>{self.n:02d}</b></font>&nbsp;&nbsp;<b>{title}</b>', "Step")
            bar = Table([[head]], colWidths=[content_w])
            bar.setStyle(TableStyle([
                ("LINEBEFORE", (0, 0), (0, 0), 2.5, violet),
                ("LEFTPADDING", (0, 0), (-1, -1), 8),
                ("RIGHTPADDING", (0, 0), (-1, -1), 0),
                ("TOPPADDING", (0, 0), (-1, -1), 1),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 2),
                ("VALIGN", (0, 0), (-1, -1), "TOP"),
            ]))
            body = []
            for block in blocks:
                body.append(P(block) if isinstance(block, str) else block)
            return KeepTogether([Spacer(1, 2.4 * mm), bar, *body])

    def story_for(steps: Steps, toc: TableOfContents) -> list:
        flow = [CoverPage()]
        flow += [
            P("Contents", "H1"),
            P("Do the steps in order. Finish the check at the end of a step before you start the next one. "
              "A fault on the bench takes a minute. The same fault inside a closed case takes an hour.", "Lede"),
            PhaseStrip(),
            Spacer(1, 4 * mm),
            toc,
            Spacer(1, 3 * mm),
            grid(
                ["Stage", "Time"],
                [
                    ["Fit coupons, then the case", "Coupons about 1 hour. Knob about 30 minutes. The case is the long print: use the slicer's own estimate."],
                    ["Plate, inserts, switches, screens", "About 1 hour"],
                    ["Matrix: 12 diodes, 4 rows, 3 columns", "2 to 3 hours the first time"],
                    ["Screens, centre control, LED sticks", "About 2 hours"],
                    ["Pico, then the short check", "1 to 2 hours"],
                    ["Flash and bench test", "About 10 minutes"],
                    ["Mount and close", "About 30 minutes"],
                ],
                [62 * mm, content_w - 62 * mm],
            ),
            Spacer(1, 3 * mm),
            P("Steps are numbered. Skip a step that names the other edition. The frame and the bar-display spine are the same parts in both editions."),
        ]

        flow += chapter(
            "printer", "The printer",
            "Any printer with a 180 × 180 mm bed or larger can print this. A Bambu Lab A1 Mini is the small end of that range. A bigger printer is fine. Do not scale the parts.",
        )
        flow += [
            P("The largest part is the top plate, 159 × 100 mm and 31 mm tall. On a 180 mm plate that leaves about 10 mm spare on either side of the long edge. The tallest feature is well under a 180 mm height."),
            P("Use a 0.4 mm nozzle, which is what an A1 Mini ships with. PLA or PETG both work. PETG is a little tougher on the screw threads and the switch clips. PLA gives crisper edges. Print the frame in translucent or transparent PETG if you want the light band. A white PLA frame lets some light through. An opaque frame still works. You see the LEDs mostly through the gap around the centre control."),
            callout(
                "note", "Bambu Studio, or any other slicer",
                "Choose the A1 Mini, or your larger printer, before you slice. Drag the 3MF onto the plate. The file is already oriented. Do not turn it over, and do not lay it on a different face. Supports off. Brim off, unless a corner lifts off the bed. Start from a 0.20 mm layer profile. Use 0.12 mm or 0.16 mm only for the knob.",
            ),
            Spacer(1, 2 * mm),
            callout(
                "warn", "Leave the first layer alone",
                "The switch holes and the signature under the base are on the first layer. A squished first layer closes them up. These parts already have a 0.5 mm chamfer on the bottom edge. If the slicer also shrinks the first layer to fight elephant's foot, set that compensation to 0. In Bambu Studio the setting is called elephant-foot compensation.",
            ),
            h2("Where the files are"),
            P("Joystick edition: <font face=\"DejaVu\">joystick/cad/exports/3mf/</font>. Rotary edition: <font face=\"DejaVu\">rotary/cad/exports/3mf/</font>. The same files are in the 3MF zip on the downloads page. STL is there too, if your slicer will not open 3MF. Fit coupons are in <font face=\"DejaVu\">fit-tests/</font> inside that folder."),
            P("Print one edition. The top plate and the base are different. The rotary edition adds the knob. <font face=\"DejaVu\">ballast</font> is in both, and each one matches that edition's centre module."),
        ]

        flow += chapter(
            "parts", "Parts and tools",
            "Buy the USB-C Pico 2 the case was drawn around. The opening in the back wall is a USB-C receptacle, not micro-USB.",
        )
        flow += [
            grid(
                ["Qty", "Part", "Notes"],
                [
                    ["1", "Raspberry Pi Pico 2, USB-C clone", "RP2350. The usual red clone has no holes at the USB end. The case holds that end."],
                    ["12", "MX switches", "Plate-mount, 3-pin. A 5-pin switch also fits. Clip the extra plastic legs, or leave them."],
                    ["12", "1u MX keycaps", "Any profile."],
                    ["12", "1N4148 diodes", "Through-hole. One per switch."],
                    ["1", "1.9 inch ST7789 module", "170 × 320, 8-pin: GND VCC SCL SDA RES DC CS BLK."],
                    ["1", "2.25 inch ST7789P3 module", "76 × 284, 8-pin: GND VCC SCL SDA RST DC CS BL."],
                    ["2", "WS2812B LED sticks", "8 LEDs, about 53 × 10 mm, CJMCU-2812-8 style."],
                    ["1", "1N4001 diode", "Any 1N400x. This feeds 5 V to the LEDs only."],
                    ["1", "330 Ω resistor", "In the LED data wire."],
                    ["8", "M3 heat-set inserts", "5.7 × 4.6 mm. Ruthex RX-M3x5.7 or a CNC Kitchen standard M3."],
                    ["8", "M3 × 8 countersunk", "ISO 10642 / DIN 7991. All eight case screws are this size."],
                    ["4", "M2 × 4 pan head, self-tapping", "1.9 inch screen."],
                    ["2", "M2 × 8 pan head, self-tapping", "Bar screen and the printed spine."],
                    ["6", "M2 × 5 pan head, self-tapping", "Pico (2) and LED sticks (4)."],
                    ["4", "Rubber feet", "About Ø10 × 3 mm. 3M Bumpon or similar."],
                    ["2.5 m", "26–28 AWG silicone wire", "Five or six colours. 30 AWG wire-wrap also works. It breaks more easily."],
                ],
                [16 * mm, 52 * mm, content_w - 68 * mm],
            ),
            Spacer(1, 2 * mm),
            h2("Joystick edition, extra"),
            grid(
                ["Qty", "Part", "Notes"],
                [["1", "KY-023 thumb joystick", "Also sold as HW-504."],
                 ["4", "M3 × 6 pan head, self-tapping", "The stick stands on four posts in the base."]],
                [16 * mm, 58 * mm, content_w - 74 * mm],
            ),
            h2("Rotary edition, extra"),
            grid(
                ["Qty", "Part", "Notes"],
                [["1", "KY-040 encoder", "EC11, 20 detents, M7 bushing, 6 mm D-shaft. The washer and M7 nut come with it."],
                 ["1", "Printed knob", "rotary/cad/exports/3mf/knob.3mf. Ø48 mm."]],
                [16 * mm, 42 * mm, content_w - 58 * mm],
            ),
            h2("Tools"),
            P("Temperature-controlled iron, fine chisel or conical tip. 320–350 °C for leaded solder (63/37 or 60/40 flows best), 350–380 °C for lead-free. Solder 0.6–0.8 mm with a flux core. A flux pen for the display pads and the LED pads. Flush cutters, wire strippers for 26–30 AWG, fine tweezers, helping hands or a small vice, Kapton tape, 2 mm heat-shrink, isopropyl alcohol. A multimeter with diode and continuity modes. A 2.5 mm hex key or Phillips #0 and #1. Pliers or a small wrench for the encoder nut. Fume extraction, or an open window. Flux smoke is not something to breathe."),
            P("No inserts on hand? You can skip them and use M3 × 8 self-tapping plastite screws instead. That is fine for a few assembly cycles. The holes in the model are sized for the inserts, so this is a second choice, not the normal build."),
        ]

        flow += chapter(
            "print", "Print the parts",
            "Print the fit coupons before the case. They use a few grams and about an hour, and they tell you whether your printer matches the holes.",
            keep=170 * mm,
        )
        flow += figure(IMG / "joystick" / "exploded.png",
                       "Joystick edition, top to bottom: keycaps, switches, top plate, frame, base. The rotary stack is the same shape, with a knob instead of the stick.")
        flow += [
            steps(
                "Print the fit coupons",
                "They live in <font face=\"DejaVu\">fit-tests/</font>. Print every coupon for your edition.",
            ),
            grid(
                ["Coupon", "Pass looks like"],
                [
                    ["fit_switch", "Three holes: 13.9, 14.0 and 14.1 mm. A switch clicks in and does not wobble. Remember which hole."],
                    ["fit_plate_d19", "The 1.9 inch module drops in. The glass touches the skin. The four M2 holes line up."],
                    ["fit_plate_bar", "The 2.25 inch module fits. The two M2 holes line up with the hole end."],
                    ["fit_plate_joy", "Joystick only. The stick tilts fully in every direction and misses the bevel."],
                    ["fit_plate_enc", "Rotary only. The KY-040 body drops into the square pocket. The washer and nut clamp it."],
                    ["fit_usb", "The Pico 2 sits on the standoffs. The USB-C receptacle sits in the port. A cable plugs in fully."],
                    ["fit_joystick", "Joystick only. The KY-023 holes line up with the four posts."],
                    ["fit_led", "An LED stick lies flat on the two posts. Both holes line up."],
                ],
                [32 * mm, content_w - 32 * mm],
            ),
            Spacer(1, 2 * mm),
            callout(
                "warn", "A coupon failed",
                "Check scaling, elephant-foot compensation, and first-layer squish before you change anything else. If you are printing the shipped files and a hole is still wrong, stop. Do not print the case on a bad first layer. If you build from source, the values marked MEASURE in common/cad/macropad_cad/params.py and the edition's edition.py are the ones to change, then run make cad.",
            ),
            steps(
                "Print the case",
                "One plate per part. Colour A for the top plate and the base. Colour B, translucent if you can, for the frame and the rotary knob. Drop the file on the bed as exported.",
            ),
            grid(
                ["Part", "Bed", "Height", "Face on the bed", "Settings"],
                [
                    ["top_plate", "159 × 100", "31 mm", "Top face down", "0.2 mm, 4 walls, 20% gyroid, 5 top and bottom layers"],
                    ["frame", "159 × 98", "27 mm", "Flat bottom down", "0.2 mm, 3 walls, 15% infill"],
                    ["base", "159 × 98", "10 mm", "Flat bottom down", "0.2 mm, 3 walls, 20% infill"],
                    ["bar_spine", "72 × 25", "6 mm", "Flat side down", "0.2 mm, 100% infill"],
                    ["ballast", "149 × 88", "19 mm", "Flat bottom down, trenches up", "0.2 mm, 100% infill"],
                    ["knob (rotary)", "Ø48", "18.7 mm", "Top face down", "0.12–0.16 mm, 4 walls, 30% infill, no brim"],
                ],
                [28 * mm, 24 * mm, 16 * mm, 42 * mm, content_w - 110 * mm],
            ),
            Spacer(1, 2 * mm),
            P("Approximate filament, not a promise: top plate 60 g, frame 30 g, base 50 g, knob 24 g, ballast 100 g (joystick) or 107 g (rotary). The ballast number is the solid mass, so print it at 100% infill."),
            P("Top plate: printing the top face down gives a flat, bed-textured surface. A textured PEI sheet, the usual A1 Mini plate, gives a matte top. A smooth sheet gives gloss. The 1.5 mm switch skin and the 2 mm screen skin are the first layers, so the 14.0 mm holes depend on that layer. The screw bosses stand up to 26 mm. Print them at a normal speed with the part fan on."),
            P("Frame: the USB-C opening has two short bridges, 8 mm or shorter. Default bridge settings are enough. For a more even glow, print the frame at 100% infill, or at 0% infill with 4 walls. 3 walls and 15% is fine."),
            P("Base: the signature is grooves in the first layer, 1.4 mm wide and 0.4 mm deep. Extra squish closes them. The four LED posts and the Pico standoffs are small. Slow the outer walls a little if they come out stringy."),
            P("Ballast: the sloped top prints without supports. On the joystick edition the slicer shows two bodies. Print both. The small strip sits between the left LED stick and the Pico."),
            P("Knob, rotary only: the flutes run straight up the sides when the top face is down, and the indicator dimple stays crisp. The bore is a press fit on the 6 mm D-shaft and ends at the shaft tip, so the knob stops 0.7 mm above the plate. Shaft lengths vary. 15, 20 and 25 mm KY-040 boards exist. Measure from the top of the encoder body to the shaft tip before you rely on the knob."),
        ]
        flow += figure(
            IMG / "joystick" / "base_underside.png",
            "The signature is on the first layer of the base. Print that layer at a normal height.",
            max_height=78 * mm,
        )

        flow += chapter(
            "plate", "Build the top plate",
            "Heat-set inserts, then the switches and both screens. The joystick itself waits until the base. The encoder goes into the plate now.",
        )
        flow += [
            steps(
                "Inserts and feet",
                "Clean stringing out of the switch holes and the screen windows. Lay the top plate face down. The eight bosses are the insert holes: Ø4.0 mm, 6.2 mm deep, with a lead-in chamfer. The bosses lean 10° on the bench, because they are vertical in the finished case. Tilt the iron to follow the boss, not the table.",
                "A heat-set tip is best. A clean conical tip works. About 200 °C for PLA and about 220 °C for PETG is a starting point. Stop if the plastic smokes. Press each insert flush with the end of the boss and let it cool before you touch it. Eight inserts, M3 × 5.7 × 4.6 mm.",
                "Stick the four rubber feet into the recesses under the base.",
            ),
            callout("note", "Rotary only, try the knob now",
                    "Press the knob onto the encoder shaft before the encoder is in the plate. It should need a firm push and it should not wobble. Too tight or too loose is a reprint of the knob only, about 30 minutes, after you change Knob.bore_clear in rotary/cad/edition.py. The default is 0.10 mm. 0.05 is tighter. 0.20 is looser. Then run make cad-rotary. Skip this if you are printing the shipped knob and the fit is firm."),
            steps(
                "Switches and both screens",
                "Push all 12 switches in from the top until both clips snap. Point the two metal pins the same way on every switch, toward the back of the plate.",
                "Lay the plate face down. Drop the 1.9 inch module in glass first, header toward the right-hand keys (the 2 × 4 block). Fix it with 4 × M2×4. Snug, not tight. The screws cut their own thread.",
                "Drop the 2.25 inch module into the long pocket, header toward the front of the plate. Put the spine over its back: crossbar at the hole end, pad at the header end. Drive 2 × M2×8 through the spine and the module's two holes.",
                "Peel the films off the glass from the top side now, or at the very end.",
            ),
        ]
        flow += figure(
            IMG / "joystick" / "plate_underside.png",
            "The plate, seen from below, after the screens are in. This is the side you solder.",
            max_height=92 * mm,
        )
        flow += [
            steps(
                "Mount the encoder",
                "Rotary edition only. Joystick edition: do nothing here. The stick is screwed to the base later.",
                "Take the nut and washer off the KY-040. Push the bushing up through the round hole from below. Point the header left, toward the single key column. The square body drops into the square pocket, which stops it turning. From the top, fit the washer and the nut. The nut clamps a 2 mm section of the plate. Tighten it gently. Leave the knob off until the case is closed.",
            ),
            callout("test", "Check",
                    "Every switch sits flush and both clips are snapped. Neither screen moves in its pocket. On the rotary edition the encoder does not rotate in the hole, and the nut is snug."),
        ]
        flow += [
            pair(
                IMG / "joystick" / "top.png", IMG / "rotary" / "top.png",
                "Joystick. Bar screen on the left. USB at the back.",
                "Rotary. The knob goes on last.",
                height=48 * mm,
            )
        ]

        flow += chapter(
            "matrix", "Solder the matrix",
            "Twelve switches would need twelve pins. A matrix needs seven: four rows and three columns. Work with the plate upside down. Left and right are mirrored in that view.",
            keep=145 * mm,
        )
        flow += figure(IMG / "matrix.png", "COL2ROW. Current goes column, switch, diode, row. The black band points at the row.", max_height=95 * mm)
        flow += [
            P("The firmware pulls one row low at a time and reads the three columns. The diode is what makes several keys work at once. Without it, three pressed keys can pretend a fourth key is pressed. Every diode points the same way: black band toward the row wire, away from the switch."),
            h2("A joint, in one minute"),
            P("Tin the tip, wipe it on brass wool, and keep it shiny. Heat the pin and the wire, then feed solder into the joint from the other side. Two or three seconds. Take the solder away, then the iron, and do not move the joint while it sets. A good joint is smooth and concave. Shiny with leaded solder, satin with lead-free. A dull grainy blob is a cold joint: reheat it with a little flux. Strip 3–4 mm, tin the wire end first, and keep every joint small. Under the switches there is about 1.5 mm of room."),
            steps(
                "Diodes on the switches",
                "Each MX switch has two metal pins. The round plastic post and the two plastic legs are not connected to anything. Call the pin nearer the switch centre pin A, and the other pin B. Pin A gets the diode. Pin B gets the column wire. Use the same choice on all 12 switches.",
                "Bend the cathode leg (the black band) 90° about 3 mm from the body. Cut the anode leg to about 4 mm. Hold the diode with tweezers, the short anode on pin A, the body flat on the switch, the long leg pointing toward the row. With the plate upside down, that long leg points toward the front of the plate.",
                "Heat pin A and the leg together and feed a little solder. The band points away from the switch. Do all 12, row by row, on a folded cloth.",
            ),
        ]
        flow += figure(IMG / "soldering_diode.png", "One switch, from below. Band toward the row wire. Column wire on the other pin.", max_height=58 * mm)
        flow += [
            steps(
                "Run the four row wires",
                "The bent cathode legs are the row wire. Bend each leg to the next diode, overlap by about 5 mm, and solder the overlap. Across the middle of the pad, bridge the gap with a piece of wire.",
                "Row 0 is the back row, next to where the USB port will be. Row 3 is the front row. Rows 0 and 1 run straight across, over the back of the 1.9 inch module. Lay them flat and tape them down with Kapton. Rows 2 and 3 have the stick or the encoder in the way. Route them around the front of it.",
                "A bare row must not touch a bare column. Where they cross, one of them is insulated. Finish each row with a 15 cm tail and a label: R0, R1, R2, R3.",
            ),
        ]
        flow += figure(IMG / "soldering_row.png", "One row, seen from below. The diode legs become the row. Insulate the crossing.", max_height=72 * mm)
        flow += [
            steps(
                "Run the three column wires",
                "One insulated wire per column, front to back, over pin B of every switch in that column. Cut it to the column length plus 15 cm toward the back. At each switch, open a 3 mm window in the insulation, tin the copper, lay it on pin B, and solder.",
                "Column 0 is the single column next to the bar screen. Columns 1 and 2 are the block on the right. That is the top view. Upside down, left and right swap, and the bar screen is on your right. Label the tails C0, C1, C2.",
            ),
            steps(
                "Test the matrix before anything else",
                "Multimeter in diode mode. Red probe on a column tail, black probe on a row tail. Press the key at that crossing: the meter reads about 0.6 V. Release the key: the meter reads open. A reading with the probes swapped, or a reading without a press, means that diode is backwards or a leg is touching something. Fix it now.",
                "Check all 12 keys.",
            ),
            callout("test", "Check",
                    "12 pressed keys read about 0.6 V. 12 released keys read open. No bare row touches a bare column. Four row tails and three column tails are labelled."),
        ]

        flow += chapter(
            "modules", "Wire the screens, the centre, and the LEDs",
            "Cut every wire from the plate long enough to reach the Pico, plus about 8 cm. The plate has to open like a book later. Use the same colour for the same signal on both screens.",
        )
        flow += [
            steps(
                "Solder both screens",
                "Both headers have pads on both sides of the board. Solder to the back of the module, the side with the components. The glass is already in the pocket. A little solder on the glass side is fine. The plate has a relief groove over the pin row.",
                "Eight wires per screen, about 20 cm, in this colour order on both: GND black, VCC red, then SCL, SDA, RES, DC, CS, BL. The bar module calls RES \"RST\" and BL \"BL\". It is the same pin order. Tin the wire, tin the pad, solder one pin at a time. Flux helps.",
                "Tug each wire. Put a short piece of heat-shrink around the eight wires, about 3 cm from the header, so one joint never takes the strain. Continuity mode: no two neighbouring pins are joined.",
                "In the top view the 1.9 inch header is on the right, under the right-hand keys. The bar header points at the front.",
            ),
            steps(
                "Solder the stick or the encoder",
                "Five wires, about 20 cm. Solder to the right-angle header and heat-shrink each joint, or unsolder the header and solder to the board. Both fit.",
                "Joystick, KY-023: GND, +5V, VRx, VRy, SW. The pin is labelled +5V. You will connect it to 3V3, not to 5 V.",
                "Encoder, KY-040: GND, +, SW, DT, CLK. The pin is labelled +. You will connect it to 3V3, not to 5 V.",
            ),
            callout(
                "stop", "3V3 for the stick and the encoder",
                "5 V on that pin reaches the Pico's pins. The screens and the centre control all take 3V3 from pin 36. Only the LED sticks take 5 V, and only through the 1N4001.",
            ),
            steps(
                "Solder the LED sticks",
                "Each stick has three pads at both ends, on the back: 5V, GND, and DIN at one end or DOUT at the other. The arrow on the LEDs points from DIN to DOUT. Keep every joint flat. The sticks sit on 3 mm posts and the joints hang underneath. Tin the pad, tin the wire, lay the wire along the pad.",
                "Right stick, DIN end. This end faces the back of the case, next to the Pico's USB end. Three wires to the Pico, about 12 cm. In the 5V wire, solder the 1N4001 with the band toward the stick, and heat-shrink it. In the data wire, solder the 330 Ω resistor about 2 cm from the stick, and heat-shrink it. GND is a plain wire.",
                "From the right stick's DOUT end to the left stick's DIN end, a three-wire jumper about 70 mm: 5V, GND, and DOUT to DIN. In the case the sticks point in opposite directions, so both of these ends are at the front. The jumper runs across in front of the Pico.",
                "Continuity: 5V does not touch GND on either stick.",
            ),
            callout("test", "Check",
                    "No two neighbouring header pins are joined. 5V does not touch GND on either LED stick. The diode band points at the stick. The plate still reaches the Pico's spot with slack left over."),
        ]

        flow += chapter(
            "pico", "Wire the Pico",
            "Solder every wire to the top of the Pico. There is about 1 mm under the board once it is on its standoffs, so a joint on the underside will not fit. Do this on the bench, before the Pico goes into the case.",
        )
        flow += [
            P("Lay the tinned wire into the hole from the top, at a shallow angle, and heat the pad and the wire together. The joint stays low. Do not push the wire through and solder underneath. Work one group at a time: rows, columns, bar screen, main screen, centre control, LEDs, power. Any GND pin can take a ground. The joystick ground is quieter on AGND, pin 33. The encoder ground goes to pin 28, the GND next to the switch pin."),
            h2("Joystick edition"),
        ]
        flow += figure(IMG / "wiring_joystick.png", "Pico 2, joystick edition. Pin numbers are the physical pins. GP numbers are the GPIO names in the firmware.", max_height=118 * mm)
        flow += [h2("Rotary edition")]
        flow += figure(IMG / "wiring_rotary.png", "Pico 2, rotary edition. Same pins. CLK, DT and SW take the three GPIOs the joystick uses.", max_height=118 * mm)
        flow += [
            h2("Where the wires run"),
            P("These maps are drawn from the CAD model. The plate is seen from below, as it lies on your bench, so left and right are mirrored. The base is seen from above. The bar screen is on the right in the plate drawing and on the left when you look at the finished pad."),
        ]
        flow += figure(IMG / "wiring_map_joystick.png", "Joystick edition. Plate from below on the left, base from above on the right.", max_height=78 * mm)
        flow += figure(IMG / "wiring_map_rotary.png", "Rotary edition. Same routing. The encoder hangs from the plate instead of standing on posts.", max_height=78 * mm)
        flow += [
            h2("Pin checklist"),
            grid(
                ["Pico pin", "GPIO", "Connect"],
                [
                    ["4, 5, 6, 7", "GP2–GP5", "Row 0 (back) through row 3 (front)"],
                    ["9, 10, 11", "GP6–GP8", "Column 0 (left, by the bar), then column 1, then column 2"],
                    ["14", "GP10", "Bar SCL, SPI1 clock"],
                    ["15", "GP11", "Bar SDA, SPI1 data"],
                    ["16", "GP12", "Bar DC"],
                    ["17", "GP13", "Bar CS"],
                    ["19", "GP14", "Bar RST"],
                    ["20", "GP15", "Bar backlight"],
                    ["21", "GP16", "Main DC"],
                    ["22", "GP17", "Main CS"],
                    ["24", "GP18", "Main SCL, SPI0 clock"],
                    ["25", "GP19", "Main SDA, SPI0 data"],
                    ["26", "GP20", "Main RES"],
                    ["27", "GP21", "Main backlight"],
                    ["29", "GP22", "Joystick SW, or encoder SW"],
                    ["31", "GP26", "Joystick VRx, or encoder CLK"],
                    ["32", "GP27", "Joystick VRy, or encoder DT"],
                    ["33", "AGND", "Joystick GND. Encoder GND uses pin 28 instead."],
                    ["34", "GP28", "LED DIN, through the 330 Ω resistor"],
                    ["36", "3V3", "VCC of both screens, and the stick's +5V pin, or the encoder's + pin"],
                    ["38", "GND", "LED GND"],
                    ["40", "VBUS", "LED 5V, through the 1N4001, band toward the sticks"],
                    ["3, 8, 13, 18, 23", "GND", "Screen grounds. Any of these."],
                ],
                [32 * mm, 28 * mm, content_w - 60 * mm],
            ),
            Spacer(1, 2 * mm),
            P("GP0, GP1 and GP9 are free. Leave them empty."),
            P("The two screens use two separate SPI ports, so a wiring fault on one does not take the other with it. Keep the SPI wires reasonably short. The Pico sits under the 1.9 inch screen."),
            steps(
                "Solder the Pico, then look for shorts",
                "Trim the plate wires so they reach the Pico's place at the back of the base, plus about 8 cm. Solder from the top, one group at a time, and tick the table as you go.",
                "Before any USB cable: continuity from 3V3 to GND, from VBUS to GND, and from 3V3 to VBUS. All three read open. A beep here is a bridged pin or a reversed LED stick. Look along every header for a bridge, and check the diode bands one more time. Do not plug a shorted board in.",
            ),
            callout("stop", "Still no cable",
                    "3V3 to GND, open. VBUS to GND, open. 3V3 to VBUS, open. Then you may plug it in."),
        ]

        flow += chapter(
            "flash", "Flash the firmware",
            "Flash on the bench, with the case still open. You need the UF2 for your edition and your board. Copying the file is the whole install.",
        )
        flow += [
            grid(
                ["Edition", "Board", "File"],
                [
                    ["Joystick", "Pico 2, RP2350", "joystick/firmware/dist/nyxilab-macropad-joystick-pico2.uf2"],
                    ["Joystick", "Pico, RP2040", "joystick/firmware/dist/nyxilab-macropad-joystick-pico.uf2"],
                    ["Rotary", "Pico 2, RP2350", "rotary/firmware/dist/nyxilab-macropad-rotary-pico2.uf2"],
                    ["Rotary", "Pico, RP2040", "rotary/firmware/dist/nyxilab-macropad-rotary-pico.uf2"],
                ],
                [28 * mm, 32 * mm, content_w - 60 * mm],
            ),
            Spacer(1, 2 * mm),
            P("Use the Pico 2 file for the USB-C RP2350 board this case is drawn around. The RP2040 file is for an original Pico. That board is micro-USB, and it will not sit in the USB-C opening."),
            steps(
                "Copy the UF2",
                "Hold the BOOTSEL button on the Pico. Plug in the USB cable. Release BOOTSEL. A drive named RP2350 (Pico 2) or RPI-RP2 (Pico) appears. Copy the UF2 onto that drive. The drive disappears and the pad restarts.",
                "Within about a second both screens come on and the LEDs light violet. If the Pico never appears as a drive, unplug it and repeat the three short checks before you try again.",
            ),
            P("To build the UF2 yourself you need PlatformIO (<font face=\"DejaVu\">make setup</font>). From the repo, <font face=\"DejaVu\">make firmware</font> writes both Pico 2 files. <font face=\"DejaVu\">make flash-joystick</font> or <font face=\"DejaVu\">make flash-rotary</font> copies the firmware over USB and restarts the pad into the bootloader on its own."),
            steps(
                "Test every input",
                "Press every key. The 1.9 inch screen highlights that key, and the LEDs flash. Move the stick, or turn the encoder shaft and press it. Keep the stick still while you plug in. The centre is sampled at power-up.",
                "On first boot the layer is MEDIA. Column 0, back to front: mute, previous, stop, layer. Column 1: volume down, play, brightness down, back. Column 2: volume up, next, brightness up, forward. The stick is a mouse. The knob is volume. Tap the front-left key to change layer. Hold that same key for the FN layer.",
            ),
            callout("test", "Check",
                    "Both screens are on. The LEDs are violet and they flash when you press a key. All 12 keys highlight on the 1.9 inch screen. The stick moves the pointer, or the encoder changes volume and its press registers. A wrong direction here is a setting, not a bad joint. See the last page."),
        ]
        flow += [
            pair(
                IMG / "joystick" / "ui" / "ui_02_media_active.png",
                IMG / "rotary" / "ui" / "ui_02_media_volume.png",
                "Joystick, MEDIA layer. Rendered by the firmware.",
                "Rotary, MEDIA layer. The dial is the knob.",
                height=58 * mm,
            )
        ]
        flow += [
            h2("Flashing again, after the case is closed"),
            P("BOOTSEL is hidden. Use one of these, then copy the UF2 the same way."),
            P("Hold the back-left key and the front-right key, then plug the cable in. Or hold the front-left key (FN) and press the back-left key (BOOT). Or, from a computer with the repo, run <font face=\"DejaVu\">make flash-joystick</font> or <font face=\"DejaVu\">make flash-rotary</font>."),
            BootKeys(),
            Spacer(1, 2 * mm),
        ]

        flow += chapter(
            "close", "Close the case",
            "Unplug the USB cable first. The wires stay attached. You are only putting the boards into the shell.",
            keep=140 * mm,
        )
        flow += figure(IMG / "joystick" / "base_leds.png", "Base, from above. Pico in the middle, one LED stick on each side, LEDs facing up.", max_height=88 * mm)
        flow += [
            steps(
                "Mount the base",
                "LED sticks, LEDs up, one each side of the Pico cradle. The mounting holes are on the outer side of each stick. The right stick has DIN at the back. The left stick has DIN at the front. 2 × M2×5 on each stick.",
                "Set the frame on the base. The lip segments locate it.",
                "Joystick edition: set the stick on its four posts, header to the left, click-switch toward the front. 4 × M3×6 from above.",
                "Tilt the Pico, push the USB-C receptacle into the opening in the back wall, and lower the far end onto the two standoffs. 2 × M2×5. A cable must now plug in fully from outside the case.",
            ),
        ]
        flow += [
            pair(
                IMG / "joystick" / "section_aa.png", IMG / "rotary" / "section_aa.png",
                "Joystick: stick, main screen, Pico, USB-C.",
                "Rotary: knob, encoder, main screen, Pico, USB-C.",
                height=42 * mm,
            )
        ]
        flow += [
            steps(
                "Ballast, plate, screws, caps",
                "Drop the ballast in. It lowers over the Pico, the LED sticks, and, on the joystick edition, the stick. That edition also has a smaller strip, which sits between the left LED stick and the Pico. Leave the LEDs uncovered. The boss holes stay open.",
                "Press the wires into the trenches. Bar-screen leads in the left lane. The LED jumper in the cross lane between the sticks. Matrix slack in the centre lane. The 1.9 inch wires in the lane on the Pico's right. Keep everything else out of the joystick opening and off the stick's mechanism. Off the encoder too.",
                "Lower the top plate onto the frame. The stick passes through its opening, or the encoder hangs in the space in front of the Pico. The lip drops inside the frame. The bosses pass through the ballast into their saddles.",
                "Turn the pad over. Drive the 8 × M3×8 countersunk screws in a cross pattern until the seams close. Do not overtighten.",
                "Fit the keycaps. Rotary edition: turn the shaft so the flat points where you want the indicator to start. Line the flat up with the bore and press the knob on until it stops. It sits 0.7 mm above the plate. A press on the knob presses the encoder switch.",
            ),
            callout("test", "Check",
                    "A USB-C cable plugs in fully from outside. Every key still works. The stick moves freely and does not scrape, or the knob turns, clicks at each detent, and the press clicks. The frame glows in the layer colour if you printed it translucent."),
        ]
        flow += figure(IMG / "joystick" / "inside.png", "What the closed pad is hiding. Wires stay in the trenches, off the LEDs and off the centre control.", max_height=90 * mm)
        flow += [
            h2("Opening it later"),
            P("Pull the knob off, rotary edition. Remove the eight bottom screws and lift the top plate. It stays connected by the wires. The ballast stays in the base. The Pico comes out with its two screws. Tilt it out of the port. The LED sticks and their wiring stay in the base."),
        ]
        flow += [
            pair(
                IMG / "joystick" / "hero.png", IMG / "rotary" / "hero.png",
                "Joystick edition, finished.",
                "Rotary edition, finished.",
                height=52 * mm,
            )
        ]

        flow += chapter(
            "wrong", "When something is wrong",
            "Most of these are one joint or one setting. Unplug before you resolder. If the Pico does not show up as a USB device at all, repeat the 3V3 and VBUS short checks before you do anything else.",
        )
        flow += [
            grid(
                ["What you see", "Look at"],
                [
                    ["One key does nothing", "That diode joint, and the window in its column wire."],
                    ["One key also presses another, or a key you did not touch", "A diode is backwards or missing, or a bare row touches a column."],
                    ["A whole row or column is dead", "The tail at the Pico. Rows are GP2 to GP5. Columns are GP6 to GP8."],
                    ["Presses show up doubled", "A cracked joint that opens and closes. Debounce is already 5 ms."],
                    ["A screen stays white or black", "Its VCC and GND, then SCL and SDA, then RES. Main screen is SPI0. Bar is SPI1."],
                    ["The picture is upside down", "Not a joint. MAIN_ROTATION (1 or 3) or BAR_ROTATION (2 or 0) in config.h."],
                    ["Colours look inverted", "MAIN_INVERT or BAR_INVERT in config.h."],
                    ["Red and blue are swapped", "MAIN_BGR or BAR_BGR in config.h."],
                    ["Garbled pixels", "SPI wires too long or too close to the LED data wire. SPI_HZ can go down to 20 MHz."],
                    ["LEDs stay dark", "5V and GND at the right stick's DIN end, the 1N4001 direction, and the data wire on GP28."],
                    ["Only the first stick lights", "The jumper from the right stick's DOUT to the left stick's DIN."],
                    ["LEDs flicker or show the wrong colours", "A weak GND, or the data wire running next to the SPI wires. Move it."],
                    ["The pointer or the knob goes the wrong way", "Not a joint. JOY_INVERT_X, JOY_INVERT_Y or JOY_SWAP_XY in config.h. On the knob, hold FN and press REV. That is saved."],
                    ["The knob steps twice per click", "ENC_STEPS_PER_DETENT in config.h. A normal KY-040 uses 4. A half-step encoder uses 2."],
                    ["The stick is off centre", "Keep it still while plugging in, or press CAL on the FN layer."],
                ],
                [58 * mm, content_w - 58 * mm],
            ),
            Spacer(1, 3 * mm),
            P("config.h is <font face=\"DejaVu\">joystick/firmware/include/config.h</font> or <font face=\"DejaVu\">rotary/firmware/include/config.h</font>. Change it, rebuild the UF2, and flash again. You do not open the case for a direction change on the knob: FN + REV is enough."),
            P("Optional, after the pad works: on Linux, the nyxd daemon can put your computer's CPU, memory and temperatures on a HOST layer, and some keys can run commands. That is a separate setup. See docs/host-bridge.md."),
            Spacer(1, 4 * mm),
            callout(
                "note", "You are done when",
                "A cable seats fully. All 12 keys highlight on the screen. The LEDs answer each press. The stick or the knob does its job. The eight screws are snug and the seams are closed.",
            ),
        ]
        return flow

    class Manual(BaseDocTemplate):
        def afterFlowable(self, flowable):
            if isinstance(flowable, TocMark):
                self.canv.bookmarkPage(flowable.key)
                self.canv.addOutlineEntry(flowable.title, flowable.key, level=0, closed=False)
                self.notify("TOCEntry", (0, flowable.title, self.page))

    def paint_cover(c):
        c.setFillColor(HexColor(NIGHT))
        c.rect(0, 0, page_w, page_h, fill=1, stroke=0)
        c.setFillColor(violet)
        c.rect(0, page_h - 5 * mm, page_w, 5 * mm, fill=1, stroke=0)
        c.setFillColor(HexColor(VIOLET_SOFT))
        c.setFont("DejaVu", 9)
        c.drawString(16 * mm, page_h - 16 * mm, "NYXILAB")
        c.setFillColor(HexColor("#a1a1a6"))
        c.drawRightString(page_w - 16 * mm, page_h - 16 * mm, "HAND-WIRED MACROPAD")
        c.setFillColor(white)
        c.setFont("DejaVuBold", 34)
        c.drawString(16 * mm, page_h - 32 * mm, "Build guide")
        c.setFillColor(HexColor("#d0d0d4"))
        c.setFont("DejaVu", 12)
        c.drawString(16 * mm, page_h - 40 * mm, "Print.  Wire.  Solder.  Flash.")
        c.setStrokeColor(violet)
        c.setLineWidth(1)
        c.line(16 * mm, page_h - 44 * mm, 78 * mm, page_h - 44 * mm)

        # Heroes already fill the image width. A zoom above 1 crops the case.
        gap = 5 * mm
        card_w = (page_w - 32 * mm - gap) / 2
        card_h = card_w * (1150 / 1800)
        rule_y = page_h - 44 * mm
        label_drop = 6 * mm
        region_top = rule_y - 10 * mm
        region_bot = 20 * mm
        card_y = region_bot + (region_top - region_bot - card_h - label_drop) / 2
        cards = (
            (16 * mm, IMG / "joystick" / "hero.png", "JOYSTICK"),
            (16 * mm + card_w + gap, IMG / "rotary" / "hero.png", "ROTARY"),
        )
        for x, path, label in cards:
            c.drawImage(str(path), x, card_y, card_w, card_h, mask="auto")
            c.setStrokeColor(HexColor("#2a2836"))
            c.setLineWidth(0.6)
            c.rect(x, card_y, card_w, card_h, fill=0, stroke=1)
            c.setFillColor(white)
            c.setFont("DejaVuBold", 8)
            c.drawCentredString(x + card_w / 2, card_y - 5 * mm, label)

        c.setFillColor(HexColor("#a1a1a6"))
        c.setFont("DejaVu", 8)
        c.drawCentredString(page_w / 2, 12 * mm, "180 × 180 mm bed or larger   ·   Bambu Lab A1 Mini and up")
        c.drawCentredString(page_w / 2, 7.5 * mm, "No supports   ·   No glue   ·   Both editions")

    def paint_page(c, doc):
        c.saveState()
        if doc.page == 1:
            paint_cover(c)
        else:
            c.setFillColor(white)
            c.rect(0, 0, page_w, page_h, fill=1, stroke=0)
            c.setFillColor(violet)
            c.rect(0, page_h - 2.2 * mm, page_w, 2.2 * mm, fill=1, stroke=0)
            c.setFillColor(soft)
            c.setFont("DejaVu", 8)
            c.drawString(left, page_h - 9 * mm, "NYXILAB")
            c.setFont("DejaVu", 8)
            c.drawRightString(page_w - right, page_h - 9 * mm, "Build guide")
            c.setStrokeColor(line)
            c.setLineWidth(0.4)
            c.line(left, page_h - 12 * mm, page_w - right, page_h - 12 * mm)
            c.setFillColor(soft)
            c.setFont("DejaVu", 8)
            c.drawCentredString(page_w / 2, 7 * mm, str(doc.page))
            c.line(left, 12 * mm, page_w - right, 12 * mm)
        c.restoreState()

    class CompressedCanvas(pdfcanvas.Canvas):
        def __init__(self, *args, **kwargs):
            kwargs["pageCompression"] = 1
            super().__init__(*args, **kwargs)

    toc = TableOfContents()
    toc.dotsMinLevel = 0
    toc.levelStyles = [styles["TOC"]]
    steps = Steps()
    doc = Manual(
        str(OUT),
        pagesize=A4,
        title="Nyxilab macropad build guide",
        author="Nyxilab",
        subject="Print, wire, solder and flash the Nyxilab macropad on a 180 mm printer or larger.",
        canvasmaker=CompressedCanvas,
    )
    frame = Frame(
        left, bottom, content_w, frame_h, id="body", showBoundary=0,
        leftPadding=0, rightPadding=0, topPadding=0, bottomPadding=0,
    )
    doc.addPageTemplates([PageTemplate(id="body", frames=[frame], onPage=paint_page)])
    doc.multiBuild(story_for(steps, toc))
    if steps.n != STEP_COUNT:
        raise SystemExit(f"expected {STEP_COUNT} steps, built {steps.n}")
    return steps.n


def check_pdf() -> None:
    pdfinfo = shutil.which("pdfinfo")
    pdftotext = shutil.which("pdftotext")
    if pdfinfo is None or pdftotext is None:
        raise SystemExit("pdfinfo and pdftotext are required to check the guide")
    info = subprocess.run([pdfinfo, str(OUT)], check=True, capture_output=True, text=True).stdout
    pages = next(int(line.split(":", 1)[1]) for line in info.splitlines() if line.startswith("Pages:"))
    if not 16 <= pages <= 40:
        raise SystemExit(f"page count {pages} is outside 16–40")
    text = subprocess.run(
        [pdftotext, "-layout", str(OUT), "-"], check=True, capture_output=True, text=True
    ).stdout
    required = [
        "Bambu Lab A1 Mini",
        "180",
        "BOOTSEL",
        "RP2350",
        "1N4148",
        "1N4001",
        "330",
        "GP28",
        "3V3",
        "VBUS",
        "nyxilab-macropad-joystick-pico2.uf2",
        "nyxilab-macropad-rotary-pico2.uf2",
        "KY-023",
        "KY-040",
        "elephant-foot",
    ]
    missing = [item for item in required if item not in text]
    if missing:
        raise SystemExit("guide is missing: " + ", ".join(missing))
    print(f"wrote {OUT} ({pages} pages, {OUT.stat().st_size / 1e6:.1f} MB)")


def main() -> None:
    build()
    check_pdf()


if __name__ == "__main__":
    main()
