"""
Runtime panel wiring tables in panel_cfg.h — host only, no device needed.

    python tests/run.py --only panel_cfg

The dashboard can now re-pin the HUB75 panel, and panel_cfg.h decides which
GPIOs it may use. These pin the facts that decide whether a frame boots:

  * the config.h defaults — what every fresh install starts on — pass the
    same validation a dashboard edit must pass;
  * the allowed table never includes a pin this build needs for something
    else (flash, octal PSRAM, USB, strapping);
  * the driver names are in the library's shift_driver enum order, because
    the index is what /panel.json stores and what the panel is built with.
"""
from __future__ import annotations

import os
import re
from pathlib import Path

from harness import test, eq, ok, Skip

FW = Path(__file__).resolve().parent.parent / "firmware" / "DigiFrame"
NAMES = ["R1", "G1", "B1", "R2", "G2", "B2", "A", "B", "C", "D", "E", "LAT", "OE", "CLK"]


def _src(name: str) -> str:
    return (FW / name).read_text(encoding="utf-8")


def _allowed() -> set[int]:
    body = _src("panel_cfg.h").split("PANEL_GPIO_OK[] = {", 1)[1].split("};", 1)[0]
    return {int(n) for n in re.findall(r"\d+", body)}


def _defaults() -> dict[str, int]:
    cfg = _src("config.h")
    return {n: int(re.search(rf"^#define\s+{n}_PIN\s+(\d+)", cfg, re.M).group(1)) for n in NAMES}


@test("panel_cfg")
def test_pin_names_match_the_library_order():
    body = _src("panel_cfg.h").split("PANEL_PIN_NAMES[PANEL_PIN_COUNT] = {", 1)[1].split("};", 1)[0]
    eq(re.findall(r'"([^"]+)"', body), NAMES, "i2s_pins order: R1 G1 B1 R2 G2 B2 A B C D E LAT OE CLK")


@test("panel_cfg")
def test_defaults_pass_the_dashboard_validation():
    d = _defaults()
    allowed = _allowed()
    for n, p in d.items():
        ok(p in allowed, f"default {n}=GPIO {p} is in PANEL_GPIO_OK")
    eq(len(set(d.values())), len(d), "default pins are all distinct")


@test("panel_cfg")
def test_allowed_pins_avoid_what_this_build_uses():
    reserved = {0: "strapping", 45: "strapping", 46: "strapping", 19: "USB", 20: "USB",
                43: "UART0", 44: "UART0"}
    reserved.update({p: "nonexistent" for p in range(22, 26)})
    reserved.update({p: "flash" for p in range(26, 33)})
    reserved.update({p: "octal PSRAM" for p in range(33, 38)})
    allowed = _allowed()
    for p, why in reserved.items():
        ok(p not in allowed, f"GPIO {p} ({why}) is refused")
    ok(all(1 <= p <= 48 for p in allowed), "every allowed pin exists on the ESP32-S3")


@test("panel_cfg")
def test_driver_names_follow_the_library_enum():
    body = _src("panel_cfg.h").split("PANEL_DRIVERS[] = {", 1)[1].split("};", 1)[0]
    ours = re.findall(r'"([^"]+)"', body)
    lib = Path(os.path.expanduser("~/Documents/Arduino/libraries/"
                                  "ESP32_HUB75_LED_MATRIX_PANEL_DMA_Display/src/"
                                  "ESP32-HUB75-MatrixPanel-I2S-DMA.h"))
    if not lib.exists():
        raise Skip("HUB75 library not installed here")
    enum = lib.read_text(encoding="utf-8").split("enum shift_driver", 1)[1].split("}", 1)[0]
    theirs = re.findall(r"([A-Z][A-Z0-9]+)", enum)
    eq(ours, theirs, "PANEL_DRIVERS is the shift_driver enum, same order")
