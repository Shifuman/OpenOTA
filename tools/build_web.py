#!/usr/bin/env python3
"""Minifie et gzippe web/index.html vers src/OpenOTAPage.h (tableau PROGMEM).

Usage:  python3 tools/build_web.py
"""
import gzip
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "web" / "index.html"
OUT = ROOT / "src" / "OpenOTAPage.h"


def minify(html: str) -> str:
    # commentaires HTML (on garde les conditionnels, il n'y en a pas ici)
    html = re.sub(r"<!--(?!\[if).*?-->", "", html, flags=re.S)
    # commentaires CSS/JS sur une ligne du type /* ... */
    html = re.sub(r"/\*.*?\*/", "", html, flags=re.S)
    # indentation en debut de ligne
    html = re.sub(r"\n\s+", "\n", html)
    # lignes vides
    html = re.sub(r"\n{2,}", "\n", html)
    return html.strip()


def main() -> int:
    if not SRC.exists():
        print(f"introuvable: {SRC}", file=sys.stderr)
        return 1

    raw = SRC.read_text(encoding="utf-8")
    small = minify(raw)
    blob = gzip.compress(small.encode("utf-8"), 9)

    lines = []
    for i in range(0, len(blob), 16):
        chunk = blob[i:i + 16]
        lines.append("  " + ", ".join(f"0x{b:02x}" for b in chunk))

    OUT.write_text(
        "// Fichier genere par tools/build_web.py — ne pas editer a la main.\n"
        "// Source: web/index.html\n"
        "#pragma once\n"
        "#include <Arduino.h>\n\n"
        f"#define OPENOTA_PAGE_LEN {len(blob)}\n\n"
        "const uint8_t OPENOTA_PAGE[] PROGMEM = {\n"
        + ",\n".join(lines)
        + "\n};\n",
        encoding="utf-8",
    )

    print(f"brut     {len(raw):6d} o")
    print(f"minifie  {len(small):6d} o")
    print(f"gzip     {len(blob):6d} o  -> {OUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
