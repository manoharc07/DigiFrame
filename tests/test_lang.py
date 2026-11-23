"""
UI string tables (firmware/lang/*.json) — host only, no device needed.

    python tests/run.py --only lang

Every user-visible string is looked up by key through tr() (i18n.h). These
tests pin the rules that keep that safe, all of which fail silently on the
device if broken:

  * every language carries exactly English's key set — a missing key falls
    back to English, which looks like a translation nobody finished;
  * placeholders match — a German "%d" where English has "%s" is undefined
    behaviour inside trf()'s vsnprintf, not just a wrong word;
  * panel.* is ASCII plus the letters textdraw.h maps onto CP437 glyphs —
    the panel's 5x7 font is CP437, and anything outside that table prints
    as '?';
  * lang_data.h was regenerated — the firmware embeds that file, not the
    JSON, so an edit without `make_lang.ps1` ships the old strings;
  * every key the code names exists, and every key in the table is used.
"""
from __future__ import annotations

import json
import re
from pathlib import Path

from harness import test, eq, ok

ROOT = Path(__file__).resolve().parent.parent
LANG_DIR = ROOT / "firmware" / "lang"
FW_DIR = ROOT / "firmware" / "DigiFrame"

# keys read through a computed name, so no literal names them
DYNAMIC = [re.compile(r"^panel\.dow\.[0-6]$"), re.compile(r"^web\.mode\.[0-6]$"),
           re.compile(r"^lang\.name$")]


def _tables() -> dict[str, dict[str, str]]:
    out = {}
    for f in sorted(LANG_DIR.glob("*.json")):
        out[f.stem] = json.loads(f.read_text(encoding="utf-8"))
    return out


def _printf(s: str) -> list[str]:
    return re.findall(r"%[-+ #0]*\d*(?:\.\d+)?[a-zA-Z%]", s)


def _braces(s: str) -> list[str]:
    return sorted(re.findall(r"\{\d+\}", s))


def _panel_letters() -> set[str]:
    """Every non-ASCII letter textdraw.h can draw, read from its own table."""
    td = (FW_DIR / "textdraw.h").read_text(encoding="utf-8")
    return {chr(int(h, 16)) for h in re.findall(r"\{0x([0-9A-Fa-f]{4}), 0x[0-9A-Fa-f]{2}\}", td)}


def _sources() -> str:
    return "\n".join(p.read_text(encoding="utf-8", errors="replace")
                     for p in sorted(FW_DIR.glob("*.h")) + [FW_DIR / "DigiFrame.ino"]
                     if p.name != "lang_data.h")


@test("lang")
def test_english_is_present_and_first():
    t = _tables()
    ok("en" in t, "firmware/lang/en.json exists")
    hdr = (FW_DIR / "lang_data.h").read_text(encoding="utf-8")
    m = re.search(r"LANG_CODES\[LANG_COUNT\] = \{ ([^}]*) \}", hdr)
    ok(m, "lang_data.h declares LANG_CODES")
    codes = re.findall(r'"([^"]+)"', m.group(1))
    eq(codes[0], "en", "English is language 0, the fallback")
    eq(sorted(codes), sorted(t), "every lang/*.json is compiled in")


@test("lang")
def test_every_language_has_the_english_key_set():
    t = _tables()
    ref = set(t["en"])
    for code, tab in t.items():
        missing, extra = ref - set(tab), set(tab) - ref
        ok(not missing and not extra,
           f"{code}.json: missing {sorted(missing)} extra {sorted(extra)}")


@test("lang")
def test_values_are_all_strings():
    for code, tab in _tables().items():
        bad = [k for k, v in tab.items() if not isinstance(v, str) or not v]
        ok(not bad, f"{code}.json: non-string or empty values {bad}")


@test("lang")
def test_placeholders_match_english():
    t = _tables()
    for code, tab in t.items():
        for k, en in t["en"].items():
            v = tab[k]
            if k.startswith("web."):
                eq(_braces(v), _braces(en), f"{code}:{k} {{n}} placeholders")
                # web.* is filled by the page's F(), which knows nothing of printf
                eq(_printf(v), [], f"{code}:{k} has a printf specifier")
            else:
                eq(_printf(v), _printf(en), f"{code}:{k} printf specifiers")


@test("lang")
def test_panel_strings_are_drawable_and_fit():
    t = _tables()
    letters = _panel_letters()
    ok("ä" in letters and "ß" in letters, "textdraw.h maps the German letters")
    for code, tab in t.items():
        for k, v in tab.items():
            if k.startswith("panel."):
                bad = [c for c in v if not (0x20 <= ord(c) < 0x7F or c in letters)]
                ok(not bad, f"{code}:{k} has {bad}, which the panel font cannot draw")
        # the footer date sits at x=2 and the plant starts at x=41: six 6px glyphs
        dows = [tab[f"panel.dow.{i}"] for i in range(7)]
        widest = max(dows, key=len)
        date = tab["panel.date"] % (widest, 30)
        ok(len(date) <= 6, f"{code}: footer date {date!r} wider than 6 glyphs")
        # AM/PM sits beside the minutes in an 11px slot: two glyphs
        for k in ("panel.am", "panel.pm"):
            ok(len(tab[k]) <= 2, f"{code}:{k} wider than its 2-glyph slot")
        # the update screen prints at x=2 in size 1: ten glyphs
        ok(len(tab["panel.updating"]) <= 10, f"{code}: panel.updating too wide")


@test("lang")
def test_lang_data_h_is_regenerated():
    hdr = (FW_DIR / "lang_data.h").read_text(encoding="utf-8")
    blobs = dict(re.findall(r'LANG_JSON_(\w+)\[\] PROGMEM = R"LJSON\((.*?)\)LJSON";', hdr, re.S))
    for code, tab in _tables().items():
        ok(code.upper() in blobs, f"lang_data.h embeds {code} (run firmware/tools/make_lang.ps1)")
        eq(json.loads(blobs[code.upper()]), tab,
           f"lang_data.h {code} matches lang/{code}.json (run firmware/tools/make_lang.ps1)")


@test("lang")
def test_every_named_key_exists_and_every_key_is_used():
    en = _tables()["en"]
    src = _sources()
    named = set(re.findall(r"""["'`=]((?:panel|tg|web|lang)\.[a-z0-9_]+(?:\.[a-z0-9_]+)*)(?=["'`\s>])""", src))
    unknown = sorted(k for k in named if k not in en)
    ok(not unknown, f"keys used in firmware but missing from en.json: {unknown}")
    unused = sorted(k for k in en
                    if k not in named and not any(p.match(k) for p in DYNAMIC))
    ok(not unused, f"keys in en.json that nothing uses: {unused}")
