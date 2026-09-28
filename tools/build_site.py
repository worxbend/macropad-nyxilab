#!/usr/bin/env python3
"""Build the website that GitHub Pages serves (the guides, the 3D viewer, downloads).

    python3 tools/build_site.py          -> site/
    python3 -m http.server -d site       to look at it locally

Every Markdown guide becomes a page with the same shell as the viewer.  Links
between the guides, to the images and to files in the repo are rewritten, so the
same Markdown works on GitHub and on the site.  Needs the `markdown` package.
The workflow in .github/workflows/pages.yml runs this on every push to main.
"""

from __future__ import annotations

import html
import re
import shutil
import zipfile
from pathlib import Path

import markdown

REPO = Path(__file__).resolve().parents[1]
SITE = REPO / "site"
GITHUB = "https://github.com/worxbend/macropad-nyxilab"
BLOB = f"{GITHUB}/blob/main/"
TREE = f"{GITHUB}/tree/main/"

# (source markdown, page name, nav title) - the order is the navigation order
PAGES = [
    ("README.md", "index", "Home"),
    ("docs/design.md", "design", "Design"),
    ("docs/printing.md", "printing", "Printing"),
    ("docs/soldering.md", "soldering", "Soldering"),
    ("docs/wiring.md", "wiring", "Wiring"),
    ("docs/assembly.md", "assembly", "Assembly"),
    ("joystick/README.md", "joystick", "Joystick edition"),
    ("rotary/README.md", "rotary", "Rotary edition"),
    ("common/firmware/README.md", "firmware", "Firmware"),
    ("common/cad/README.md", "cad", "CAD"),
]
EXTRA_NAV = [("viewer.html", "3D viewer"), ("downloads.html", "Downloads")]

STYLE = """
:root {
  --ground: #edecf2; --surface: #ffffff; --ink: #1b1a21; --muted: #666372; --line: #dcd9e4;
  --accent: #5b3e8e; --accent-ink: #ffffff; --code: #f4f2f8; --warn: #b45309; --tip: #1f6f8b;
}
@media (prefers-color-scheme: dark) {
  :root { color-scheme: dark; --ground: #111016; --surface: #19181f; --ink: #eceaf3; --muted: #9d99ad;
          --line: #2b2935; --accent: #a58ddb; --accent-ink: #15131b; --code: #22202b; --warn: #f5b25a; --tip: #7fc4dc; }
}
* { box-sizing: border-box; }
html { background: var(--ground); }
body { margin: 0; color: var(--ink); background: var(--ground);
       font: 15px/1.6 "IBM Plex Sans", system-ui, -apple-system, "Segoe UI", sans-serif; }
nav.top { position: sticky; top: 0; z-index: 5; background: var(--surface); border-bottom: 1px solid var(--line);
          padding: 0 16px; }
nav.top .in { max-width: 1040px; margin: 0 auto; display: flex; flex-wrap: wrap; align-items: center; gap: 4px 14px;
              padding: 10px 0; }
nav.top .brand { font-weight: 600; margin-right: 10px; color: var(--ink); text-decoration: none; }
nav.top a { color: var(--muted); text-decoration: none; font-size: 13.5px; padding: 4px 6px; border-radius: 6px; }
nav.top a:hover, nav.top a.on { color: var(--accent); background: var(--ground); }
main { max-width: 1040px; margin: 0 auto; padding: 28px 16px 60px; }
article { background: var(--surface); border: 1px solid var(--line); border-radius: 12px; padding: 32px 36px; }
h1, h2, h3 { line-height: 1.2; text-wrap: balance; }
h1 { font-size: 30px; margin: 0 0 12px; }
h2 { font-size: 22px; margin: 36px 0 10px; padding-top: 8px; border-top: 1px solid var(--line); }
h3 { font-size: 17px; margin: 24px 0 8px; }
a { color: var(--accent); }
img { max-width: 100%; height: auto; border-radius: 6px; }
p, li { max-width: 78ch; }
code, pre { font-family: "IBM Plex Mono", ui-monospace, SFMono-Regular, Menlo, monospace; font-size: 13px; }
code { background: var(--code); padding: 1px 5px; border-radius: 4px; }
pre { background: var(--code); padding: 14px 16px; border-radius: 8px; overflow-x: auto; }
pre code { background: none; padding: 0; }
table { border-collapse: collapse; margin: 12px 0; display: block; overflow-x: auto; max-width: 100%; }
th, td { border: 1px solid var(--line); padding: 6px 10px; text-align: left; vertical-align: top; font-size: 14px; }
th { background: var(--code); }
td img { max-width: 440px; }
blockquote { margin: 14px 0; padding: 10px 16px; border-left: 4px solid var(--line); color: var(--muted); }
.alert { border-left: 4px solid var(--tip); background: var(--code); padding: 10px 16px; border-radius: 0 8px 8px 0;
         margin: 14px 0; }
.alert.warning { border-color: var(--warn); }
.alert .t { font-weight: 600; display: block; margin-bottom: 4px; }
.alert.warning .t { color: var(--warn); } .alert.tip .t, .alert.note .t { color: var(--tip); }
footer { text-align: center; color: var(--muted); font-size: 12.5px; margin-top: 30px; }
.downloads td:first-child { white-space: nowrap; }
@media (max-width: 640px) { article { padding: 20px 16px; } h1 { font-size: 24px; } }
"""

SHELL = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{title} · Nyxilab macropad</title>
<meta name="description" content="{description}">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=IBM+Plex+Mono:wght@400;500&family=IBM+Plex+Sans:wght@400;500;600&display=swap">
<style>{style}</style>
</head>
<body>
<nav class="top"><div class="in"><a class="brand" href="index.html">🌙 nyxilab macropad</a>{nav}</div></nav>
<main><article>
{body}
</article>
<footer>generated from the repository's Markdown · <a href="{github}">source on GitHub</a></footer>
</main>
{mermaid}
</body>
</html>
"""

MERMAID = ('<script type="module">import mermaid from "https://cdn.jsdelivr.net/npm/mermaid@11/dist/mermaid.esm.min.mjs";'
           'mermaid.initialize({startOnLoad: true, theme: matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "default"});'
           '</script>')


def page_for(repo_path: str) -> str | None:
    for src, name, _ in PAGES:
        if src == repo_path:
            return f"{name}.html"
    return None


def rewrite_link(href: str, src_dir: Path) -> str:
    """Repo-relative Markdown link -> site link (page, image, viewer) or GitHub URL."""
    if re.match(r"^(https?:|mailto:|#)", href):
        return href
    target, _, anchor = href.partition("#")
    anchor = f"#{anchor}" if anchor else ""
    repo_path = (src_dir / target).resolve()
    try:
        rel = repo_path.relative_to(REPO).as_posix()
    except ValueError:
        return href
    if (page := page_for(rel)):
        return page + anchor
    if rel == "docs/viewer.html":
        return "viewer.html" + anchor
    if rel.startswith("docs/images/"):
        return rel[len("docs/"):]
    if repo_path.is_dir():
        return TREE + rel
    return BLOB + rel


def preprocess(md: str) -> str:
    """GitHub alerts (> [!WARNING] ...) -> styled divs; mermaid fences -> <pre class="mermaid">."""
    out, i = [], 0
    lines = md.split("\n")
    while i < len(lines):
        m = re.match(r"^> \[!(NOTE|TIP|WARNING|IMPORTANT|CAUTION)\]\s*$", lines[i])
        if m:
            kind = m.group(1).lower()
            body = []
            i += 1
            while i < len(lines) and lines[i].startswith(">"):
                body.append(lines[i][1:].lstrip())
                i += 1
            inner = markdown.markdown("\n".join(body), extensions=["tables"])
            title = {"warning": "Warning", "caution": "Caution", "important": "Important", "tip": "Tip", "note": "Note"}[kind]
            cls = "warning" if kind in ("warning", "caution", "important") else kind
            out.append(f'<div class="alert {cls}"><span class="t">{title}</span>{inner}</div>')
            continue
        if lines[i].strip() == "```mermaid":
            body = []
            i += 1
            while i < len(lines) and lines[i].strip() != "```":
                body.append(lines[i])
                i += 1
            i += 1
            out.append('<pre class="mermaid">\n' + html.escape("\n".join(body)) + "\n</pre>")
            continue
        # Markdown inside <div> blocks (GitHub renders it; Python-Markdown needs to be told)
        out.append(re.sub(r'^<div(?![^>]*markdown=)([^>]*)>', r'<div\1 markdown="1">', lines[i]))
        i += 1
    return "\n".join(out)


def render(src: str, name: str, title: str) -> str:
    path = REPO / src
    md = preprocess(path.read_text())
    body = markdown.markdown(md, extensions=["tables", "fenced_code", "toc", "sane_lists", "md_in_html"],
                             extension_configs={"toc": {"permalink": False}})
    src_dir = path.parent

    def fix(m):
        attr, quote, href = m.group(1), m.group(2), m.group(3)
        return f'{attr}={quote}{rewrite_link(href, src_dir)}{quote}'

    body = re.sub(r'\b(href|src)=(["\'])([^"\']+)\2', fix, body)
    body = body.replace('<a href="https://', '<a target="_blank" rel="noopener" href="https://')
    first_p = re.search(r"<p>(.*?)</p>", re.sub(r"<[^>]+>", "", body), re.S)
    desc = html.escape(re.sub(r"\s+", " ", first_p.group(1))[:180]) if first_p else "Nyxilab macropad"
    nav = "".join(f'<a href="{n}.html"{" class=on" if n == name else ""}>{t}</a>' for _, n, t in PAGES[1:])
    nav = "".join(f'<a href="{h}"{" class=on" if h == name + ".html" else ""}>{t}</a>' for h, t in EXTRA_NAV) + nav
    return SHELL.format(title=html.escape(title), description=desc, style=STYLE, nav=nav, body=body,
                        github=GITHUB, mermaid=MERMAID if 'class="mermaid"' in body else "")


def downloads_page() -> str:
    rows = []
    dl = SITE / "downloads"
    dl.mkdir(parents=True, exist_ok=True)
    for ed in ("joystick", "rotary"):
        for env, board in (("pico2", "Pico 2 (RP2350)"), ("pico", "Pico (RP2040)")):
            f = REPO / ed / "firmware" / "dist" / f"nyxilab-macropad-{ed}-{env}.uf2"
            if f.exists():
                shutil.copy(f, dl / f.name)
                rows.append((f"{ed} edition firmware, {board}", f.name, f.stat().st_size))
        pdf = REPO / ed / "cad" / "exports" / "drawings" / f"nyxilab_macropad_{ed}_blueprints.pdf"
        if pdf.exists():
            shutil.copy(pdf, dl / pdf.name)
            rows.append((f"{ed} edition blueprints (5 × A3)", pdf.name, pdf.stat().st_size))
        tpl = REPO / ed / "cad" / "exports" / "drawings" / "template_top_plate_1to1.pdf"
        if tpl.exists():
            shutil.copy(tpl, dl / f"template_top_plate_1to1_{ed}.pdf")
            rows.append((f"{ed} edition 1:1 paper template of the top plate", f"template_top_plate_1to1_{ed}.pdf",
                         tpl.stat().st_size))
        for fmt in ("3mf", "stl"):
            src = REPO / ed / "cad" / "exports" / fmt
            if src.exists():
                zname = f"nyxilab-macropad-{ed}-{fmt}.zip"
                with zipfile.ZipFile(dl / zname, "w", zipfile.ZIP_DEFLATED) as z:
                    for f in sorted(src.rglob("*")):
                        if f.is_file():
                            z.write(f, f.relative_to(src))
                rows.append((f"{ed} edition print files ({fmt.upper()}, incl. fit tests)", zname, (dl / zname).stat().st_size))
    trs = "".join(f"<tr><td>{html.escape(w)}</td><td><a href=\"downloads/{n}\">{n}</a></td><td>{s / 1e6:.1f} MB</td></tr>"
                  for w, n, s in rows)
    body = ("<h1>Downloads</h1><p>Everything you need to build one, straight from the repository's latest build. "
            "The joystick and the rotary edition share the frame and the bar-display spine; pick the files of the "
            "edition you are building.</p>"
            f'<table class="downloads"><thead><tr><th>What</th><th>File</th><th>Size</th></tr></thead><tbody>{trs}</tbody></table>'
            f'<p>Editable CAD (STEP, FreeCAD) and the sources are in the <a href="{GITHUB}">repository</a>.</p>')
    nav = "".join(f'<a href="{h}"{" class=on" if h == "downloads.html" else ""}>{t}</a>' for h, t in EXTRA_NAV)
    nav += "".join(f'<a href="{n}.html">{t}</a>' for _, n, t in PAGES[1:])
    return SHELL.format(title="Downloads", description="Firmware, print files and blueprints of the Nyxilab macropad",
                        style=STYLE, nav=nav, body=body, github=GITHUB, mermaid="")


def main():
    if SITE.exists():
        shutil.rmtree(SITE)
    SITE.mkdir()
    shutil.copytree(REPO / "docs" / "images", SITE / "images")
    shutil.copy(REPO / "docs" / "viewer.html", SITE / "viewer.html")
    (SITE / ".nojekyll").write_text("")
    for src, name, title in PAGES:
        if not (REPO / src).exists():
            print("  missing", src)
            continue
        (SITE / f"{name}.html").write_text(render(src, name, title))
    (SITE / "downloads.html").write_text(downloads_page())
    n = sum(1 for _ in SITE.rglob("*") if _.is_file())
    size = sum(f.stat().st_size for f in SITE.rglob("*") if f.is_file()) / 1e6
    print(f"wrote {SITE} ({n} files, {size:.1f} MB)")


if __name__ == "__main__":
    main()
