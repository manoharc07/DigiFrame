# tests/ — sanity suite

```
python tests/run.py                 # everything
python tests/run.py --only espn     # ESPN contract only — no device needed
python tests/run.py --only update   # GitHub update contract — no device needed
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
| `update` | internet | the GitHub CORS policy the one-click update rests on |
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

- **cricket ball-by-ball paging and sentinel** — `limit` pages from the start of
  the innings, and every page carries a padding entry whose valid-looking
  playType would freeze the pill strip forever.
- **cricket ball ids reset at the innings break** — ids are monotonic
  only WITHIN an innings; a second innings restarts near zero, which is why
  the strip is rebuilt from the tail page instead of appended to by id.
- **cricket ball page is expensive enough to count** — a six-ball page is
  ~16 KB, which is why the reach-back to the previous page is conditional.
- **cricket overs need linescores after the first innings** — from then on
  both sides carry a `(… ov)`, so only `isBatting` on the `isCurrent`
  innings says whose overs the card should print.
- **match-state words are prose** — why the suspension keywords fold case.

### `update` — one missing header holds the whole design up

The update feature is split across two machines, and the split rests entirely
on GitHub's CORS policy: the **check** runs in the browser because
`api.github.com` sends `Access-Control-Allow-Origin: *`, and the **download**
runs on the frame because the release asset does not. Neither is a documented
promise, and either flipping fails silently — a check that reports a bare CORS
error with no cause visible, or a device paying for a TLS session it no longer
needs.

- **release api is readable from a browser** — the reason `updateCheck()` is
  not on the ESP32. If this fails, the check has to move onto the device and
  buy a TLS session, a filtered JSON parse and a periodic timer for an answer
  only worth having while somebody is looking at the dashboard.
- **release asset is NOT readable from a browser** — the load-bearing
  negative, and the entire justification for `updater.h`. The day this starts
  sending the header, the frame can stop doing TLS.
- **asset url matches the firmware allowlist** — `updUrlAllowed()` pins the
  frame to its own repo's releases, so the shape of `browser_download_url` is
  now part of the contract. If GitHub changes it, the frame refuses its own
  update.
- **asset redirect stays https** — why `HTTPC_STRICT_FOLLOW_REDIRECTS`, and
  why this one cannot be done in the clear like the ESPN provider.
- **asset is an app image not the merged one** — checks the
  `esp_app_desc_t` magic at 0x20 on *both* release binaries: the app-only
  image has it, the full-flash image must not, because the asset-name suffix
  is the only thing keeping the dashboard off the one that would wipe
  LittleFS.
- **firmware version is a plain semver** — the dashboard compares field by
  field as integers, so anything the release workflow's stamp would not
  produce compares as 0 and can hide a real update.
- the last two need a device: that `/api/config` serves the three values the
  page cannot check without, and that `/api/update` refuses a URL pointing
  anywhere but the configured repo.

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
