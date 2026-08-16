#!/usr/bin/env python3
"""
panelshot — capture what the DigiFrame LED panel is actually showing.

This is a development tool, not part of the firmware. It pulls the raw
RGB565 framebuffer from the running device's GET /api/frame endpoint (see
firmware/DigiFrame/capture.h) and turns it into PNGs, animated GIFs and
contact sheets you can actually look at.

These are true screenshots, not a simulation: the pixels come from the
same draw calls the panel receives, so colour, layout and timing are the
device's, not an approximation of it.

Examples
--------
  # one frame of whatever is on screen right now
  python dev/panelshot.py shot

  # 3 seconds of the ambient scene as an animated GIF + contact sheet
  python dev/panelshot.py film --seconds 3

  # drive the panel somewhere first, then capture
  python dev/panelshot.py shot  --msg "HELLO WORLD"
  python dev/panelshot.py film  --celebrate birthday --seconds 5
  python dev/panelshot.py sheet --test --steps 17 --every 4   # every screen
  python dev/panelshot.py film  --sport 0 --event WICKET --seconds 4

  # the scene through the day, and in every weather
  python dev/panelshot.py sheet --hour 22 --wcode 0 --steps 8

  # back to normal
  python dev/panelshot.py stop

Notes
-----
* Capture arms itself on first request and auto-disarms 30 s after the
  last one, so leaving the device alone costs it nothing.
* The first request after arming returns 503 until a frame is presented;
  the tool retries automatically. A static screen (the setup QR) is
  repainted once on arming so there is always something to capture.
* Frame rate is limited by HTTP round-trips over WiFi, not by the panel.
  --fps is a request, not a guarantee; actual timing is reported.
"""

from __future__ import annotations

import argparse
import io
import re
import sys
import time
from pathlib import Path

try:
    import numpy as np
    import requests
    from PIL import Image, ImageDraw
except ImportError as exc:  # pragma: no cover - dependency hint
    sys.exit(f"missing dependency: {exc}. Try: pip install requests pillow numpy")


PANEL_W = PANEL_H = 64
FRAME_BYTES = PANEL_W * PANEL_H * 2
DEFAULT_HOST = "digiframe.local"
# Windows consoles default to cp1252 and choke on the em-dashes below.
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

OUT_DIR = Path(__file__).resolve().parent / "shots"

# mode ids from the Mode enum in globals.h, for the X-Mode response header
MODE_NAMES = ["CLOCK", "MSG", "GIF", "CELEBRATE", "TEST", "SETUP"]


def int_list(text: str) -> list[int]:
    return [int(x) for x in text.replace(" ", "").split(",") if x != ""]


# --------------------------------------------------------------------------
# sport metadata, parsed straight from the firmware headers
# --------------------------------------------------------------------------
FW_DIR = Path(__file__).resolve().parent.parent / "firmware" / "DigiFrame"


def sport_registry() -> list[tuple[str, str]]:
    """[(key, header_stem)] in the order sports_registry.h binds them.

    Parsed from the source rather than hard-coded so it cannot drift from
    SPORTS_[] -- that table is the single place a sport is registered.
    """
    reg = (FW_DIR / "sports_registry.h").read_text(encoding="utf-8", errors="replace")
    order = re.findall(r"&SPORT_([A-Z_]+),", reg)
    out = []
    for name in order:
        stem = f"sport_{name.lower()}"
        src = (FW_DIR / f"{stem}.h").read_text(encoding="utf-8", errors="replace")
        m = re.search(r'SportModule SPORT_' + name + r'\s*=\s*\{\s*"([^"]+)"',
                      src, re.S)
        out.append((m.group(1) if m else name.lower(), stem))
    return out


def sport_events(stem: str) -> list[tuple[str, str, str]]:
    """[(native, punch, label)] for one sport, de-duplicated by label.

    Several natives are aliases onto the same effect ("score" alongside
    "goal"/"td"/"try"); firing both would just repeat the animation.
    """
    src = (FW_DIR / f"{stem}.h").read_text(encoding="utf-8", errors="replace")
    rows = re.findall(
        r'\{\s*"([a-z0-9]+)"\s*,\s*(EV_[A-Z_]+)\s*,\s*"([^"]*)"\s*,\s*"([^"]*)"\s*\}',
        src)
    seen, out = set(), []
    for native, kind, punch, label in rows:
        if label in seen:
            continue
        seen.add(label)
        out.append((native, punch, label))
    return out


# --------------------------------------------------------------------------
# device I/O
# --------------------------------------------------------------------------
class Panel:
    def __init__(self, host: str, timeout: float = 5.0):
        self.base = f"http://{host}"
        self.timeout = timeout
        self.session = requests.Session()
        self.last_mode = None

    def _post(self, path: str, **params) -> None:
        r = self.session.post(f"{self.base}{path}", params=params, timeout=self.timeout)
        r.raise_for_status()

    def grab(self, retries: int = 25) -> np.ndarray:
        """Fetch one frame as an (H, W, 3) uint8 RGB array."""
        for attempt in range(retries):
            r = self.session.get(f"{self.base}/api/frame", timeout=self.timeout)
            if r.status_code == 503:          # armed, but no frame presented yet
                time.sleep(0.1)
                continue
            r.raise_for_status()
            if len(r.content) != FRAME_BYTES:
                raise RuntimeError(
                    f"expected {FRAME_BYTES} bytes, got {len(r.content)}"
                )
            self.last_mode = r.headers.get("X-Mode")
            return rgb565_to_rgb(r.content)
        raise RuntimeError(
            "device never presented a frame. Is it stuck on a static screen, "
            "or is the panel mid-reboot?"
        )

    def mode_name(self) -> str:
        try:
            return MODE_NAMES[int(self.last_mode)]
        except (TypeError, ValueError, IndexError):
            return "?"

    def disarm(self) -> None:
        self.session.get(f"{self.base}/api/frame", params={"off": 1}, timeout=self.timeout)

    # ---- driving the panel into the state you want to photograph ----
    # Arg names are the firmware's (web_portal.h): terse single letters on
    # the user-facing API, spelled out on the dev-only /api/dev.
    def send_msg(self, text: str) -> None:
        self._post("/api/msg", t=text)

    def play_gif(self, name: str) -> None:
        self._post("/api/play", g=name)

    def brightness(self, value: int) -> None:
        self._post("/api/brightness", v=value)

    def stop(self) -> None:
        self._post("/api/stop")

    def dev(self, **kwargs) -> None:
        """POST /api/dev — hour/wcode/sport/event/celebrate/msg/test/clear."""
        self._post("/api/dev", **{k: v for k, v in kwargs.items() if v is not None})


def rgb565_to_rgb(raw: bytes) -> np.ndarray:
    """Expand little-endian RGB565 to 8-bit RGB.

    The low bits are replicated into the empty low bits of each channel
    (the standard expansion) so that full-scale 565 maps to full-scale
    888 -- a plain left-shift would cap white at (248, 252, 248) and make
    every screenshot look slightly dim compared to the panel.
    """
    px = np.frombuffer(raw, dtype="<u2").reshape(PANEL_H, PANEL_W)
    r = ((px >> 11) & 0x1F).astype(np.uint8)
    g = ((px >> 5) & 0x3F).astype(np.uint8)
    b = (px & 0x1F).astype(np.uint8)
    return np.dstack([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)])


# --------------------------------------------------------------------------
# rendering the captures into something viewable
# --------------------------------------------------------------------------
def upscale(frame: np.ndarray, scale: int, grid: bool = False) -> Image.Image:
    """Nearest-neighbour blow-up. 64x64 is unreadable at 1:1 on a screen."""
    img = Image.fromarray(frame, "RGB").resize(
        (PANEL_W * scale, PANEL_H * scale), Image.NEAREST
    )
    if grid and scale >= 6:
        d = ImageDraw.Draw(img)
        for i in range(1, PANEL_W):
            d.line([(i * scale, 0), (i * scale, img.height)], fill=(24, 24, 24))
            d.line([(0, i * scale), (img.width, i * scale)], fill=(24, 24, 24))
    return img


def contact_sheet(frames: list[np.ndarray], scale: int, cols: int = 6) -> Image.Image:
    """All frames on one page -- the fastest way to judge motion in a still."""
    pad, label_h = 4, 12
    cell_w, cell_h = PANEL_W * scale, PANEL_H * scale
    rows = (len(frames) + cols - 1) // cols
    sheet = Image.new(
        "RGB",
        (cols * (cell_w + pad) + pad, rows * (cell_h + label_h + pad) + pad),
        (18, 18, 20),
    )
    d = ImageDraw.Draw(sheet)
    for i, f in enumerate(frames):
        x = pad + (i % cols) * (cell_w + pad)
        y = pad + (i // cols) * (cell_h + label_h + pad)
        sheet.paste(upscale(f, scale), (x, y))
        d.text((x + 2, y + cell_h + 1), f"{i}", fill=(150, 150, 160))
    return sheet


def labelled_grid(shots: list[tuple[str, np.ndarray]], scale: int,
                  cols: int = 4) -> Image.Image:
    """A labelled contact sheet: the fastest way to compare variants.

    Differences between weather codes or hours are often only a handful of
    pixels; side by side they are obvious, one at a time they are not.
    """
    pad, label_h = 6, 14
    cell_w, cell_h = PANEL_W * scale, PANEL_H * scale
    cols = min(cols, len(shots))
    rows = (len(shots) + cols - 1) // cols
    img = Image.new(
        "RGB",
        (cols * (cell_w + pad) + pad, rows * (cell_h + label_h + pad) + pad),
        (18, 18, 20),
    )
    d = ImageDraw.Draw(img)
    for i, (label, frame) in enumerate(shots):
        x = pad + (i % cols) * (cell_w + pad)
        y = pad + (i // cols) * (cell_h + label_h + pad)
        img.paste(upscale(frame, scale), (x, y))
        d.text((x + 2, y + cell_h + 2), label, fill=(190, 190, 200))
    return img


def save(img: Image.Image, name: str) -> Path:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    path = OUT_DIR / name
    img.save(path)
    return path


# --------------------------------------------------------------------------
# putting the panel into a given state before capturing
# --------------------------------------------------------------------------
def drive(panel: Panel, args) -> None:
    """Put the panel where we want it, then let it settle before capturing."""
    acted = False
    if args.stop_first:
        panel.stop()
        acted = True
    if args.brightness is not None:
        panel.brightness(args.brightness)
        acted = True
    if args.msg:
        panel.send_msg(args.msg)
        acted = True
    if args.gif:
        panel.play_gif(args.gif)
        acted = True

    # everything else goes through the single dev endpoint
    dev = {}
    if args.hour is not None:
        dev["hour"] = args.hour
    if args.wcode is not None:
        dev["wcode"] = args.wcode
    if args.sport is not None:
        dev["sport"] = args.sport
    if args.event:
        dev["event"] = args.event
    if args.celebrate:
        dev["celebrate"] = args.celebrate
        dev["msg"] = args.celeb_msg or ""
    if args.test:
        dev["test"] = 1
    if args.clear:
        dev["clear"] = 1
    if dev:
        panel.dev(**dev)
        acted = True

    if acted:
        time.sleep(args.settle)


def add_drive_args(p: argparse.ArgumentParser) -> None:
    g = p.add_argument_group("drive the panel before capturing")
    g.add_argument("--msg", help="scroll a message (MODE_MSG)")
    g.add_argument("--gif", help="play a stored GIF by name (MODE_GIF)")
    g.add_argument("--celebrate", choices=["custom", "birthday"],
                   help="start a celebration (MODE_CELEBRATE)")
    g.add_argument("--celeb-msg", help="banner text for --celebrate")
    g.add_argument("--hour", type=int, metavar="0-23",
                   help="spoof the clock hour: the ambient scene changes "
                        "through the day (sun/moon/stars/sleeping cat)")
    g.add_argument("--wcode", type=int, metavar="CODE",
                   help="spoof the weather code: 0 clear, 2 cloudy, 45 fog, "
                        "61 rain, 73 snow, 95 storm")
    g.add_argument("--sport", type=int, metavar="IDX",
                   help="force the live-score card to sport index IDX")
    g.add_argument("--event", help="fire a native score event, e.g. WICKET")
    g.add_argument("--test", action="store_true",
                   help="run the firmware /test walk")
    g.add_argument("--clear", action="store_true",
                   help="lift any hour/weather overrides")
    g.add_argument("--brightness", type=int, help="set brightness 1-255")
    g.add_argument("--stop-first", action="store_true",
                   help="return to the clock before doing anything else")
    g.add_argument("--settle", type=float, default=1.0,
                   help="seconds to wait after driving, before capturing")


def add_output_args(p: argparse.ArgumentParser) -> None:
    p.add_argument("--host", default=DEFAULT_HOST)
    p.add_argument("--scale", type=int, default=8, help="upscale factor")
    p.add_argument("--grid", action="store_true", help="draw pixel gridlines")
    p.add_argument("--name", help="output filename stem")


# --------------------------------------------------------------------------
def cmd_shot(panel: Panel, args) -> None:
    drive(panel, args)
    frame = panel.grab()
    stem = args.name or f"shot-{time.strftime('%H%M%S')}"
    path = save(upscale(frame, args.scale, args.grid), f"{stem}.png")
    print(f"mode={panel.mode_name()}  ->  {path}")


def cmd_film(panel: Panel, args) -> None:
    drive(panel, args)
    interval = 1.0 / args.fps
    frames, deadline, t0 = [], time.monotonic(), time.monotonic()
    while time.monotonic() - t0 < args.seconds:
        frames.append(panel.grab())
        deadline += interval
        sleep = deadline - time.monotonic()
        if sleep > 0:
            time.sleep(sleep)
        else:                       # HTTP is slower than the requested rate
            deadline = time.monotonic()
    elapsed = time.monotonic() - t0

    stem = args.name or f"film-{time.strftime('%H%M%S')}"
    gif = [upscale(f, args.scale) for f in frames]
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    gif_path = OUT_DIR / f"{stem}.gif"
    gif[0].save(gif_path, save_all=True, append_images=gif[1:],
                duration=int(elapsed / max(len(frames), 1) * 1000), loop=0)
    sheet_path = save(contact_sheet(frames, max(args.scale // 2, 2)), f"{stem}-sheet.png")

    print(f"mode={panel.mode_name()}  {len(frames)} frames in {elapsed:.1f}s "
          f"({len(frames)/elapsed:.1f} fps captured)")
    print(f"  {gif_path}")
    print(f"  {sheet_path}")


def cmd_sheet(panel: Panel, args) -> None:
    """N frames on one page. With --test, samples one frame per /test step."""
    drive(panel, args)
    frames = []
    for _ in range(args.steps):
        frames.append(panel.grab())
        time.sleep(args.every)
    stem = args.name or f"sheet-{time.strftime('%H%M%S')}"
    path = save(contact_sheet(frames, args.scale), f"{stem}.png")
    print(f"mode={panel.mode_name()}  {len(frames)} frames  ->  {path}")


def cmd_sweep(panel: Panel, args) -> None:
    """Vary one thing, hold everything else, and lay the results out together.

    Averaging is deliberately not done here: each cell is a single frame, so
    what you see is a frame the panel really drew.
    """
    if args.hours:
        values, key, fmt = args.hours, "hour", "{:02d}:00"
    elif args.wcodes:
        values, key, fmt = args.wcodes, "wcode", "wcode {}"
    elif args.sports:
        values, key, fmt = args.sports, "sport", "sport {}"
    else:
        sys.exit("pick one of --hours / --wcodes / --sports")

    shots = []
    for v in values:
        params = {key: v}
        # hold the other axis steady so only the swept variable moves
        if key == "wcode" and args.hold_hour is not None:
            params["hour"] = args.hold_hour
        if key == "hour" and args.hold_wcode is not None:
            params["wcode"] = args.hold_wcode
        panel.dev(**params)
        time.sleep(args.settle)
        shots.append((fmt.format(v), panel.grab()))
        print(f"  captured {fmt.format(v)}")

    stem = args.name or f"sweep-{key}-{time.strftime('%H%M%S')}"
    path = save(labelled_grid(shots, args.scale, args.cols), f"{stem}.png")
    print(f"{len(shots)} variants  ->  {path}")


def cmd_fx(panel: Panel, args) -> None:
    """Fire every event of a sport and lay the animations out as filmstrips.

    One row per event, `--frames` samples across the animation. Animations
    run 12-45 frames at 15 fps (EV_FRAMES in sports_core.h), i.e. 0.8-3.0 s,
    so capturing flat out for a couple of seconds spans the whole thing.
    """
    reg = sport_registry()
    idx = args.sport
    key, stem = reg[idx]
    events = sport_events(stem)
    print(f"sport {idx} '{key}' — {len(events)} distinct events")

    panel.dev(sport=idx)
    time.sleep(1.5)
    base = panel.grab()                       # the resting card, for reference

    rows: list[tuple[str, list[np.ndarray]]] = []
    for native, punch, label in events:
        panel.dev(event=native)
        shots = []
        t0 = time.monotonic()
        while time.monotonic() - t0 < args.window and len(shots) < args.frames * 8:
            shots.append(panel.grab())
        # even samples across what we caught
        if len(shots) > args.frames:
            step = len(shots) / args.frames
            shots = [shots[int(i * step)] for i in range(args.frames)]
        rows.append((f"{label} ({native})", shots))
        print(f"  {label:<11} {native:<9} {len(shots)} frames")
        time.sleep(args.gap)                  # let the animation finish

    img = filmstrip(key, base, rows, args.scale)
    stem_out = args.name or f"fx-{key}"
    print(f"-> {save(img, f'{stem_out}.png')}")


def filmstrip(title: str, base: np.ndarray,
              rows: list[tuple[str, list[np.ndarray]]], scale: int) -> Image.Image:
    """Left column = the resting card; each row = one event over time."""
    cell = PANEL_W * scale
    lab_w, pad, hdr = 96, 3, 18
    ncols = max(len(f) for _, f in rows)
    w = lab_w + (ncols + 1) * (cell + pad) + pad
    h = hdr + len(rows) * (cell + pad) + pad
    img = Image.new("RGB", (w, h), (16, 16, 18))
    d = ImageDraw.Draw(img)
    d.text((pad, 4), f"{title} — resting card, then each event over time",
           fill=(200, 200, 210))
    for r, (label, frames) in enumerate(rows):
        y = hdr + r * (cell + pad)
        d.text((pad, y + cell // 2 - 4), label[:16], fill=(185, 185, 195))
        img.paste(upscale(base, scale), (lab_w, y))
        for c, f in enumerate(frames):
            img.paste(upscale(f, scale), (lab_w + (c + 1) * (cell + pad), y))
    return img


def cmd_stop(panel: Panel, args) -> None:
    panel.stop()
    panel.disarm()
    print("panel back to clock, capture disarmed")


def main() -> None:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("shot", help="capture a single frame as a PNG")
    add_output_args(p); add_drive_args(p)
    p.set_defaults(func=cmd_shot)

    p = sub.add_parser("film", help="capture a sequence as an animated GIF")
    add_output_args(p); add_drive_args(p)
    p.add_argument("--seconds", type=float, default=3.0)
    p.add_argument("--fps", type=float, default=10.0)
    p.set_defaults(func=cmd_film)

    p = sub.add_parser("sheet", help="capture N frames onto one contact sheet")
    add_output_args(p); add_drive_args(p)
    p.add_argument("--steps", type=int, default=12)
    p.add_argument("--every", type=float, default=1.0,
                   help="seconds between frames (use 4.0 with --test, one "
                        "per TEST_STEP_MS step)")
    p.set_defaults(func=cmd_sheet)

    p = sub.add_parser("sweep", help="vary one parameter, compare side by side")
    add_output_args(p)
    p.add_argument("--hours", type=int_list, help="e.g. 0,6,9,12,15,18,21")
    p.add_argument("--wcodes", type=int_list, help="e.g. 0,2,45,61,73,95")
    p.add_argument("--sports", type=int_list, help="e.g. 0,1,2,3,4,5")
    p.add_argument("--hold-hour", type=int, default=13,
                   help="hour to hold while sweeping weather (default 13)")
    p.add_argument("--hold-wcode", type=int,
                   help="weather code to hold while sweeping hours")
    p.add_argument("--cols", type=int, default=4)
    p.add_argument("--settle", type=float, default=1.5)
    p.set_defaults(func=cmd_sweep)

    p = sub.add_parser("fx", help="every event animation of one sport, as filmstrips")
    add_output_args(p)
    p.add_argument("--sport", type=int, required=True)
    p.add_argument("--frames", type=int, default=6, help="samples per animation")
    p.add_argument("--window", type=float, default=3.2,
                   help="seconds to capture after firing; the longest animation "
                        "is EV_SCORE_BIG/EV_MILESTONE at 45 frames @15fps = 3.0 s")
    p.add_argument("--gap", type=float, default=3.2,
                   help="seconds to idle between events. Must outlast the "
                        "animation: the event ring only pops the next event once "
                        "the current one ends (score_widget.h), so too small a "
                        "gap silently shifts every row one event late.")
    p.set_defaults(func=cmd_fx)

    p = sub.add_parser("stop", help="clock mode + disarm capture")
    p.add_argument("--host", default=DEFAULT_HOST)
    p.set_defaults(func=cmd_stop)

    args = ap.parse_args()
    panel = Panel(args.host)
    try:
        args.func(panel, args)
    except requests.exceptions.RequestException as exc:
        sys.exit(f"cannot reach {args.host}: {exc}")


if __name__ == "__main__":
    main()
