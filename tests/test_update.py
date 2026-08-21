"""
Update contract tests — the assumptions the one-click update is built on.

The update feature is split across two machines on purpose, and the split
rests entirely on GitHub's CORS policy:

  * the CHECK runs in the BROWSER, because api.github.com sends
    `Access-Control-Allow-Origin: *`.
  * the DOWNLOAD runs on the FRAME, because the release asset does not.

Neither half is documented as a promise, and if either flips the failure is
silent and confusing — a check that reports a CORS error with no cause
visible, or a device that spends a TLS session it did not need to. Every test
here corresponds to one decision in config.h / updater.h / the dashboard, so a
failure tells you which decision stopped being valid.

Needs the internet. Does not need the device (the last two skip without one).
"""
from __future__ import annotations

import json
import re
import urllib.request
from pathlib import Path

import config
from config import http, dev_json, device_up
from harness import test, eq, ok, contains, Skip

FW_DIR = Path(__file__).resolve().parent.parent / "firmware" / "DigiFrame"
GH_API = "https://api.github.com/repos/"

# A browser-shaped Origin, since that is the case under test.
ORIGIN = "http://digiframe.local"


def _cfg_define(name: str) -> str:
    """Read a string #define out of config.h — the firmware is the spec."""
    cfg = (FW_DIR / "config.h").read_text(encoding="utf-8")
    m = re.search(rf'^#define\s+{name}\s+"([^"]*)"', cfg, re.M)
    if not m:
        raise AssertionError(f"{name} not found in config.h")
    return m.group(1)


def _latest_release() -> dict:
    url = f"{GH_API}{_cfg_define('UPDATE_REPO')}/releases/latest"
    status, body = http(url, timeout=25)
    if status == 403:                       # unauthenticated API rate limit
        raise Skip("github API rate-limited this IP")
    eq(status, 200, f"{url}")
    return json.loads(body)


def _app_asset(rel: dict) -> dict:
    suffix = _cfg_define("UPDATE_ASSET_SUFFIX")
    for a in rel.get("assets", []):
        if a["name"].endswith(suffix):
            return a
    raise AssertionError(f"{rel.get('tag_name')} has no {suffix} asset")


@test("update")
def test_release_api_is_readable_from_a_browser():
    """The whole reason the check does not live on the ESP32. If this ever
    fails, the check has to move onto the device — a TLS session, a filtered
    JSON parse and a periodic timer that the browser currently does for free."""
    url = f"{GH_API}{_cfg_define('UPDATE_REPO')}/releases/latest"
    req = urllib.request.Request(url)
    req.add_header("Origin", ORIGIN)
    req.add_header("User-Agent", config.UA)
    with urllib.request.urlopen(req, timeout=25) as r:
        hdrs = {k.lower(): v for k, v in r.headers.items()}
    eq(hdrs.get("access-control-allow-origin"), "*",
       "api.github.com must allow any origin")
    ok("content-length" in hdrs, "expected Content-Length, not a chunked body")
    ok(int(hdrs["content-length"]) < 64 * 1024,
       f"/releases/latest grew to {hdrs['content-length']} bytes")


@test("update")
def test_release_carries_an_app_image_asset():
    """The dashboard picks the asset by this suffix, so the release workflow
    and config.h have to agree on the name. They are edited in different
    files, which is exactly how they drift."""
    rel = _latest_release()
    a = _app_asset(rel)
    ok(a["size"] > 512 * 1024, f"{a['name']} is only {a['size']} bytes")
    ok(a["size"] < 4 * 1024 * 1024,
       f"{a['name']} ({a['size']}) exceeds the 4 MB OTA slot in partitions.csv")


@test("update")
def test_release_asset_is_NOT_readable_from_a_browser():
    """The load-bearing negative. github.com 302s the asset to
    release-assets.githubusercontent.com, whose response carries no CORS
    header — so `fetch()` can never read the image and POST it to /api/ota,
    and updater.h has to exist. The day this starts sending
    Access-Control-Allow-Origin, the device can stop doing TLS entirely."""
    a = _app_asset(_latest_release())
    req = urllib.request.Request(a["browser_download_url"])
    req.add_header("Origin", ORIGIN)
    req.add_header("Range", "bytes=0-63")       # no need for 1.6 MB to see this
    with urllib.request.urlopen(req, timeout=25) as r:
        hdrs = {k.lower(): v for k, v in r.headers.items()}
    ok("access-control-allow-origin" not in hdrs,
       "the asset now allows cross-origin reads — the browser could do the "
       "download itself and updater.h's TLS session is no longer necessary")


@test("update")
def test_asset_url_matches_the_firmware_allowlist():
    """updUrlAllowed() in updater.h refuses any URL that is not a release
    asset of the configured repo, so the dashboard cannot point the frame at
    an arbitrary host. If GitHub ever changes the shape of
    browser_download_url, the frame would refuse its own update."""
    a = _app_asset(_latest_release())
    prefix = f"https://github.com/{_cfg_define('UPDATE_REPO')}/releases/download/"
    contains(a["browser_download_url"], prefix, "asset URL vs updUrlAllowed()")
    ok(a["browser_download_url"].startswith(prefix),
       f"{a['browser_download_url']} would be refused by updUrlAllowed()")


@test("update")
def test_asset_redirect_stays_https():
    """HTTPClient is set to STRICT_FOLLOW_REDIRECTS, which follows only
    same-protocol hops. It is also why this cannot be done in the clear like
    the ESPN provider: the redirect's SAS signature carries spr=https."""
    a = _app_asset(_latest_release())

    class NoRedirect(urllib.request.HTTPRedirectHandler):
        def redirect_request(self, *args, **kw):
            return None

    opener = urllib.request.build_opener(NoRedirect)
    req = urllib.request.Request(a["browser_download_url"])
    req.add_header("User-Agent", config.UA)
    try:
        with opener.open(req, timeout=25) as r:
            raise AssertionError(f"expected a redirect, got HTTP {r.status}")
    except urllib.error.HTTPError as e:
        eq(e.code, 302, "github.com should 302 the asset")
        ok(e.headers["Location"].startswith("https://"),
           "the redirect must stay https — plain HTTP would be free of TLS")


@test("update")
def test_asset_is_an_app_image_not_the_merged_one():
    """updater.h checks the esp_app_desc_t magic at offset 0x20 before it
    calls Update.begin(), so neither a hand-picked file nor a mis-named
    release asset can write a slot the board will not boot. The full-flash
    image starts with the bootloader and fails this — which is the point."""
    rel = _latest_release()
    head = {}
    for a in rel["assets"]:
        req = urllib.request.Request(a["browser_download_url"])
        req.add_header("Range", "bytes=0-63")
        req.add_header("User-Agent", config.UA)
        with urllib.request.urlopen(req, timeout=25) as r:
            head[a["name"]] = r.read(64)

    def magic(b: bytes) -> int:
        return int.from_bytes(b[0x20:0x24], "little")

    app = _app_asset(rel)["name"]
    eq(hex(magic(head[app])), hex(0xABCD5432), f"{app} must carry the app magic")
    for name, b in head.items():
        if name != app:
            ok(magic(b) != 0xABCD5432,
               f"{name} also looks like an app image — the suffix is the only "
               "thing keeping the dashboard off the full-flash binary")


@test("update")
def test_firmware_version_is_a_plain_semver():
    """The dashboard compares FW_VERSION field by field as integers. Anything
    the release workflow's stamp would not produce (a suffix, a 'v', a fourth
    field) silently compares as 0 and can hide a real update."""
    v = _cfg_define("FW_VERSION")
    ok(re.fullmatch(r"\d+\.\d+\.\d+", v), f"FW_VERSION {v!r} is not major.minor.patch")


@test("update")
def test_device_reports_what_the_dashboard_needs():
    """The page cannot run the check without these three: its own version to
    compare, the repo to ask about, and which asset is the app image."""
    if not device_up():
        raise Skip(f"no frame at {config.HOST}")
    j = dev_json("/api/config")
    for k in ("fw", "updRepo", "updAsset"):
        ok(j.get(k), f"/api/config is missing {k!r}")
    eq(j["updRepo"], _cfg_define("UPDATE_REPO"), "device repo vs config.h")
    eq(j["updAsset"], _cfg_define("UPDATE_ASSET_SUFFIX"), "device suffix vs config.h")
    ok(re.fullmatch(r"\d+\.\d+\.\d+", j["fw"]), f"device reports fw={j['fw']!r}")


@test("update")
def test_device_refuses_a_foreign_update_url():
    """/api/update is unauthenticated and LAN-only, like the rest of the
    dashboard, but a URL is a standing instruction to go and fetch rather than
    bytes someone already had to be on the network to supply. updUrlAllowed()
    is the difference."""
    if not device_up():
        raise Skip(f"no frame at {config.HOST}")
    status, body = config.http(
        config.dev_url("/api/update", u="https://example.com/evil.bin", t="v9.9.9", s="1"),
        method="POST", timeout=15)
    eq(status, 400, "a non-release URL must be refused")
    contains(body.decode(errors="replace").lower(), "release asset", "refusal reason")
