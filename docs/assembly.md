# Assembly

![Exploded view](images/exploded.png)

## Bill of materials

| Qty | Part | Notes |
|---|---|---|
| 1 | Raspberry Pi Pico 2 (RP2350), USB-C version | the original Pico (RP2040) also works (`pio run -e pico`) |
| 12 | MX-style switches | plate-mount (3-pin) or PCB-mount (5-pin, clip the two plastic legs or leave them) |
| 12 | 1u MX keycaps | any profile |
| 12 | 1N4148 diodes | through-hole |
| 1 | 1.9" 170×320 ST7789 IPS module, 8-pin (GND VCC SCL SDA RES DC CS BLK) | |
| 1 | 2.25" 76×284 ST7789P3 module, 8-pin (GND VCC SCL SDA RST DC CS BL) | |
| 1 | KY-023 / HW-504 thumb joystick module | |
| 8 | M3 heat-set inserts, 5.7 × 4.6 mm | |
| 8 | M3 × 8 countersunk screws (ISO 10642 / DIN 7991) | case |
| 4 | M3 × 6 pan-head self-tapping screws | joystick |
| 4 | M2 × 4 pan-head self-tapping screws | 1.9" display |
| 2 | M2 × 8 pan-head self-tapping screws | bar display + spine |
| 2 | M2 × 5 pan-head self-tapping screws | Pico |
| 4 | Rubber feet Ø10 × 3 mm (3M Bumpon or similar) | |
| – | 26–28 AWG silicone wire (or 30 AWG wire-wrap), ~2 m in several colours | |
| – | Kapton tape or small cable ties | wire dressing |

Tools: soldering iron (with a heat-set insert tip if you have one), flush cutters,
2.5 mm hex / Phillips #0 and #1 drivers, multimeter.

## 1 · Prepare the printed parts

1. Remove any stringing from the switch cut-outs and display windows.
2. Press the **8 heat-set inserts** into the boss ends of the top plate
   (see [printing.md](printing.md#heat-set-inserts)).
3. Stick the **rubber feet** into the four recesses under the base.

## 2 · Build the top plate

1. **Switches**: push all 12 switches into the plate from the top until both clips snap.
   Orient them the same way (e.g. the two metal pins towards the back).
2. **1.9" display**: lay the plate top-down, drop the module glass-first into its
   pocket (header towards the right-hand key block), and fix it with **4 × M2×4**.
   Snug, not tight – the screws cut their own thread.
3. **2.25" bar display**: drop it into the long pocket, header towards the *front*.
   Put the **spine** over its back (crossbar at the hole end, pad at the header end)
   and drive **2 × M2×8** through the spine and the module's two holes.
4. Peel the protective films off the display glass (from the top) now or at the very end.

## 3 · Hand-wire the matrix

Follow the [matrix diagram](wiring.md#key-matrix):

1. Solder a **1N4148** to one pin of every switch, cathode band pointing away from the
   switch, towards where the row wire will run.
2. **Rows** (4 wires): join the diode cathodes of each physical row – left key, then
   across under the display/joystick area to the two right keys. Row 0 is the back row.
3. **Columns** (3 wires): join the other switch pin of every key in a column, front to back.
4. Check with a multimeter (diode mode): column → row conducts through a pressed key only.

Keep wires flat against the plate underside and away from the joystick opening; a few
bits of Kapton tape help.

## 4 · Wire the modules and the Pico

Use the [wiring diagram](wiring.md). Cut every wire to reach the Pico's position at the
back centre **plus ~8 cm of slack**, so the top plate can be opened like a book later.

- Displays: 8 wires each, soldered from the back of the module (the relief grooves in
  the plate clear the solder on the glass side).
- Joystick: 5 wires. Remove the right-angle header or solder to it; the posts leave room
  for either. **Power it from 3V3, not 5 V.**
- Solder all wires to the Pico from the top.

Flash the firmware now (drop the UF2 on the `RP2350` drive while holding BOOTSEL) and
test every key, both displays and the stick before closing the case – the serial
console (`make monitor`, command `joy`) prints raw joystick values.

## 5 · Mount the joystick and the Pico in the base

1. Place the **frame** on the **base** (the lip segments locate it).
2. Set the **joystick** on its four posts, header to the left, click-switch towards the
   front, and fix it with **4 × M3×6** from above.
3. Insert the **Pico**: tilt it, push the USB-C receptacle into the opening in the back
   wall, lower the far end onto the two standoffs and fix it with **2 × M2×5**.
   A cable must now plug in fully from outside.

## 6 · Close the case

1. Fold the wires into the case (keep them out of the joystick opening and away from the
   stick's mechanism).
2. Lower the top plate onto the frame: the knob passes through its opening, the lip drops
   inside the frame and the bosses slide into their saddles.
3. Turn the pad over and drive the **8 × M3×8 countersunk** screws. Tighten evenly in a
   cross pattern until the seams close – do not overtighten.
4. Fit the keycaps.

## Opening it again

Remove the 8 bottom screws and lift the top plate; it stays connected to the Pico by the
wires. The Pico comes out with its two screws; tilt it out of the port.
