# Build guide

From printed parts to a working pad, in order. This page is the route; each stage links
to the page with the details and pictures. Do the test at the end of every stage before
you start the next one: a fault is easy to fix on the bench and hard to fix in a closed case.

| Stage | What you do | Time |
|---|---|---|
| [1 · Prepare](#1-prepare) | parts, tools, inserts, switches and modules into the plate | 1 h |
| [2 · Solder the matrix](#2-solder-the-matrix) | 12 diodes, 4 row wires, 3 column wires | 2-3 h |
| [3 · Solder the modules](#3-solder-the-modules) | wires onto the displays, the centre control, the LED sticks | 2 h |
| [4 · Wire the Pico](#4-wire-the-pico) | all 31 wires onto the Pico | 1-2 h |
| [5 · Flash the firmware](#5-flash-the-firmware) | UF2 onto the Pico, bench test | 10 min |
| [6 · Assemble](#6-assemble) | everything into the base, close the case | 30 min |

## 1. Prepare

1. Collect the parts and tools: [bill of materials](assembly.md#bill-of-materials),
   [soldering tools](soldering.md#tools-and-materials).
2. Print the fit tests, then the case: [printing](printing.md).
3. Press the 8 heat-set inserts into the top plate and stick the feet under the base:
   [assembly, step 1](assembly.md#1--prepare-the-printed-parts).
4. Clip the 12 switches into the plate. Screw in both displays. Rotary edition: fit the
   encoder with its nut. [Assembly, step 2](assembly.md#2--build-the-top-plate).

**Test:** every switch sits flush, both clips snapped. No display moves in its pocket.

## 2. Solder the matrix

New to soldering? Read [soldering basics](soldering.md#soldering-basics-60-seconds) first.
Work with the plate upside down. Left and right are mirrored in that view.

1. Solder one 1N4148 diode to pin A of every switch, black band away from the switch:
   [step 1](soldering.md#step-1--diodes-on-the-switches).
2. Join the diode legs of each row into a row wire. Row 0 is the back row:
   [step 2](soldering.md#step-2--row-wires).
3. Run one insulated wire per column over pin B of each switch:
   [step 3](soldering.md#step-3--column-wires).
4. Leave a labelled 15 cm tail on every row and column: R0-R3, C0-C2.

**Test:** multimeter in diode mode, red probe on a column tail, black probe on a row tail.
A pressed key reads about 0.6 V. A released key reads open. Check all 12 keys:
[step 4](soldering.md#step-4--test-the-matrix-before-anything-else).

## 3. Solder the modules

Cut each wire from the plate long enough to reach the Pico plus about 8 cm of slack.
The plate must open like a book later.

1. Displays: 8 wires each, soldered to the back of the module:
   [step 5](soldering.md#step-5--the-displays).
2. Joystick or encoder: 5 wires: [step 6](soldering.md#step-6--the-joystick-or-the-encoder).
3. LED sticks: 3 wires from the right stick to the Pico, with the 1N4001 in the 5V wire
   (band towards the stick) and the 330 Ω resistor in the data wire. Then a 3-wire jumper
   from the right stick's DOUT end to the left stick's DIN end:
   [step 7](soldering.md#step-7--the-led-sticks).

**Test:** continuity mode. No two neighbouring header pins are joined. 5V does not touch
GND on either LED stick.

## 4. Wire the Pico

Solder every wire to the **top** side of the Pico. There is no room under it:
[step 8](soldering.md#step-8--the-pico).

Work through the [pin map](wiring.md#pin-map) one group at a time. The wiring diagram for
your edition is at the top of the [wiring page](wiring.md). The short version:

| Group | Pico pins |
|---|---|
| Rows R0-R3 | GP2-GP5 |
| Columns C0-C2 | GP6-GP8 |
| 2.25" bar display | GP10-GP15 |
| 1.9" display | GP16-GP21 |
| Joystick or encoder | GP22, GP26, GP27 |
| LED data | GP28, through the 330 Ω resistor |
| Module power | **3V3 (pin 36)** for both displays and the joystick or encoder |
| LED power | **VBUS (pin 40)**, through the 1N4001 |

> [!WARNING]
> Power the joystick or the encoder from **3V3**, even though its pin is labelled +5V.
> 5 V there reaches the Pico's GPIOs. Only the LED sticks take 5 V.

**Test, before any USB cable:** 3V3 to GND, VBUS to GND and 3V3 to VBUS must all read
open: [before you plug it in](soldering.md#before-you-plug-it-in).

## 5. Flash the firmware

Flash on the bench, with the case still open.

1. Pick the UF2 for your edition and board:

   | | Pico 2 (RP2350) | Pico (RP2040) |
   |---|---|---|
   | Joystick | `joystick/firmware/dist/nyxilab-macropad-joystick-pico2.uf2` | `joystick/firmware/dist/nyxilab-macropad-joystick-pico.uf2` |
   | Rotary | `rotary/firmware/dist/nyxilab-macropad-rotary-pico2.uf2` | `rotary/firmware/dist/nyxilab-macropad-rotary-pico.uf2` |

2. Hold the **BOOTSEL** button on the Pico and plug in the USB cable. Release the button.
3. A drive named `RP2350` (Pico 2) or `RPI-RP2` (Pico) appears. Copy the UF2 onto it.
4. The drive disappears and the pad restarts. Both screens come up within a second and
   the LEDs light violet.

To build the firmware yourself, run `make flash-joystick` or `make flash-rotary`. It needs
PlatformIO (`make setup`): [firmware README](../common/firmware/README.md#build--flash).

**Test:**

1. Press every key. The 1.9" screen highlights it and the LEDs flash.
2. Move the stick or turn and press the knob.
3. Optional: `make monitor`, then type `info`. The commands `joy` and `enc` print raw values.

Something wrong? Use the [troubleshooting table](soldering.md#troubleshooting). An
upside-down screen or a reversed stick or knob is a setting in `config.h`, not a wiring
fault: [firmware README](../common/firmware/README.md).

### Flashing again later

The closed case hides BOOTSEL. Use one of these instead:

- `make flash-joystick` or `make flash-rotary`: the pad restarts into the bootloader itself.
- Hold the **back-left + front-right** keys while you plug in the cable.
- Press **FN + BOOT**: hold the front-left key, press the back-left key.

Then copy the UF2 onto the drive as before.

## 6. Assemble

Unplug the USB cable first.

1. Screw the two LED sticks onto their posts in the base, LEDs up, 2 × M2×5 each.
2. Put the frame on the base.
3. Joystick edition: screw the joystick onto its four posts, 4 × M3×6.
4. Tilt the Pico into the USB opening in the back wall and fix it with 2 × M2×5.
5. Fold the wires in. Keep them off the LEDs and away from the stick or the encoder.
6. Lower the top plate onto the frame. Turn the pad over and drive the 8 × M3×8 screws
   in a cross pattern. Do not overtighten.
7. Fit the keycaps. Rotary edition: press the knob on until it stops.

Details for every step: [assembly, steps 5 and 6](assembly.md#5--mount-the-led-sticks-the-joystick-and-the-pico-in-the-base).

**Test:** a USB-C cable plugs in fully from outside. Every key still works. The stick
moves freely, or the knob turns and clicks.
