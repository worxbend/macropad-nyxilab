# Nyxilab macropad - monorepo tasks
#   make setup      one-time: CAD virtualenv + PlatformIO
#   make cad        checks, STEP/STL/3MF/GLB (+FreeCAD), renders, blueprints
#   make firmware   build the Pico 2 firmware (UF2 copied to firmware/dist)
#   make flash      build + upload over USB
#   make test       unit tests of the firmware logic on this PC

PY          := $(CURDIR)/cad/.venv/bin/python
FREECAD_CMD ?= $(shell command -v freecadcmd 2>/dev/null || command -v FreeCADCmd 2>/dev/null)
CAD         := cd cad && FREECAD_CMD="$(FREECAD_CMD)" PYTHONPATH=. $(PY) -m macropad_cad

.PHONY: help setup cad cad-check cad-export cad-render cad-drawings firmware firmware-pico uf2 flash monitor test ui-sim diagrams viewer all

help:
	@grep -E '^#   make' Makefile | sed 's/^#   //'
	@echo "  other: cad-check cad-export cad-render cad-drawings firmware-pico uf2 monitor ui-sim diagrams viewer all"

setup:
	python3 -m venv cad/.venv
	$(PY) -m pip install -U pip
	$(PY) -m pip install -e "cad[render]"
	command -v pio >/dev/null || pipx install platformio || python3 -m pip install --user platformio

cad:
	$(CAD) all

cad-check:
	$(CAD) check

cad-export:
	$(CAD) export

cad-render:
	$(CAD) render

cad-drawings:
	$(CAD) drawings

firmware:
	cd firmware && pio run -e pico2
	$(MAKE) uf2

firmware-pico:
	cd firmware && pio run -e pico
	$(MAKE) uf2

uf2:
	mkdir -p firmware/dist
	for env in pico2 pico; do \
	  f=firmware/.pio/build/$$env/firmware.uf2; \
	  [ -f $$f ] && cp $$f firmware/dist/nyxilab-macropad-$$env.uf2 || true; \
	done

flash:
	cd firmware && pio run -e pico2 -t upload

monitor:
	cd firmware && pio device monitor

test:
	cd firmware && pio test -e native

ui-sim:
	python3 firmware/tools/ui_sim/run.py

diagrams:
	$(PY) docs/tools/wiring_diagram.py

viewer:
	python3 docs/tools/build_viewer.py

all: cad firmware firmware-pico test ui-sim diagrams viewer
