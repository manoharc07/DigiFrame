"""
Device tests — the firmware's own behaviour, over the LAN.

Split into three groups:

  parsers  the pure functions behind the ESPN provider, exercised through
           GET /api/devtest so the assertions land on the real compiled
           firmware and not on a host-side copy that would drift from it
  device   the HTTP API surface every front end depends on
  panel    what the LEDs are actually showing, via the capture shadow

Needs a powered, reachable frame; skips cleanly otherwise.
"""
from harness import test, eq, ok, within, contains, Skip
import config
from config import dev_get, dev_json, dev_post, device_up

_up = None


def need_device():
    global _up
    if _up is None:
        _up = device_up()
    if not _up:
        raise Skip(f"no frame at {config.HOST}")


def devtest(**params) -> dict:
    need_device()
    return dev_json("/api/devtest", **params)


# ---- parsers ------------------------------------------------------------
@test("parsers")
def test_cricket_score_strings():
    """Every case here came from real ESPN data, and two of them were bugs
    found on the panel: a completed innings ("426") was rendering as "has
    not batted", and the overs of the batting side were being wiped by the
    fielding side sharing the buffer."""
    cases = [
        # (ESPN string,                 runs, wkts, overs)
        ("161/4 (53 ov)",                161,   4,  "53"),
        ("198 & 161/4 (53 ov)",          161,   4,  "53"),   # take last innings
        ("426",                          426,  10,  ""),     # all out, bare runs
        ("305 & 128/3 (26 ov)",          128,   3,  "26"),
        # Limited-overs keeps the "bowled/total" form: "18.2" alone is
        # ambiguous without knowing the format, and "18.2/20" is exactly 7
        # chars, which is the most LiveMatch.period[8] can hold.
        ("148/4 (18.2/20 ov, target 148)", 148,  4,  "18.2/20"),
        ("120/5 (27.2/42 ov, target 117)", 120,  5,  "27.2/42"),
        ("108/8",                        108,   8,  ""),
        ("",                               0,  -1,  ""),     # not batted yet
    ]
    for s, runs, wkts, overs in cases:
        r = devtest(cricket=s)
        eq(r["runs"], runs, f"runs of {s!r}")
        eq(r["wkts"], wkts, f"wickets of {s!r}")
        if overs:
            eq(r["overs"], overs, f"overs of {s!r}")


@test("parsers")
def test_cricket_overs_not_clobbered_by_empty_score():
    """The fielding side has no "(53 ov)"; parsing it must leave the batting
    side's overs alone. Both sides share one buffer in espnLive()."""
    eq(devtest(cricket="")["overs"], "", "empty score writes no overs")
    eq(devtest(cricket="426")["overs"], "", "completed innings writes no overs")


@test("parsers")
def test_hex_colour():
    def rgb565(r, g, b):
        return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    eq(devtest(color="e31837")["rgb565"], rgb565(0xE3, 0x18, 0x37), "KC red")
    eq(devtest(color="#e20520")["rgb565"], rgb565(0xE2, 0x05, 0x20), "leading # tolerated")
    # ESPN leaves colour empty for most cricket sides -> must fall back
    eq(devtest(color="")["rgb565"], rgb565(1, 2, 3), "empty falls back")
    eq(devtest(color="zzz")["rgb565"], rgb565(1, 2, 3), "garbage falls back")


@test("parsers")
def test_team_ink_floor():
    """The All Blacks' catalogue colour is RGB565(20,20,20) — near-black on a
    black card, which made NZL's score and every event invisible."""
    def mk(r, g, b):
        return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    dark = mk(20, 20, 20)
    lifted = devtest(ink=dark)["ink"]
    ok(lifted != dark, "near-black is lifted")
    ok((lifted >> 11) > (dark >> 11), "red channel raised")
    bright = mk(226, 5, 32)
    eq(devtest(ink=bright)["ink"], bright, "a normal colour is left alone")


@test("parsers")
def test_iso_date():
    r = devtest(utc="2026-08-21T19:00Z")
    within(r["epoch"], 1_700_000_000, 2_200_000_000, "epoch in a sane range")
    eq(devtest(utc="")["epoch"], 0, "empty date -> 0")
    eq(devtest(utc="garbage")["epoch"], 0, "unparsable date -> 0")


@test("parsers")
def test_score_delta_to_event():
    """The derived-event mapping: with no play feed, a score change is all
    the firmware gets, and each sport decides what a delta means."""
    cases = [
        # sportIdx, dScore, dScore2, expected native event
        (0, 4, 0, "four"),      (0, 6, 0, "six"),
        (0, 1, 0, "run"),       (0, 0, 1, "wicket"),
        (1, 1, 0, "goal"),      (1, 0, 0, ""),
        (2, 3, 0, "three"),     (2, 2, 0, "score"),   (2, 1, 0, "ft"),
        (3, 6, 0, "td"),        (3, 3, 0, "fg"),      (3, 1, 0, "xp"),
        (3, 7, 0, "td"),        (3, 2, 0, "twopt"),
        (4, 1, 0, "goal"),
        (5, 5, 0, "try"),       (5, 3, 0, "pen"),     (5, 2, 0, "con"),
    ]
    for sport, d1, d2, want in cases:
        r = devtest(delta=f"{sport}:{d1}:{d2}")
        eq(r["event"], want, f"{r['sport']} delta +{d1}/{d2}")


# ---- device API ---------------------------------------------------------
@test("device")
def test_core_endpoints_respond():
    need_device()
    # GET-able endpoints only — /api/sports, /api/msg etc. are POST-only and
    # the ESP32 WebServer answers 404 for a method it has no handler for.
    for path in ("/api/config", "/api/list", "/api/events", "/api/teams",
                 "/api/catalogue", "/api/logs"):
        status, _ = dev_get(path)
        eq(status, 200, f"GET {path}")
    cfg = dev_json("/api/config")
    for key in ("heap", "wifi", "mode", "sportEn", "sportSrc", "sportErr"):
        contains(cfg, key, "/api/config key")


@test("device")
def test_heap_headroom():
    """Internal DRAM is the binding resource. The ESPN provider was designed
    around plain HTTP precisely to avoid a third TLS session; if free heap
    has collapsed, something started doing TLS again."""
    need_device()
    heap = dev_json("/api/config")["heap"]
    ok(heap >= 25, f"free internal heap {heap}KB — expected ~35-45KB")


@test("device")
def test_espn_catalogue_cached():
    need_device()
    status, body = dev_get("/api/espnteams", s="football")
    eq(status, 200, "/api/espnteams")
    import json
    teams = json.loads(body)
    if not teams:
        raise Skip("catalogue not fetched yet (POST /api/espnrefresh?s=football)")
    ok(len(teams) >= 10, f"{len(teams)} teams cached")
    t = teams[0]
    for key in ("i", "a", "n"):
        contains(t, key, "catalogue entry key")
    ok(t["i"].isdigit(), f"ESPN team id is numeric, got {t['i']!r}")


@test("device")
def test_following_a_team_stores_its_espn_league():
    """The picker sends ESPN's own id and league; discovery needs both. Uses a
    real team so the round trip proves the store, then removes it again."""
    need_device()
    import json
    key = "football/86"                       # Real Madrid, ESPN id 86
    dev_post("/api/teamdel", k=key)           # start clean
    before = json.loads(dev_get("/api/teams")[1])
    try:
        eq(dev_post("/api/espnfollow", s="football", e="86",
                    n="Real Madrid", a="RM", l="esp.1")[0], 200, "follow")
        teams = json.loads(dev_get("/api/teams")[1])
        rm = next((t for t in teams if t["key"] == key), None)
        ok(rm, "followed team appears in /api/teams")
        eq(rm["espn"], "86", "ESPN id stored")
        eq(rm["lg"], "esp.1", "ESPN league stored (not the module default eng.1)")
        eq(rm["abbr"], "RM", "abbreviation stored")
    finally:
        dev_post("/api/teamdel", k=key)
    eq(len(json.loads(dev_get("/api/teams")[1])), len(before), "cleaned up")


@test("device")
def test_following_a_whole_league_round_trips():
    need_device()
    import json
    key = "football/eng.1"
    dev_post("/api/teamdel", k=key)
    before = len(json.loads(dev_get("/api/teams")[1]))
    try:
        eq(dev_post("/api/leaguefollow", s="football", l="eng.1",
                    n="English Premier League")[0], 200, "follow a league")
        lg = next((t for t in json.loads(dev_get("/api/teams")[1])
                   if t["key"] == key), None)
        ok(lg, "league appears in /api/teams")
        eq(lg["kind"], "league", "recorded as a league, not a team")
        eq(lg["lg"], "eng.1", "league id stored")
        eq(lg["espn"], "", "a league has no team id")
    finally:
        dev_post("/api/teamdel", k=key)
    eq(len(json.loads(dev_get("/api/teams")[1])), before, "cleaned up")


@test("device")
def test_pinning_a_match_round_trips():
    """A pin carries every id the live tick needs, so the frame skips discovery.
    Uses a real fixture; the pin releases itself when the match ends, so this
    also has to clean up explicitly."""
    need_device()
    try:
        eq(dev_post("/api/pin", s="nfl", l="nfl", e="401874393", h="3", a="5")[0],
           200, "pin")
        eq(dev_json("/api/config")["sportPin"], "401874393", "pin recorded")
        # an incomplete pin must be refused rather than half-applied
        eq(dev_post("/api/pin", s="nfl", l="nfl", e="401874393")[0], 400,
           "pin without team ids is rejected")
        eq(dev_json("/api/config")["sportPin"], "401874393", "still the good pin")
    finally:
        dev_post("/api/unpin")
    eq(dev_json("/api/config")["sportPin"], "", "unpinned")


@test("device")
def test_sport_favourites_gate_the_panel():
    """Switching a sport off is a filter over everything followed under it."""
    need_device()
    cat = dev_json("/api/catalogue")
    idx = next(i for i, c in enumerate(cat) if c["key"] == "hockey")
    try:
        eq(dev_post("/api/sportsel", i=idx, v=0)[0], 200, "switch hockey off")
        mask = dev_json("/api/config")["sportMask"]
        eq((mask >> idx) & 1, 0, "bit cleared in sportMask")
        ok(not dev_json("/api/catalogue")[idx]["on"], "catalogue reports it off")
    finally:
        dev_post("/api/sportsel", i=idx, v=1)
    eq((dev_json("/api/config")["sportMask"] >> idx) & 1, 1, "restored")


@test("device")
def test_rotation_setting_persists():
    need_device()
    was = dev_json("/api/config")["sportRot"]
    try:
        eq(dev_post("/api/sports", en=1, s="http", h=5, f=1, r=45)[0], 200, "set")
        eq(dev_json("/api/config")["sportRot"], 45, "rotation stored")
        dev_post("/api/sports", en=1, s="http", h=5, f=1, r=9999)
        eq(dev_json("/api/config")["sportRot"], 300, "clamped to the maximum")
    finally:
        dev_post("/api/sports", en=1, s="http", h=5, f=1, r=was)


@test("device")
def test_catalogue_exposes_espn_sport_slugs():
    """The picker maps an ESPN search result onto a sport with these, so a new
    sport_*.h becomes searchable without editing the dashboard."""
    need_device()
    cat = dev_json("/api/catalogue")
    eq(len(cat), 6, "six sports")
    slugs = {c["key"]: c["espn"] for c in cat}
    eq(slugs["football"], "soccer", "our football is ESPN's soccer")
    eq(slugs["nfl"], "football", "our nfl is ESPN's football")
    for c in cat:
        ok(c["espn"], f"{c['key']} has an ESPN sport slug")
        ok(c["fx"], f"{c['key']} lists its animations")


@test("device")
def test_dev_overrides_round_trip():
    """/api/dev is what makes the panel reviewable at all — see dev/README."""
    need_device()
    eq(dev_post("/api/dev", hour=22, wcode=73)[0], 200, "set overrides")
    eq(dev_post("/api/dev", clear=1)[0], 200, "clear overrides")
    eq(dev_post("/api/stop")[0], 200, "stop")


# ---- panel --------------------------------------------------------------
def grab():
    """One frame as (H,W,3) uint8. Mirrors dev/panelshot.py's decode."""
    need_device()
    try:
        import numpy as np
    except ImportError:
        raise Skip("numpy not installed")
    import time
    for _ in range(30):
        status, body = dev_get("/api/frame")
        if status == 503:            # armed, no frame presented yet
            time.sleep(0.1)
            continue
        eq(status, 200, "/api/frame")
        eq(len(body), 64 * 64 * 2, "frame size")
        px = np.frombuffer(body, dtype="<u2").reshape(64, 64)
        r, g, b = (px >> 11) & 0x1F, (px >> 5) & 0x3F, px & 0x1F
        return np.dstack([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)])
    raise Skip("panel never presented a frame")


@test("panel")
def test_clock_block_is_drawn():
    """Rows 0-18 are the clock and are inviolate — every mode keeps them."""
    f = grab()
    lit = (f[0:19].sum(axis=2) > 40).sum()
    within(int(lit), 20, 800, "lit pixels in the clock block")


@test("panel")
def test_scene_sprites_never_enter_the_clock_block():
    """The ambient balloon used to rise over the AM/PM text."""
    import numpy as np
    import time
    need_device()
    dev_post("/api/stop")
    time.sleep(1.0)
    worst = 0
    for _ in range(12):
        f = grab()
        # row 19 is the divider; 0-18 belong to the clock alone
        worst = max(worst, int((f[0:19].sum(axis=2) > 40).sum()))
        time.sleep(0.4)
    within(worst, 20, 900, "clock block stays within its own budget")


@test("panel")
def test_score_card_renders_for_every_sport():
    """Drives each sport's card and checks it actually painted rows 21-54.
    Catches a body that throws or draws nothing without needing a live match."""
    import time
    need_device()
    for sport in range(6):
        eq(dev_post("/api/dev", sport=sport)[0], 200, f"force sport {sport}")
        time.sleep(1.6)
        f = grab()
        lit = int((f[21:55].sum(axis=2) > 40).sum())
        ok(lit > 60, f"sport {sport} card drew only {lit} lit pixels")
    dev_post("/api/stop")


@test("panel")
def test_every_sport_lands_on_the_shared_grid():
    """The card used to be welded to the clock — one blank row above the
    divider and one below, invisible at this pitch — while eight rows at the
    bottom held nothing. Every sport now shares one origin, so this checks the
    grid itself rather than any single layout: a real gap under the clock, the
    divider where it belongs, and nothing spilling into the margin."""
    import time
    need_device()
    for sport in range(6):
        eq(dev_post("/api/dev", sport=sport)[0], 200, f"force sport {sport}")
        time.sleep(1.6)
        f = grab()
        lit = lambda y: int((f[y].sum(axis=1) > 40).sum())
        eq(lit(18), 0, f"sport {sport}: row 18 is the gap under the clock")
        eq(lit(19), 0, f"sport {sport}: row 19 is the gap under the clock")
        ok(lit(20) > 50, f"sport {sport}: divider spans row 20, got {lit(20)}")
        # row 21 is where the chrome starts: two 3px edge bars plus the 2px
        # live dot. Body content must not be up here.
        ok(lit(21) <= 10, f"sport {sport}: row 21 holds {lit(21)} px — body "
                          "content has crept above the divider clearance")
        # rows 60-63 belong to the margin; only the two 3px edge bars reach them
        for y in (61, 62):
            ok(lit(y) <= 6, f"sport {sport}: row {y} margin holds {lit(y)} px")
    dev_post("/api/stop")


@test("panel")
def test_match_comet_matches_the_clock_comet():
    """The signature: the card's progress meter is the clock's seconds comet,
    same colour and same pulsing head, one row set lower. If someone retunes
    one and not the other, the two halves of the panel stop rhyming."""
    import numpy as np
    import time
    need_device()
    dev_post("/api/dev", sport=3)          # NFL always has a period and clock
    time.sleep(1.6)
    f = grab()
    trail = [tuple(int(v) for v in f[58][x]) for x in range(6, 12)]
    clock = [tuple(int(v) for v in f[17][x]) for x in range(6, 12)]
    lit_clock = [c for c in clock if sum(c) > 40]
    if not lit_clock:
        raise Skip("the seconds comet has not reached x=6 yet")
    ok(any(c in lit_clock for c in trail),
       f"meter trail {trail[:2]} should use the comet's colour {lit_clock[0]}")
    row = (f[58].sum(axis=1) > 40).sum()
    ok(row > 40, f"meter spans the card, got {row} lit px")
    dev_post("/api/stop")
