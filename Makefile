# Nyxilab macropad - monorepo tasks
#
#   joystick/   cad + firmware of the joystick edition (KY-023 thumbstick)
#   rotary/     cad + firmware of the rotary edition (KY-040 encoder + printed knob)
#   common/     the shared case model and firmware libraries
#
#   make setup             one-time: Python venv (CAD tools) + PlatformIO
#   make cad               both editions: checks, STEP/STL/3MF/GLB (+FreeCAD), renders, blueprints
#   make firmware          both editions for the Pico 2 (UF2 files in <edition>/firmware/dist)
#   make test              all unit tests on this PC (shared core, joystick maths, encoder decoder)
#   make docs              wiring diagrams, soldering figures, UI renders, 3D viewer, the website in site/
#   make site              the website only (what GitHub Pages serves), then: python3 -m http.server -d site
#   make all               everything above

PY          := $(CURDIR)/.venv/bin/python
FREECAD_CMD ?= $(shell command -v freecadcmd 2>/dev/null || command -v FreeCADCmd 2>/dev/null)
CMD         ?= all
EDITIONS    := joystick rotary

.PHONY: help setup cad cad-check cad-joystick cad-rotary firmware firmware-all firmware-joystick firmware-rotary \
        firmware-joystick-pico firmware-rotary-pico uf2 flash-joystick flash-rotary monitor test test-core \
        test-joystick test-rotary ui-sim diagrams viewer site docs all clean

help:
	@grep -E '^#   make' Makefile | sed 's/^#   //'
	@echo "  per edition:  cad-joystick cad-rotary (CMD=check|export|render|drawings|all)"
	@echo "                firmware-joystick firmware-rotary (+ -pico), flash-joystick flash-rotary"
	@echo "  more:         cad-check firmware-all test-core test-joystick test-rotary ui-sim diagrams viewer clean"

setup:
	python3 -m venv .venv
	$(PY) -m pip install -U pip
	$(PY) -m pip install -e "common/cad[render]" markdown
	command -v pio >/dev/null || pipx install platformio || python3 -m pip install --user platformio

# ------------------------------------------------------------------ CAD
cad: cad-joystick cad-rotary

cad-check:
	$(MAKE) cad-joystick CMD=check
	$(MAKE) cad-rotary CMD=check

cad-joystick:
	FREECAD_CMD="$(FREECAD_CMD)" $(PY) joystick/cad/build.py $(CMD)

cad-rotary:
	FREECAD_CMD="$(FREECAD_CMD)" $(PY) rotary/cad/build.py $(CMD)

# ------------------------------------------------------------- firmware
firmware: firmware-joystick firmware-rotary

firmware-all: firmware-joystick firmware-joystick-pico firmware-rotary firmware-rotary-pico

firmware-joystick:
	cd joystick/firmware && pio run -e pico2
	$(MAKE) uf2

firmware-joystick-pico:
	cd joystick/firmware && pio run -e pico
	$(MAKE) uf2

firmware-rotary:
	cd rotary/firmware && pio run -e pico2
	$(MAKE) uf2

firmware-rotary-pico:
	cd rotary/firmware && pio run -e pico
	$(MAKE) uf2

uf2:
	@for ed in $(EDITIONS); do \
	  mkdir -p $$ed/firmware/dist; \
	  for env in pico2 pico; do \
	    f=$$ed/firmware/.pio/build/$$env/firmware.uf2; \
	    [ -f $$f ] && cp $$f $$ed/firmware/dist/nyxilab-macropad-$$ed-$$env.uf2 && echo "  $$ed/firmware/dist/nyxilab-macropad-$$ed-$$env.uf2" || true; \
	  done; \
	done

flash-joystick:
	cd joystick/firmware && pio run -e pico2 -t upload

flash-rotary:
	cd rotary/firmware && pio run -e pico2 -t upload

monitor:
	pio device monitor -b 115200

# ---------------------------------------------------------------- tests
test: test-core test-joystick test-rotary

test-core:
	cd common/firmware && pio test -e native

test-joystick:
	cd joystick/firmware && pio test -e native

test-rotary:
	cd rotary/firmware && pio test -e native

# ----------------------------------------------------------------- docs
ui-sim:
	$(PY) tools/ui_sim/run.py

diagrams:
	$(PY) tools/wiring_diagram.py
	$(PY) tools/wiring_map.py
	$(PY) tools/soldering_figures.py

viewer:
	$(PY) tools/build_viewer.py

site:
	$(PY) tools/build_site.py

docs: ui-sim diagrams viewer site

all: cad firmware-all test docs

clean:
	rm -rf site .build */firmware/.pio common/firmware/.pio
