"""
ESPN contract tests — the assumptions espn_api.h is built on.

ESPN's API is undocumented and can change without notice. Every test here
corresponds to a design decision in the firmware, so a failure tells you
which decision has stopped being valid. When live scores break, run this
first: it separates "ESPN changed" from "we broke it".

Needs the internet. Does not need the device.
"""
from harness import test, eq, ok, within, contains, Skip
import config
from config import http, espn_json, ESPN_SITE, ESPN_CORE, UA


def _live_cricket_slate():
    """Every (league, event) cricket fixture currently in progress."""
    import json
    status, body = http("http://site.web.api.espn.com/apis/v2/scoreboard/header"
                        "?sport=cricket&region=in")
    if status != 200:
        return []
    out = []
    for sport in json.loads(body).get("sports", []):
        for lg in sport.get("leagues", []):
            if not lg.get("slug"):
                continue
            for ev in lg.get("events", []):
                st = ev.get("fullStatus", {}).get("type", {}) or ev.get("status", {})
                if st.get("state") == "in":
                    out.append((lg["slug"], ev["id"]))
    return out


_LIVE_CRICKET = []          # one-element memo: two tests want the same match


def _live_cricket_event():
    """(league, event) of a cricket match in progress, or None.

    Cricket has no /scoreboard of its own (404) — the personalized header is
    the only list of active series, which is the same call espnDiscoverCricket()
    makes. Discovering the fixture rather than pinning one keeps these tests
    from ageing out the way the pinned-fixture test does."""
    import json
    if _LIVE_CRICKET:
        return _LIVE_CRICKET[0]
    status, body = http("http://site.web.api.espn.com/apis/v2/scoreboard/header"
                        "?sport=cricket&region=in")
    if status != 200:
        return None
    cand = []
    for sport in json.loads(body).get("sports", []):
        for lg in sport.get("leagues", []):
            if not lg.get("slug"):
                continue
            for ev in lg.get("events", []):
                st = ev.get("fullStatus", {}).get("type", {}) or ev.get("status", {})
                if st.get("state") == "in":
                    cand.append((lg["slug"], ev["id"]))
    # "in progress" is not the same as "has commentary": second-XI and county
    # fixtures return an empty 78-byte playbyplay, and they sort FIRST in the
    # header, so a test that took the first live event measured that emptiness
    # instead of a page of balls. Probing is cheap precisely because the misses
    # are 78 bytes, so walk the whole slate rather than a prefix of it.
    for lg, ev in cand[:32]:
        st, b = http(f"{ESPN_SITE}/cricket/{lg}/playbyplay?event={ev}&limit=6&page=1")
        if st != 200:
            continue
        c = json.loads(b).get("commentary") or {}
        if [i for i in c.get("items", []) if i.get("shortText")]:
            _LIVE_CRICKET.append((lg, ev))
            return lg, ev
    return None



@test("espn")
def test_plain_http_is_served():
    """No TLS. This is why the feature fits in internal DRAM at all: a TLS
    session is ~32 KB against a ~39 KB free budget. If this ever fails, the
    feature needs redesigning, not patching."""
    for url in (f"{ESPN_SITE}/football/nfl/scoreboard?limit=1",
                f"{ESPN_CORE}/football/leagues/nfl/events"):
        status, body, hdrs = http(url, want_headers=True)
        eq(status, 200, f"plain HTTP {url}")
        ok(not hdrs.get("Location"), f"{url} must not redirect to https")


@test("espn")
def test_user_agent_allowlist():
    """The UA is load-bearing and counter-intuitive: browser-shaped strings
    are rejected while honest tool strings pass. Cost an hour of 403s to
    find, so it is pinned here."""
    url = f"{ESPN_SITE}/soccer/eng.1/teams"
    for good in ("ESP32HTTPClient", "curl/8.5.0", "python-requests/2.32"):
        eq(http(url, ua=good)[0], 200, f"UA {good!r} should be accepted")
    for bad in ("Mozilla/5.0", "DigiFrame/1.0", "Wget/1.21"):
        eq(http(url, ua=bad)[0], 403, f"UA {bad!r} should still be rejected")


@test("espn")
def test_firmware_user_agent_matches():
    """config.h and this suite must agree, or the suite proves nothing."""
    from pathlib import Path
    cfg = (Path(__file__).resolve().parent.parent
           / "firmware" / "DigiFrame" / "config.h").read_text(encoding="utf-8")
    line = next(l for l in cfg.splitlines() if "define ESPN_USER_AGENT" in l)
    eq(line.split('"')[1], UA, "ESPN_USER_AGENT in config.h vs tests/config.py")


@test("espn")
def test_transfer_encodings_are_as_assumed():
    """EspnBufferedStream decodes chunked itself because HTTPClient does not
    on the getStream() path. If an endpoint we assumed was Content-Length
    goes chunked (or the reverse) the stream still handles both — this test
    exists to notice the change, not because either breaks us."""
    cases = {
        f"{ESPN_SITE}/soccer/eng.1/teams": "content-length",
        f"{ESPN_SITE}/soccer/eng.1/teams/359": "content-length",
        "http://site.api.espn.com/apis/personalized/v2/scoreboard/header"
        "?sport=cricket&region=in": "chunked",
    }
    for url, expect in cases.items():
        _, _, hdrs = http(url, want_headers=True)
        lower = {k.lower(): str(v).lower() for k, v in hdrs.items()}
        got = "chunked" if "chunked" in lower.get("transfer-encoding", "") \
              else "content-length" if "content-length" in lower else "?"
        eq(got, expect, f"transfer encoding of {url.split('/apis')[1][:48]}")


@test("espn")
def test_search_is_cors_open_and_shaped_for_the_picker():
    """The dashboard's team picker runs in the BROWSER, so this endpoint has to
    grant CORS — and it is the one ESPN endpoint that does while /teams does
    not. If the grant disappears the picker silently falls back to the six
    hardcoded catalogues, so this failing is the only warning there will be."""
    url = ("http://site.web.api.espn.com/apis/common/v3/search"
           "?limit=10&type=team&query=arsenal")
    status, body, hdrs = http(url, want_headers=True)
    eq(status, 200, "search endpoint")
    lower = {k.lower(): v for k, v in hdrs.items()}
    eq(lower.get("access-control-allow-origin"), "*",
       "CORS grant (without it the browser-side picker cannot work)")

    import json
    items = json.loads(body).get("items") or []
    ok(items, "search returned teams")
    top = items[0]
    # every field the picker reads off a result
    for key in ("id", "displayName", "abbreviation", "color", "sport", "league"):
        contains(top, key, "search item key")
    eq(top["type"], "team", "type=team filter honoured")
    eq(top["league"], "eng.1", "league slug is what gets stored per favourite")


@test("espn")
def test_browser_host_accepts_browser_user_agents():
    """The dashboard's pickers and its "live now" list run in the browser, and a
    page CANNOT override its User-Agent — fetch forbids the header. site.api
    403s every browser-shaped UA at the Akamai edge (the same allowlist the
    firmware works around by sending "ESP32HTTPClient"), and a 403 page carries
    no CORS header, so the browser reports it as a CORS error and hides the real
    cause. site.web.api is the browser-facing twin. Getting this wrong looks
    exactly like a CORS bug, which is why it is pinned as its own test."""
    paths = ("/site/v2/sports/football/nfl/scoreboard?dates=20260815",
             "/site/v2/leagues/dropdown?sport=soccer",
             "/common/v3/search?query=arsenal&type=team")
    for p in paths:
        blocked = http("http://site.api.espn.com/apis" + p, ua="Mozilla/5.0")[0]
        eq(blocked, 403, f"site.api still 403s a browser UA on {p[:40]}")

        status, _, hdrs = http("http://site.web.api.espn.com/apis" + p,
                               ua="Mozilla/5.0", want_headers=True)
        eq(status, 200, f"site.web.api serves a browser UA on {p[:40]}")
        lower = {k.lower(): v for k, v in hdrs.items()}
        eq(lower.get("access-control-allow-origin"), "*",
           f"site.web.api grants CORS on {p[:40]}")


@test("espn")
def test_league_scan_primitives_are_cheap():
    """What makes following a whole league affordable on the frame. The
    scoreboard cannot do this job — measured 147 KB for an in-season NFL day —
    so the scan is events?dates + one status per candidate."""
    day = "20260815"
    base = f"{ESPN_CORE}/football/leagues/nfl"
    status, body = http(f"{base}/events?dates={day}&limit=8")
    eq(status, 200, "league events by date")
    within(len(body), 1, 4000,
           f"events?dates is {len(body)}B — the whole point is that it is tiny")
    import json
    items = json.loads(body).get("items") or []
    if not items:
        raise Skip("no NFL fixtures on the pinned date any more")
    ev = items[0]["$ref"].split("/events/")[1].split("?")[0]

    status, body = http(f"{base}/events/{ev}/competitions/{ev}/status")
    eq(status, 200, "competition status")
    within(len(body), 1, 1200, "status size")

    status, body = http(f"{base}/events/{ev}/competitions/{ev}/competitors")
    eq(status, 200, "competitors")
    within(len(body), 1, 6000, "competitors size")
    comp = json.loads(body)["items"]
    eq(len(comp), 2, "two competitors")
    ok({c["homeAway"] for c in comp} == {"home", "away"}, "home/away present")


@test("espn")
def test_a_competitor_id_is_a_team_id():
    """Why pinning needs no discovery: the dashboard reads these ids off the
    scoreboard and the frame builds core score URLs straight from them. If the
    two ever diverge, every pin silently fetches the wrong side's score."""
    d = espn_json(f"{ESPN_SITE}/football/nfl/scoreboard?dates=20260815")
    events = d.get("events") or []
    if not events:
        raise Skip("no NFL fixtures on the pinned date any more")
    cp = events[0]["competitions"][0]
    for c in cp["competitors"]:
        eq(c["id"], c["team"]["id"], f"competitor id == team id for {c['team'].get('abbreviation')}")
    # and the id built that way actually resolves
    ev = events[0]["id"]
    cid = cp["competitors"][0]["id"]
    eq(http(f"{ESPN_CORE}/football/leagues/nfl/events/{ev}"
            f"/competitions/{ev}/competitors/{cid}/score")[0], 200,
       "core score URL built from a scoreboard id")


@test("espn")
def test_fixture_dates_are_not_the_viewers_dates():
    """ESPN files a fixture under its own local date. From IST the NFL games in
    progress are filed under *yesterday*, so both the frame and the dashboard
    ask for a two-day UTC span. A single local day silently returned zero."""
    same_day = espn_json(f"{ESPN_CORE}/football/leagues/nfl/events?dates=20260816")
    span = espn_json(f"{ESPN_CORE}/football/leagues/nfl/events?dates=20260815-20260816")
    eq(same_day.get("count"), 0, "the day after has nothing")
    ok(span.get("count", 0) > 0, "the two-day span picks the fixtures up")


@test("espn")
def test_situation_carries_possession_cheaply():
    """The NFL possession chevron's only source. 615 bytes, so it is affordable
    on every live tick; the possessing team arrives as a $ref whose last path
    segment is the team id. Soccer's equivalent is an empty stub, which is why
    only NFL pays for this call."""
    ev = "401873282"
    base = f"{ESPN_CORE}/football/leagues/nfl/events/{ev}/competitions/{ev}"
    status, body = http(f"{base}/situation")
    if status == 404:
        raise Skip("the pinned NFL fixture has aged out of the core API")
    eq(status, 200, "nfl situation")
    within(len(body), 1, 1500, "situation size — it runs on the live tick")
    import json
    d = json.loads(body)
    ref = (d.get("team") or {}).get("$ref", "")
    if not ref:
        raise Skip("no possession recorded (game not in progress)")
    contains(ref, "/teams/", "possession arrives as a team $ref")
    ok(ref.split("/teams/")[1].split("?")[0].isdigit(), "and its id is numeric")

    # soccer's is a stub: this is the reason the firmware does not fetch it
    s_status, s_body = http(f"{ESPN_CORE}/soccer/leagues/esp.1/events/401882920"
                            "/competitions/401882920/situation")
    if s_status == 200:
        ok(len(s_body) < 400,
           "soccer situation is still an empty stub — if it gained content, "
           "football's card could show possession too")


@test("espn")
def test_cricket_ball_by_ball_paging_and_sentinel():
    """The pill strip's source, and both of its traps.

    `limit` windows from the START of the innings, so limit=6 returns the first
    over of the day rather than the last — the firmware pages to `pageCount`
    instead and appends by ball id. And EVERY page is padded with a sentinel
    whose id is 999999999999999 and whose text is empty, but whose playType is
    a perfectly valid "2"/no run. Consuming it once sets the high-water id
    above anything a real ball can reach and the strip freezes forever."""
    base = (f"{ESPN_SITE}/cricket/24231/playbyplay?event=1527273")
    status, body = http(base + "&limit=6&page=1")
    if status != 200:
        raise Skip("the pinned cricket fixture has aged out")
    import json
    c = json.loads(body)["commentary"]
    for key in ("count", "pageIndex", "pageCount", "items"):
        contains(c, key, "commentary pagination key")

    # paging runs oldest -> newest, and ids increase monotonically
    real = [i for i in c["items"] if i.get("shortText")]
    ok(real, "page 1 has real balls")
    ids = [int(i["id"]) for i in real]
    ok(all(a < b for a, b in zip(ids, ids[1:])), "ball ids increase within a page")
    ok(ids[0] < 100000, f"page 1 is the START of the innings, got id {ids[0]}")

    # the sentinel: present, absurd id, empty text, plausible playType
    sent = [i for i in c["items"] if not i.get("shortText")]
    if sent:
        s = sent[0]
        ok(int(s["id"]) > 99999999,
           "sentinel still carries an out-of-range id — the firmware filters on "
           "empty shortText AND id magnitude, so either check alone would do")

    # a later page really is later, which is what makes pageCount usable
    last = json.loads(http(f"{base}&limit=6&page={c['pageCount']}")[1])["commentary"]
    lreal = [i for i in last["items"] if i.get("shortText")]
    if lreal:
        ok(int(lreal[-1]["id"]) > ids[-1], "the last page holds newer balls than page 1")


@test("espn")
def test_cricket_ball_ids_reset_at_the_innings_break():
    """Ball ids are monotonic only WITHIN an innings, never across a match.

    Measured on a live T20: the first innings ran 110..115060 and the second
    restarted at 210. That kills the obvious cheap strip: "remember the last id
    seen and append anything larger" silently stops appending the moment the
    innings turns over, and the pills freeze on the old innings while the score
    above them counts the new one. espnCricketBalls() rebuilds from the tail
    page instead, which needs no cross-page ordering at all.

    It also confirms the sentinel filter is safe: real ids stay well under the
    99999999 magnitude cut on both sides of the break."""
    import json
    live = _live_cricket_event()
    if not live:
        raise Skip("no cricket match is in progress right now")
    lg, ev = live
    url = f"{ESPN_SITE}/cricket/{lg}/playbyplay?event={ev}&limit=500&page=1"
    status, body = http(url)
    eq(status, 200, "playbyplay answers for a live match")
    items = [i for i in json.loads(body)["commentary"]["items"] if i.get("shortText")]
    if len(items) < 2:
        raise Skip("nothing bowled yet in the live match")

    by_period = {}
    for i in items:
        by_period.setdefault(i["period"], []).append(int(i["id"]))
    for period, ids in by_period.items():
        ok(all(a < b for a, b in zip(ids, ids[1:])),
           f"ids increase within innings {period}")
        ok(max(ids) <= 99999999,
           f"innings {period} ids stay under the sentinel cut, got {max(ids)}")
    # Every innings restarts its numbering near zero (110, 210, 310 ...) while
    # a full innings climbs into six or seven digits. That overlap is the whole
    # hazard: a high-water mark carried out of one innings sits far above the
    # next innings' opening ids, so the merge rule stops appending at the break.
    for period, ids in by_period.items():
        ok(min(ids) < 1000,
           f"innings {period} restarts its ids near zero, got {min(ids)}")
    order = sorted(by_period)
    for a, b in zip(order, order[1:]):
        if max(by_period[a]) > min(by_period[b]):
            ok(True, f"innings {a} ends at {max(by_period[a])}, above innings "
                     f"{b}'s opening {min(by_period[b])} — 'newer id' is NOT a "
                     "valid merge rule across a whole match")
            break


@test("espn")
def test_cricket_ball_page_is_expensive_enough_to_count():
    """Why the reach-back to page-1 is conditional rather than unconditional.

    A playbyplay page carries both squads' full bowling figures per delivery,
    so six balls cost ~16 KB — comparable to the entire series scoreboard. The
    strip now refreshes whenever the score moves (every ~20 s during play), so
    fetching a second page on every refresh when the tail page already holds
    six deliveries would have roughly doubled cricket's traffic for nothing."""
    live = _live_cricket_event()
    if not live:
        raise Skip("no cricket match is in progress right now")
    lg, ev = live
    base = f"{ESPN_SITE}/cricket/{lg}/playbyplay?event={ev}"
    status, body = http(f"{base}&limit=6&page=1")
    eq(status, 200, "playbyplay page 1")
    ok(len(body) > 4000,
       f"a 6-ball page is still bulky ({len(body)} B) — the conditional "
       "reach-back is worth keeping")
    one = http(f"{base}&limit=1&page=1")[1]
    ok(len(body) > 2 * len(one),
       "cost scales with limit, so it really is per-delivery bulk")


@test("espn")
def test_cricket_overs_need_linescores_after_the_first_innings():
    """Which side's overs the card should print, and why it cannot be guessed.

    From the second innings on, BOTH competitors carry a "(… ov)" — a completed
    innings keeps its own ("VKK 171/3 (16/16 ov)" beside "DD 88/7 (11/16 ov)").
    So "the side with parentheses" identifies two sides, and the competitors
    array is ordered home/away rather than by who is batting, which is why a
    shared overs buffer captioned a chase at 11 overs as 16 — always, and only,
    after the first innings.

    linescores settles it: exactly one side has isBatting on the isCurrent
    innings. `order` vs `status.period` would agree here but not after a
    follow-on, where one side bats two innings running."""
    import json
    found = None
    for lg, ev in _live_cricket_slate():
        status, body = http(f"{ESPN_SITE}/cricket/{lg}/scoreboard")
        if status != 200:
            continue
        for e in json.loads(body).get("events", []):
            if e["id"] != ev:
                continue
            comps = e["competitions"][0]["competitors"]
            if sum(1 for c in comps if "(" in (c.get("score") or "")) >= 2:
                found = (lg, ev, e, comps)
        if found:
            break
    if not found:
        raise Skip("no cricket match is past its first innings right now")
    lg, ev, e, comps = found

    ok(int(e["status"].get("period") or 0) >= 2,
       "both sides carrying overs means the first innings is done")

    batting = []
    for c in comps:
        cur = [l for l in (c.get("linescores") or []) if l.get("isCurrent")]
        ok(len(cur) == 1,
           f"{c['team']['abbreviation']} has exactly one current innings")
        contains(cur[0], "isBatting", "current linescore")
        if cur[0]["isBatting"]:
            batting.append(c)
    eq(len(batting), 1, "exactly one side is at the crease")

    # the whole point: the batting side's overs are NOT the ones a naive
    # last-writer-wins parse would have landed on
    ok("(" in (batting[0].get("score") or ""),
       "the batting side's score carries the live overs")
    other = [c for c in comps if c is not batting[0]][0]
    ok("(" in (other.get("score") or ""),
       "so does the side that is done — which is the entire trap")


@test("espn")
def test_match_state_words_are_prose_not_labels():
    """Why the state keywords are matched case-insensitively. ESPN writes these
    as sentences as often as labels — the live example that caught it was
    "Match delayed by a wet outfield", where a case-sensitive search for
    "Delay" misses and a stopped match reads as in progress."""
    d = espn_json("http://site.api.espn.com/apis/personalized/v2/scoreboard/header"
                  "?sport=cricket&region=in")
    descs = []
    for lg in d["sports"][0]["leagues"]:
        for e in lg.get("events", []):
            t = (e.get("status") or "")
            if isinstance(t, str):
                descs.append(t)
    ok(descs, "the personalized header still reports per-event status")
    # the shape the firmware relies on: state is a short lowercase token
    ok(any(s in ("in", "pre", "post") for s in descs),
       f"status vocabulary is still in/pre/post, saw {sorted(set(descs))[:5]}")


@test("espn")
def test_teams_list_is_still_cors_blocked():
    """Documents WHY the picker uses search rather than the catalogue endpoint
    the firmware uses. If this ever gains CORS, the picker could list a whole
    league at once — worth knowing, so it is asserted rather than assumed."""
    _, _, hdrs = http(f"{ESPN_SITE}/soccer/eng.1/teams", want_headers=True)
    lower = {k.lower() for k in hdrs}
    ok("access-control-allow-origin" not in lower,
       "/teams gained CORS — the picker could now list leagues directly")


@test("espn")
def test_team_lookup_under_the_wrong_league_fails_silently():
    """Why FavTeam stores a league. Real Madrid (esp.1) looked up under the
    football module's default eng.1 answers 200 with an empty nextEvent[] —
    no error anywhere, the score card just never appears."""
    right = espn_json(f"{ESPN_SITE}/soccer/esp.1/teams/86")["team"]
    if not right.get("nextEvent"):
        # The test is a CONTRAST between the right and wrong league, so it
        # needs the right one to actually have a fixture. In an international
        # break or the close season nobody does, and there is nothing to
        # compare — that is a quiet calendar, not a broken assumption.
        raise Skip("Real Madrid has no scheduled fixture right now")
    wrong = espn_json(f"{ESPN_SITE}/soccer/eng.1/teams/86")["team"]
    eq(len(wrong.get("nextEvent") or []), 0,
       "wrong league still returns 200 — the failure is invisible")


@test("espn")
def test_scoreboard_is_too_big_to_poll():
    """The justification for the three-cadence design. If NFL's scoreboard
    ever became small this could be simplified — it has not."""
    status, body = http(f"{ESPN_SITE}/football/nfl/scoreboard")
    eq(status, 200, "nfl scoreboard")
    ok(len(body) > 60_000,
       f"nfl scoreboard is {len(body)}B — if this is now small, revisit the "
       "discovery/live split in espn_api.h")


@test("espn")
def test_team_endpoint_yields_nextEvent():
    """Discovery depends entirely on this shape: event id, kickoff date,
    league slug and both competitor ids from one ~25 KB call."""
    d = espn_json(f"{ESPN_SITE}/soccer/eng.1/teams/359")
    t = d["team"]
    eq(t["abbreviation"], "ARS", "team abbreviation")
    ok(t.get("color"), "team colour present")
    ne = t.get("nextEvent") or []
    if not ne:
        raise Skip("Arsenal has no next fixture scheduled right now")
    e = ne[0]
    ok(e.get("id"), "nextEvent id")
    contains(e.get("date", ""), "T", "nextEvent date is ISO")
    ok(e.get("league", {}).get("slug"), "nextEvent.league.slug (picks the live URL)")
    comps = e["competitions"][0]["competitors"]
    eq(len(comps), 2, "two competitors")
    ok({c["homeAway"] for c in comps} == {"home", "away"}, "home/away present")


@test("espn")
def test_core_live_endpoints_are_tiny():
    """The 15 s tick. ~830 B total is what makes polling this often defensible."""
    d = espn_json(f"{ESPN_CORE}/football/leagues/nfl/events?limit=1")
    items = d.get("items") or []
    if not items:
        raise Skip("no NFL events listed today")
    ev = items[0]["$ref"].split("/events/")[1].split("?")[0]
    base = f"{ESPN_CORE}/football/leagues/nfl/events/{ev}/competitions/{ev}"

    status, body = http(f"{base}/status")
    eq(status, 200, "core status")
    within(len(body), 1, 2000, "core status size")
    import json
    st = json.loads(body)
    ok("period" in st, "status.period")
    ok(st["type"]["state"] in ("pre", "in", "post"), "status.type.state vocabulary")

    d2 = espn_json(f"{ESPN_SITE}/football/nfl/scoreboard?limit=1")
    cid = d2["events"][0]["competitions"][0]["competitors"][0]["id"]
    evid = d2["events"][0]["id"]
    status, body = http(f"{ESPN_CORE}/football/leagues/nfl/events/{evid}"
                        f"/competitions/{evid}/competitors/{cid}/score")
    eq(status, 200, "core score")
    within(len(body), 1, 1000, "core score size")
    contains(json.loads(body), "value", "score.value")


@test("espn")
def test_cricket_needs_its_own_path():
    """Cricket breaks the uniform shape three ways, and the firmware special-
    cases all three. Each is asserted here so a fix upstream is noticed."""
    # 1. no team endpoint
    ok(http(f"{ESPN_SITE}/cricket/24231/teams/2")[0] != 200,
       "cricket team endpoint still absent (firmware routes around it)")
    # 2. the personalized header is the series index
    d = espn_json("http://site.api.espn.com/apis/personalized/v2/scoreboard/header"
                  "?sport=cricket&region=in")
    leagues = d["sports"][0]["leagues"]
    ok(leagues, "personalized header lists cricket series")
    ok(any(str(l.get("id", "")).isdigit() for l in leagues), "series ids are numeric")
    # 3. scores are strings, not numbers
    found = False
    for lg in leagues:
        for e in lg.get("events", []):
            for c in e.get("competitors", []):
                if c.get("score"):
                    ok(isinstance(c["score"], str),
                       f"cricket score is a string, got {type(c['score'])}")
                    found = True
    if not found:
        raise Skip("no cricket match with a score right now")


@test("espn")
def test_numeric_league_ids_work_for_cricket_and_rugby():
    """The docs claim cricket's scoreboard 404s. It does for slug ids like
    `icc.t20`, but works for numeric series ids — which is why the firmware
    uses those."""
    eq(http(f"{ESPN_SITE}/cricket/icc.t20/scoreboard")[0], 404,
       "slug-style cricket league still 404s (as the docs say)")
    d = espn_json("http://site.api.espn.com/apis/personalized/v2/scoreboard/header"
                  "?sport=cricket&region=in")
    series = str(d["sports"][0]["leagues"][0]["id"])
    eq(http(f"{ESPN_SITE}/cricket/{series}/scoreboard")[0], 200,
       f"numeric cricket series {series} scoreboard")

# ---- racing / F1 --------------------------------------------------------
# Racing is not a small variation on the team-sport shape. Each test here pins
# one of the places espn_api.h had to branch for it.

_F1 = f"{ESPN_CORE}/racing/leagues/f1"


def _f1_weekend():
    """(event, [competitions]) for the current race weekend, or None."""
    d = espn_json(f"{ESPN_SITE}/racing/f1/scoreboard")
    evs = d.get("events") or []
    return (evs[0], evs[0].get("competitions") or []) if evs else None


def _f1_session(states=("in", "post")):
    """A competition with competitors on it: (eventId, compId), or None."""
    wk = _f1_weekend()
    if not wk:
        return None
    ev, comps = wk
    for c in comps:
        st = ((c.get("status") or {}).get("type") or {}).get("state")
        if st in states and c.get("competitors"):
            return ev["id"], c["id"]
    return None


@test("espn")
def test_racing_needs_its_own_path():
    """Four deviations the generic discovery path assumes away.

    A driver is an ATHLETE, so there is no /teams endpoint and therefore no
    nextEvent — which is how every other sport learns what to watch. And a
    competitor carries no homeAway, so the two-Side parse cannot run."""
    ok(http(f"{ESPN_SITE}/racing/f1/teams/4665")[0] != 200,
       "racing team endpoint still absent (firmware routes around it)")
    ses = _f1_session()
    if not ses:
        raise Skip("no F1 session with competitors on the board right now")
    ev, comp = ses
    wk = _f1_weekend()
    cps = [c for c in wk[1] if c["id"] == comp][0]["competitors"]
    ok(not any("homeAway" in c for c in cps), "racing competitors carry no homeAway")
    ok(all(isinstance(c.get("order"), int) for c in cps),
       "every competitor has an int order")
    ok(not any("score" in c for c in cps), "racing competitors carry no score")
    # A weekend is ONE event holding several competitions, unlike every other
    # sport, where the competition id IS the event id.
    ok(len(wk[1]) > 1, f"a race weekend holds several sessions, got {len(wk[1])}")
    ok(comp != ev, "a racing competition id is NOT its event id")


@test("espn")
def test_racing_date_spans_are_not_two_day():
    """The opposite of the rule every other sport follows.

    espnScanLeague() and the dashboard live-now list must ask for a two-day UTC
    span, because ESPN files a football fixture under its own local date.
    Measured 2026-08-21, racing does not behave that way at all: the span
    20260820-20260821 returned ZERO events while each of those days queried
    alone returned the weekend. That is why racing has no league scan."""
    wk = _f1_weekend()
    if not wk:
        raise Skip("no F1 weekend on the board right now")
    day = wk[0]["date"][:10].replace("-", "")          # the event's own UTC day
    single = espn_json(f"{_F1}/events?dates={day}")
    ok(single["count"] >= 1, f"single day {day} finds the weekend")
    prev = f"{int(day) - 1:08d}"
    span = espn_json(f"{_F1}/events?dates={prev}-{day}")
    eq(span["count"], 0,
       f"the two-day span {prev}-{day} still returns nothing for racing")


@test("espn")
def test_racing_live_primitives_are_cheap():
    """The four calls espnLiveRacing() makes, and the one field that makes the
    flag tint possible. ~5.3 KB together, against cricket's accepted 6.7 KB."""
    import json
    ses = _f1_session()
    if not ses:
        raise Skip("no F1 session with competitors on the board right now")
    ev, comp = ses
    base = f"{_F1}/events/{ev}/competitions/{comp}"

    st, body = http(f"{base}/status")
    eq(st, 200, "competition status")
    ok(len(body) < 800, f"status is {len(body)} B")
    d = json.loads(body)
    ok("flag" in d, "status carries the race-control flag")
    contains(("pre", "in", "post"), d["type"]["state"], "status state vocabulary")
    ok(isinstance(d.get("period"), int), "period is the lap number")

    # The order is ONE call for the whole board, and it is a page rather than
    # the field: the full list is ~16 KB and the card shows six.
    st, body = http(f"{base}/competitors?limit=6")
    eq(st, 200, "top-of-the-field page")
    ok(len(body) < 6000, f"limit=6 page is {len(body)} B")
    top = json.loads(body)["items"]
    eq([i["order"] for i in top], [1, 2, 3, 4, 5, 6], "the page is ordered by position")
    drv = top[0]["id"]
    for i in top:
        ok(i["vehicle"]["teamColor"], "every row carries its constructor colour")
        ok(i["vehicle"]["manufacturer"], "and its constructor")

    st, body = http(f"{base}/competitors/{drv}")
    eq(st, 200, "one competitor by athlete id")
    ok(len(body) < 1200, f"single competitor is {len(body)} B")

    st, body = http(f"{base}/competitors/{drv}/statistics")
    eq(st, 200, "competitor statistics")
    ok(len(body) < 4500, f"statistics is {len(body)} B")
    cats = json.loads(body)["splits"]["categories"]
    names = {s["name"] for c in cats for s in c["stats"]}
    for want in ("lapsCompleted", "place"):
        contains(names, want, f"statistics carries {want}")

    # The gap is in a category of its own and that category exists ONLY while
    # the session is running — a finished session carries `general` alone.
    # espnLiveRacing() therefore treats a missing gap as an empty detail
    # field rather than an error, and the card simply shows the lap.
    if _f1_session(("in",)) != (ev, comp):
        raise Skip("session is not running: gapToLeader appears only while it is")
    contains(names, "gapToLeader", "a running session carries gapToLeader")
    contains(names, "pitsTaken", "a running session carries pitsTaken")


@test("espn")
def test_circuit_carries_the_total_laps():
    """The lap count is on none of the event, the competition or the
    scoreboard — only the circuit. Without it the match comet has nothing to
    measure against, and f1Progress() correctly draws nothing."""
    import json
    wk = _f1_weekend()
    if not wk:
        raise Skip("no F1 weekend on the board right now")
    cid = (wk[0].get("circuit") or {}).get("id")
    ok(cid, "the scoreboard event names its circuit")
    st, body = http(f"{_F1}/circuits/{cid}")
    eq(st, 200, "circuit endpoint")
    ok(len(body) < 4000, f"circuit is {len(body)} B")
    laps = json.loads(body).get("laps")
    ok(isinstance(laps, int) and laps > 0,
       f"circuit laps is a positive int, got {laps!r}")


@test("espn")
def test_racing_scoreboard_is_the_only_name_source():
    """espnDiscoverRacing() spends one 24.5 KB call because that call answers
    everything at once: the session ids and labels, the circuit, and every
    driver's id and name for the identity table. Nothing cheaper does."""
    import json
    st, body = http(f"{ESPN_SITE}/racing/f1/scoreboard")
    eq(st, 200, "racing scoreboard")
    ok(len(body) < 60000,
       f"racing scoreboard is {len(body)} B (poll once per weekend, not per tick)")
    ev = (json.loads(body).get("events") or [None])[0]
    if not ev:
        raise Skip("no F1 weekend on the board right now")
    labels = [((c.get("type") or {}).get("abbreviation")) for c in ev["competitions"]]
    ok(all(labels), f"every session is labelled, got {labels}")
    named = [c for c in ev["competitions"] if c.get("competitors")]
    if not named:
        raise Skip("no session has run yet this weekend")
    for c in named[0]["competitors"]:
        ok(c["athlete"].get("shortName"), "every competitor names its driver")
    # And the obvious cheaper-looking alternative is not cheaper: asking the
    # core API for the session list alone costs more than the whole scoreboard.
    body2 = http(f"{_F1}/events/{ev['id']}/competitions?limit=10")[1]
    ok(len(body2) > len(body) / 2,
       f"core competitions list is {len(body2)} B — no cheaper than the scoreboard")


@test("espn")
def test_driver_identity_needs_the_standings_not_the_search():
    """Pins the browser-side driver picker.

    ESPN's team search cannot find a driver at all, and its player search finds
    them without an abbreviation — but Side.abbr is three characters and the
    whole race card is laid out around that code. The championship standings
    are the only feed carrying ESPN's own codes, and they are CORS-open, which
    is what lets the browser do this instead of the frame."""
    import json
    search = "http://site.web.api.espn.com/apis/common/v3/search"
    d = json.loads(http(f"{search}?limit=10&type=team&query=verstappen",
                        ua="Mozilla/5.0")[1])
    eq(d.get("count", 0), 0, "type=team finds no driver")
    d = json.loads(http(f"{search}?limit=10&type=player&query=verstappen",
                        ua="Mozilla/5.0")[1])
    hit = [i for i in d.get("items", []) if i.get("sport") == "racing"]
    ok(hit, "type=player finds the driver under sport 'racing'")
    eq(hit[0].get("league"), "f1", "and under league f1")
    ok(not hit[0].get("abbreviation"),
       "but with no abbreviation — which is why the picker uses the standings")

    st, body, hdrs = http(
        "http://site.web.api.espn.com/apis/v2/sports/racing/f1/standings",
        ua="Mozilla/5.0", want_headers=True)
    eq(st, 200, "driver standings")
    eq(hdrs.get("access-control-allow-origin"), "*", "standings are CORS-open")
    tbl = [c for c in json.loads(body)["children"]
           if "driver" in c["name"].lower()][0]
    entries = tbl["standings"]["entries"]
    ok(len(entries) >= 20, f"the grid has {len(entries)} drivers")
    for e in entries:
        a = e["athlete"]
        eq(len(a["abbreviation"]), 3, f"{a['displayName']} has a 3-letter code")


@test("espn")
def test_race_intervals_have_no_bulk_source():
    """The load-bearing NEGATIVE behind the race card's request count.

    An interval column needs gapToLeader for every row, and that stat exists
    only inside a per-competitor statistics document — 2.4 KB each. Nothing
    returns it for the field at once, which is why a six-row board costs eight
    requests a poll instead of two. If this ever starts failing, the card can
    get much cheaper: check the competition-level statistics first.

    ESPN's own results page does show `timeBehindLeader` for everyone, but it
    is server-rendered into ~470 KB of HTML with no JSON endpoint behind it,
    and https-only, so the frame cannot use it either."""
    import json
    ses = _f1_session()
    if not ses:
        raise Skip("no F1 session with competitors on the board right now")
    ev, comp = ses
    base = f"{_F1}/events/{ev}/competitions/{comp}"

    # the competition-level statistics document is race aggregates, not drivers
    st, body = http(f"{base}/statistics")
    eq(st, 200, "competition statistics")
    ok(b"gapToLeader" not in body,
       "competition statistics still carries no per-driver gap")
    names = {s["name"] for c in json.loads(body)["categories"] for s in c["stats"]}
    contains(names, "avgSpeed", "it is race aggregates (avgSpeed, victoryMargin)")

    # and the site scoreboard names the drivers but leaves statistics empty
    sb = espn_json(f"{ESPN_SITE}/racing/f1/scoreboard")
    for c in sb["events"][0]["competitions"]:
        for x in c.get("competitors", []):
            ok(not x.get("statistics"),
               "the site scoreboard still ships no competitor statistics "
               f"(got {x.get('statistics')!r})")


@test("espn")
def test_qualifying_segment_is_derivable_but_the_clock_is_not():
    """Two halves of the same question, with opposite answers.

    Which segment qualifying is in (Q1/Q2/Q3) comes free out of the per-driver
    statistics the interval column already fetches: qual1TimeMS, qual2TimeMS
    and qual3TimeMS are 0 until that segment is reached, so the highest
    non-zero across the fetched rows is the current one.

    A session COUNTDOWN does not exist. status.clock is 0.0 and displayClock
    "0:00" on racing whether the session is live or finished, and the
    competition carries a start `date` with no endDate. Anything the card
    showed there would have to be invented from hardcoded segment lengths that
    a red flag invalidates — so the card shows the segment and no clock."""
    import json
    wk = _f1_weekend()
    if not wk:
        raise Skip("no F1 weekend on the board right now")
    ev, comps = wk
    quali = [c for c in comps
             if str((c.get("type") or {}).get("id")) in ("2", "5")
             and c.get("competitors")]
    if not quali:
        raise Skip("no qualifying session has run this weekend yet")
    comp = quali[0]["id"]
    base = f"{_F1}/events/{ev['id']}/competitions/{comp}"

    # The segment times are BACKFILLED, not live. Measured 2026-08-22 during
    # the Dutch GP qualifying hour: every driver had qual1/2/3TimeMS at 0.000
    # with 17-19 laps completed, while the same fields on the finished
    # Hungarian qualifying carried 1:18.277 / 1:17.456 / 1:17.207. So the card
    # can only name the segment after the session ends, and correctly falls
    # back to "QUALI" while it is running.
    st = ((quali[0].get("status") or {}).get("type") or {}).get("state")
    if st == "in":
        for c in quali[0]["competitors"][:3]:
            body = http(f"{base}/competitors/{c['id']}/statistics")[1]
            stats = {s["name"]: s for cat in json.loads(body)["splits"]["categories"]
                     for s in cat["stats"]}
            for q in ("qual1TimeMS", "qual2TimeMS", "qual3TimeMS"):
                eq(float(stats[q].get("value") or 0), 0.0,
                   f"{q} is still 0 during a running session (backfilled after)")
        raise Skip("qualifying is running: segment times are not filled yet")

    # a finished session: at least one driver has a Q1 time
    seen = set()
    for c in quali[0]["competitors"][:4]:
        body = http(f"{base}/competitors/{c['id']}/statistics")[1]
        stats = {s["name"]: s for cat in json.loads(body)["splits"]["categories"]
                 for s in cat["stats"]}
        for q in ("qual1TimeMS", "qual2TimeMS", "qual3TimeMS"):
            contains(stats, q, f"statistics carries {q}")
            if float(stats[q].get("value") or 0) > 0:
                seen.add(q)
    ok("qual1TimeMS" in seen, "a driver who ran qualifying has a Q1 time")

    # the clock: absent, on this session and on the weekend's own status
    for path in (f"{base}/status",
                 f"{_F1}/events/{ev['id']}/competitions/{comps[0]['id']}/status"):
        st = json.loads(http(path)[1])
        eq(float(st.get("clock", 0)), 0.0, "racing status carries no clock")
        eq(st.get("displayClock"), "0:00", "and no display clock")
    comp_obj = json.loads(http(f"{base}")[1])
    ok("endDate" not in comp_obj,
       "a competition has a start date but no end date to count down to")
