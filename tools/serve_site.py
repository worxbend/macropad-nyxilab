#!/usr/bin/env python3
"""Work on the website with live reload.

    make site-dev                 (= python tools/serve_site.py [--port 8000])

Builds site/ with tools/build_site.py, serves it, and watches the sources: web/,
docs/ (guides, images, viewer), the READMEs and the site builder.  On a save it
rebuilds and every open page reloads itself.  Pages get a small polling script
injected while they are served; the files in site/ stay exactly what Pages deploys.
"""

from __future__ import annotations

import argparse
import functools
import http.server
import subprocess
import sys
import threading
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
SITE = REPO / "site"
WATCH = [REPO / "web", REPO / "docs", REPO / "tools" / "build_site.py", REPO / "README.md",
         REPO / "joystick" / "README.md", REPO / "rotary" / "README.md",
         REPO / "common" / "firmware" / "README.md", REPO / "common" / "cad" / "README.md"]
RELOAD = b"""<script>(() => { let seen = null; setInterval(async () => {
  try { const v = await (await fetch("/__build", {cache: "no-store"})).text();
        if (seen !== null && v !== seen) location.reload(); seen = v; } catch (e) {} }, 700); })();</script>"""

build_id = 0


def snapshot() -> dict[Path, float]:
    files = {}
    for root in WATCH:
        for f in ([root] if root.is_file() else root.rglob("*")):
            if f.is_file():
                files[f] = f.stat().st_mtime
    return files


def build() -> None:
    global build_id
    started = time.monotonic()
    res = subprocess.run([sys.executable, str(REPO / "tools" / "build_site.py")], capture_output=True, text=True)
    out = (res.stdout + res.stderr).strip().splitlines()
    status = "ok" if res.returncode == 0 else "FAILED"
    print(f"[{time.strftime('%H:%M:%S')}] build {status} in {time.monotonic() - started:.1f}s: {out[-1] if out else ''}")
    if res.returncode:
        print("\n".join(out))
    build_id += 1


def watch() -> None:
    seen = snapshot()
    while True:
        time.sleep(0.5)
        now = snapshot()
        if now != seen:
            changed = sorted(str(f.relative_to(REPO)) for f in set(now) ^ set(seen) | {f for f in now if seen.get(f) != now[f]})
            print("changed:", ", ".join(changed[:4]) + (" ..." if len(changed) > 4 else ""))
            build()
            seen = snapshot()


class Handler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/__build":
            body = str(build_id).encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        path = Path(self.translate_path(self.path))
        if path.is_dir():
            path = path / "index.html"
        if path.suffix == ".html" and path.is_file():
            body = path.read_bytes().replace(b"</body>", RELOAD + b"</body>", 1)
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)
            return
        super().do_GET()

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, *args):  # the build log is the useful output
        pass


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=8000)
    port = ap.parse_args().port
    build()
    threading.Thread(target=watch, daemon=True).start()
    handler = functools.partial(Handler, directory=str(SITE))
    with http.server.ThreadingHTTPServer(("127.0.0.1", port), handler) as httpd:
        print(f"serving http://127.0.0.1:{port}/  (Ctrl+C to stop)")
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
