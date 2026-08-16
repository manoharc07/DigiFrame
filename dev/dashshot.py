#!/usr/bin/env python3
"""
dashshot — screenshot and audit the DigiFrame web dashboard.

The companion to panelshot.py: that one photographs the LED panel, this
one photographs the browser UI served from web_portal.h. It loads the
dashboard from the live device in a real headless Chromium, so what you
get is the actual HTML/CSS/JS the firmware serves, rendered by a real
engine -- not a guess about how the string in DASH_HTML will look.

Beyond screenshots it reports the things that are invisible in a static
read of the source but obvious to a browser: JavaScript errors, failed
requests, and horizontal overflow (the classic phone-layout bug).

Examples
--------
  # every breakpoint, plus a health report
  python dev/dashshot.py

  # just the phone width, full page
  python dev/dashshot.py --widths 390

  # audit only, no images
  python dev/dashshot.py --no-shots

  # a local HTML file instead of the device (offline iteration)
  python dev/dashshot.py --url file:///C:/tmp/dash.html
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

try:
    from playwright.sync_api import sync_playwright
except ImportError:
    sys.exit(
        "playwright is not installed.\n"
        "  pip install playwright && python -m playwright install chromium"
    )

# Windows consoles default to cp1252 and choke on the em-dashes below.
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

OUT_DIR = Path(__file__).resolve().parent / "shots"
DEFAULT_HOST = "digiframe.local"

# name -> viewport width. Heights are generous; --full-page overrides anyway.
BREAKPOINTS = {"phone": 390, "tablet": 768, "desktop": 1280}


def audit(page) -> dict:
    """Facts about the rendered page that a source read cannot tell you."""
    return page.evaluate(
        """() => {
        const de = document.documentElement;
        // elements poking out past the viewport -- the usual cause of a
        // phone layout that scrolls sideways
        const overflowing = [];
        for (const el of document.querySelectorAll('*')) {
            const r = el.getBoundingClientRect();
            if (r.width > 0 && r.right > de.clientWidth + 1) {
                overflowing.push({
                    tag: el.tagName.toLowerCase(),
                    id: el.id || null,
                    cls: (el.className && el.className.toString().slice(0, 40)) || null,
                    right: Math.round(r.right),
                });
            }
        }
        // controls with no accessible name
        const unlabelled = [];
        for (const el of document.querySelectorAll('input,select,textarea,button')) {
            const name = el.getAttribute('aria-label') || el.title ||
                         el.placeholder || (el.labels && el.labels.length) ||
                         (el.tagName === 'BUTTON' && el.textContent.trim());
            if (!name) unlabelled.push(el.tagName.toLowerCase() +
                                      (el.id ? '#' + el.id : '') +
                                      (el.type ? '[' + el.type + ']' : ''));
        }
        // tap targets smaller than the 24px minimum
        const tiny = [];
        for (const el of document.querySelectorAll('button,a,input[type=button],input[type=submit]')) {
            const r = el.getBoundingClientRect();
            if (r.width > 0 && (r.height < 24 || r.width < 24))
                tiny.push((el.id || el.textContent.trim().slice(0, 20) || el.tagName) +
                          ` ${Math.round(r.width)}x${Math.round(r.height)}`);
        }
        return {
            scrollW: de.scrollWidth,
            clientW: de.clientWidth,
            docH: de.scrollHeight,
            overflowing: overflowing.slice(0, 12),
            unlabelled: unlabelled.slice(0, 12),
            tiny: tiny.slice(0, 12),
            title: document.title,
            hasViewportMeta: !!document.querySelector('meta[name=viewport]'),
        };
        }"""
    )


def run(args) -> int:
    url = args.url or f"http://{args.host}/"
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    if args.widths:
        targets = {f"w{w}": w for w in args.widths}
    else:
        targets = BREAKPOINTS

    problems = 0
    with sync_playwright() as pw:
        browser = pw.chromium.launch()
        for name, width in targets.items():
            ctx = browser.new_context(
                viewport={"width": width, "height": args.height},
                device_scale_factor=2 if args.retina else 1,
            )
            page = ctx.new_page()

            console: list[str] = []
            failed: list[str] = []
            page.on("console", lambda m: console.append(f"{m.type}: {m.text}")
                    if m.type in ("error", "warning") else None)
            page.on("pageerror", lambda e: console.append(f"pageerror: {e}"))
            page.on("requestfailed",
                    lambda r: failed.append(f"{r.method} {r.url} — {r.failure}"))

            try:
                page.goto(url, wait_until="load", timeout=args.timeout)
            except Exception as exc:
                print(f"[{name}] could not load {url}: {exc}")
                ctx.close()
                problems += 1
                continue

            # the dashboard fetches /api/* after load; give it a moment
            page.wait_for_timeout(args.settle)

            info = audit(page)
            print(f"\n=== {name} ({width}px) — {info['title'] or '(no <title>)'}")
            print(f"    document {info['clientW']}x{info['docH']}"
                  f"  scrollWidth {info['scrollW']}")

            if not info["hasViewportMeta"]:
                print("    [!] no <meta name=viewport> — mobile will render at "
                      "desktop width and zoom out")
                problems += 1
            if info["scrollW"] > info["clientW"] + 1:
                print(f"    [!] horizontal overflow: content is "
                      f"{info['scrollW'] - info['clientW']}px wider than the viewport")
                problems += 1
                for o in info["overflowing"]:
                    print(f"        {o['tag']}"
                          f"{'#' + o['id'] if o['id'] else ''}"
                          f"{'.' + o['cls'] if o['cls'] else ''} right={o['right']}")
            if info["tiny"]:
                print(f"    [!] {len(info['tiny'])} tap target(s) under 24px:")
                for t in info["tiny"]:
                    print(f"        {t}")
                problems += 1
            if info["unlabelled"]:
                print(f"    [!] {len(info['unlabelled'])} control(s) with no "
                      f"accessible name:")
                for u in info["unlabelled"]:
                    print(f"        {u}")
                problems += 1
            for c in console:
                print(f"    [js] {c}")
                problems += 1
            for f in failed:
                print(f"    [net] {f}")
                problems += 1
            if not any([console, failed, info["tiny"], info["unlabelled"]]) \
                    and info["scrollW"] <= info["clientW"] + 1 \
                    and info["hasViewportMeta"]:
                print("    clean")

            if not args.no_shots:
                stem = args.name or "dash"
                path = OUT_DIR / f"{stem}-{name}.png"
                page.screenshot(path=str(path), full_page=not args.viewport_only)
                print(f"    -> {path}")

            ctx.close()
        browser.close()

    print(f"\n{problems} issue(s) found" if problems else "\nno issues found")
    return 0


def main() -> None:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("--host", default=DEFAULT_HOST)
    ap.add_argument("--url", help="load this URL instead of the device")
    ap.add_argument("--widths", type=lambda s: [int(x) for x in s.split(",")],
                    help="viewport widths, e.g. 390,768,1280")
    ap.add_argument("--height", type=int, default=900)
    ap.add_argument("--retina", action="store_true", help="2x device pixel ratio")
    ap.add_argument("--viewport-only", action="store_true",
                    help="screenshot the fold only, not the whole page")
    ap.add_argument("--no-shots", action="store_true", help="audit only")
    ap.add_argument("--settle", type=int, default=1500,
                    help="ms to wait after load for /api/* fetches")
    ap.add_argument("--timeout", type=int, default=20000)
    ap.add_argument("--name", help="output filename stem")
    sys.exit(run(ap.parse_args()))


if __name__ == "__main__":
    main()
