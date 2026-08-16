#!/usr/bin/env python3
"""
Tiny zero-dependency test harness.

This project has no CI and no test framework, and adding pytest just to run
a few hundred assertions would be a dependency the rest of the repo does not
carry. This is the whole runner: a decorator, a handful of assert helpers,
and a summary. Test modules import it and register with @test.

Two kinds of test live here, and the distinction matters:

  * CONTRACT tests hit ESPN and assert the assumptions the firmware is built
    on (plain HTTP works, the User-Agent rules, chunked vs Content-Length,
    the JSON shapes). They need the internet but no device. They exist
    because ESPN is undocumented and can change under us — when live scores
    break, run these first to find out whether it is our bug or theirs.

  * DEVICE tests drive a running frame over the LAN. They need the device
    powered on and reachable, and they skip cleanly when it is not.

Nothing here mocks anything. A green run means the real API and the real
firmware behaved, which is the only claim worth making about a system whose
failures have all been at the boundaries.
"""

from __future__ import annotations

import argparse
import sys
import time
import traceback

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

_TESTS: list[tuple[str, str, callable]] = []


class Skip(Exception):
    """Raised by a test that cannot run here (no device, nothing live)."""


def test(group: str):
    """Register a test. `group` selects it with --only."""
    def deco(fn):
        _TESTS.append((group, fn.__name__, fn))
        return fn
    return deco


# ---- assertions ---------------------------------------------------------
def eq(actual, expected, what: str):
    if actual != expected:
        raise AssertionError(f"{what}: expected {expected!r}, got {actual!r}")


def ok(cond, what: str):
    if not cond:
        raise AssertionError(what)


def within(actual, lo, hi, what: str):
    if not (lo <= actual <= hi):
        raise AssertionError(f"{what}: {actual} not in [{lo}, {hi}]")


def contains(haystack, needle, what: str):
    if needle not in haystack:
        raise AssertionError(f"{what}: {needle!r} missing from {str(haystack)[:120]}")


# ---- runner -------------------------------------------------------------
def main(default_host: str = "digiframe.local") -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default=default_host, help="frame hostname or IP")
    ap.add_argument("--only", help="run only this group (e.g. espn, device, panel)")
    ap.add_argument("--list", action="store_true", help="list tests and exit")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    import config
    config.HOST = args.host
    config.VERBOSE = args.verbose

    if args.list:
        for g, n, _ in _TESTS:
            print(f"  {g:8} {n}")
        return 0

    chosen = [t for t in _TESTS if not args.only or t[0] == args.only]
    if not chosen:
        print(f"no tests matched --only {args.only!r}")
        return 2

    passed = failed = skipped = 0
    failures: list[tuple[str, str]] = []
    group_now = None
    t0 = time.monotonic()

    for group, name, fn in chosen:
        if group != group_now:
            group_now = group
            print(f"\n{group}")
        label = name.replace("test_", "").replace("_", " ")
        try:
            fn()
        except Skip as exc:
            print(f"  ~  {label:44} skipped: {exc}")
            skipped += 1
        except Exception as exc:                       # noqa: BLE001
            print(f"  X  {label:44} {exc}")
            failures.append((name, traceback.format_exc()))
            failed += 1
        else:
            print(f"  ok {label}")
            passed += 1

    print(f"\n{passed} passed, {failed} failed, {skipped} skipped "
          f"in {time.monotonic() - t0:.1f}s")
    if failures and args.verbose:
        for name, tb in failures:
            print(f"\n--- {name} ---\n{tb}")
    elif failures:
        print("re-run with -v for tracebacks")
    return 1 if failed else 0
