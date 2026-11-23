"""
UTF-8 panel text tables in textdraw.h — host only, no device needed.

    python tests/run.py --only text

The panel's only font is CP437, so every accented letter is drawn by looking
its codepoint up in UTF8_TO_CP437 and every emoji by looking it up in EMOJIS.
A wrong hex digit in either table draws the wrong glyph with no error
anywhere, so both are checked against an independent source: Python's own
cp437 codec, and the shape rules the C++ drawing loop assumes.
"""
from __future__ import annotations

import re
from pathlib import Path

from harness import test, eq, ok

TD = Path(__file__).resolve().parent.parent / "firmware" / "DigiFrame" / "textdraw.h"


def _src() -> str:
    return TD.read_text(encoding="utf-8")


def _emojis() -> list[tuple[int, int, str]]:
    """(codepoint, palette entries that are non-zero, 64-char pixel string)"""
    out = []
    body = _src().split("static const Emoji8 EMOJIS[] = {", 1)[1].split("};", 1)[0]
    for m in re.finditer(r"\{0x([0-9A-Fa-f]+),\s*\{([^}]*)\},[^\n]*\n((?:\s*\"[^\"]*\"\s*\n?)+)\}", body):
        pal = [p.strip() for p in m.group(2).split(",")]
        used = sum(1 for p in pal if p and p != "0")
        px = "".join(re.findall(r"\"([^\"]*)\"", m.group(3)))
        out.append((int(m.group(1), 16), used, px))
    return out


@test("text")
def test_cp437_map_matches_the_real_code_page():
    pairs = re.findall(r"\{0x([0-9A-Fa-f]{4}), 0x([0-9A-Fa-f]{2})\}", _src())
    ok(len(pairs) >= 7, "UTF8_TO_CP437 parsed")
    for cp, glyph in pairs:
        want = chr(int(cp, 16))
        got = bytes([int(glyph, 16)]).decode("cp437")
        eq(got, want, f"U+{cp} maps to CP437 0x{glyph}")


@test("text")
def test_german_letters_are_all_mapped():
    mapped = {chr(int(c, 16)) for c, _ in re.findall(r"\{0x([0-9A-Fa-f]{4}), 0x([0-9A-Fa-f]{2})\}", _src())}
    for ch in "ÄÖÜäöüß":
        ok(ch in mapped, f"{ch} is drawable")


@test("text")
def test_emoji_bitmaps_are_well_formed():
    ems = _emojis()
    ok(len(ems) >= 10, f"EMOJIS parsed ({len(ems)})")
    seen = set()
    for cp, used, px in ems:
        name = f"U+{cp:04X}"
        ok(cp not in seen, f"{name} listed once")
        seen.add(cp)
        eq(len(px), 64, f"{name} is 8x8")
        ok(set(px) <= set(".1234"), f"{name} uses only '.', '1'-'4'")
        top = max((int(c) for c in px if c != "."), default=0)
        ok(top <= used, f"{name} references palette {top} but defines {used} colours")
        ok(any(c != "." for c in px), f"{name} is not blank")


@test("text")
def test_emoji_are_not_ascii_or_mapped_letters():
    """drawText() checks ASCII first and the letter map last; an emoji
    codepoint in either range would never be reached."""
    letters = {int(c, 16) for c in re.findall(r"\{0x([0-9A-Fa-f]{4}), 0x[0-9A-Fa-f]{2}\}", _src())}
    for cp, _, _ in _emojis():
        ok(cp >= 0x80 and cp not in letters, f"U+{cp:04X} is reachable")
