# tests/ — sanity suite

```
python tests/run.py                 # everything
python tests/run.py --only espn     # ESPN contract only — no device needed
python tests/run.py --host 192.168.1.50 -v
python tests/run.py --list
```

No dependencies beyond the standard library (`numpy` is used only by the
panel group, and those tests skip without it). No pytest — `harness.py` is the
whole runner, because a test framework would be the only one in the repo.

Nothing is mocked. A green run means the real ESPN API and the real firmware
behaved, which is the only claim worth making about a system whose failures
have all been at the boundaries.

## Groups

| Group | Needs | Covers |
|---|---|---|
| `espn` | internet | the assumptions `espn_api.h` is built on |
| `parsers` | device | the provider's pure functions, via `GET /api/devtest` |
| `device` | device | the HTTP API every front end depends on |
| `panel` | device + numpy | what the LEDs are actually showing |

### `espn` — run this first when scores break

ESPN is undocumented and can change without notice. Each test pins a decision
made in the firmware, so a failure tells you *which* assumption died and
separates "ESPN changed" from "we broke it":

- **plain HTTP is served** — the whole feature fits in internal DRAM only
  because there is no TLS session (~32 KB against a ~39 KB budget). If this
  fails the feature needs redesigning, not patching.
- **user agent allowlist** — counter-intuitive and expensive to rediscover:
  `ESP32HTTPClient`, `curl/…` and `python-requests` are accepted;
  `Mozilla/5.0`, `Wget/1.21` and any product name get 403.
- **firmware user agent matches** — reads `config.h` so the suite cannot pass
  while testing a UA the firmware no longer sends.
- **transfer encodings** — `/teams` sends Content-Length, the cricket header
  and the core live endpoints send chunked. `EspnBufferedStream` handles both;
  this notices a change rather than guarding against breakage.
- **scoreboard is too big to poll** — the justification for the three-cadence
  design (NFL measured 281 KB).
- **team endpoint yields nextEvent** — discovery depends entirely on this shape.
- **core live endpoints are tiny** — the ~830 B that makes a 15 s tick defensible.
- **cricket needs its own path** — its `/teams` 404s, its series ids are
  numeric and change per tour, and its scores are strings.
- **search is CORS-open, `/teams` is not** — the dashboard's team picker runs
  in the browser and can only do so because the search endpoint grants
  `Access-Control-Allow-Origin: *`. Losing that silently drops the picker back
  to the six hardcoded catalogues, so this failing is the only warning there
  will be. The paired test asserts `/teams` still lacks CORS, which documents
  why the picker searches instead of listing.
- **wrong-league lookup fails silently** — why a favourite stores its own
  league. Real Madrid under `eng.1` answers 200 with an empty `nextEvent[]`.
- **the browser host accepts browser user agents** — `site.api` 403s any
  browser UA and a 403 page has no CORS header, so pointing a dashboard fetch
  at the wrong host looks exactly like a CORS bug. `site.web.api` is the
  browser-facing twin; the frame keeps `site.api`.
- **league scan primitives are cheap** — the ~1 KB `events?dates` and ~350 B
  `status` that make following a whole league affordable at all.
- **a competitor id is a team id** — why pinning needs no discovery. If these
  ever diverge, every pinned match silently fetches the wrong side's score.
- **fixture dates are not the viewer's dates** — ESPN files a fixture under its
  own local date, so both the frame and the dashboard ask for a two-day UTC
  span. A single local day silently returns zero.
- **situation carries possession cheaply** — the NFL chevron's only source, and
  the check that soccer's equivalent is still an empty stub (which is why only
  NFL pays for the call).

### `parsers` — the logic that had real bugs

Exercised through `GET /api/devtest` so assertions land on the **compiled
firmware**, not a host-side copy that would silently drift from it. That
endpoint is dev-only and compiles out with `DEV_ENDPOINTS 0`.

Every cricket case is a real ESPN string, and two of them were bugs found on
the panel: `"426"` (a completed innings) rendered as "has not batted", and the
batting side's overs were wiped by the fielding side sharing one buffer.

### `panel` — pixels, not intentions

Uses the same capture shadow as `dev/panelshot.py`. Checks the clock block
(rows 0–18) is drawn and stays inviolate — the ambient balloon used to rise
over the AM/PM text — and that all six sports' cards actually paint, which
catches a `drawBody` that throws or draws nothing without needing a live match.

Two tests guard the layout rather than any one sport: `every sport lands on the
shared grid` checks the gap under the clock, the divider row and the bottom
margin across all six, and `match comet matches the clock comet` checks the
card's progress meter still uses the clock's own comet colour — the tie between
the two halves of the panel that the whole design rests on.

## Notes

- Device tests **skip**, not fail, when no frame answers at `--host`.
- Tests that need something live (a fixture today, a cricket match in progress)
  skip with a reason rather than failing on a quiet afternoon.
- `test_score_card_renders_for_every_sport` drives the panel through all six
  sports and leaves it back on the clock.
