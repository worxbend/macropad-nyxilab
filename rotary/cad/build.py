#!/usr/bin/env python3
"""Build the rotary edition of the case.  Run from anywhere:

    python rotary/cad/build.py [check|export|render|drawings|all] [--no-freecad]

Outputs: rotary/cad/exports/ and docs/images/rotary/.  Needs the shared package in
common/cad (installed into the repo's .venv by `make setup`).
"""

import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path[:0] = [str(HERE), str(HERE.parents[1] / "common" / "cad")]  # edition.py + the shared package

from macropad_cad.cli import main  # noqa: E402
from edition import EDITION  # noqa: E402

if __name__ == "__main__":
    sys.exit(main(EDITION, HERE))
