# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

DigiFrame is a source-available (noncommercial — PolyForm Noncommercial 1.0.0, see `LICENSE.md`) Arduino/ESP32-S3 firmware that drives a 64x64 HUB75 LED matrix as a **smart clock**: NTP clock, Open-Meteo weather, a neutral living ambient scene, GIF playback from LittleFS, scrolling messages, typed **special days** (date + type + message → themed celebration; merges the old "party mode"), a **live sports-score widget** (favourite teams auto-switch the lower two-thirds of the clock face into a score card, with per-sport layouts and event animations), a Telegram bot, a local web dashboard with **one-click firmware updates from GitHub releases**, optional **Home Assistant integration over MQTT**, and a WiFi setup hotspot with an on-panel QR code. Being repositioned from a personal "gift frame" — keep it generic, no personal/gift references.

## Repository layout

- `firmware/DigiFrame/` — the Arduino sketch: `DigiFrame.ino` + all `.h` tabs + `partitions.csv`. The folder must stay named `DigiFrame` (Arduino requires the sketch dir name to match the `.ino`). Build/flash target this path.
- `firmware/gifs/` + `firmware/tools/make_default_gifs.ps1` — default GIF-pack source + its generator (writes `firmware/DigiFrame/default_gifs.h`).
- `website/` — cloud dashboard PWA. **Currently non-functional**: it spoke to the frame over Web Bluetooth, which the firmware no longer implements. It needs rewriting against an outbound relay (see "Cloud dashboard" below) before it works again.
- `dev/` — **UI development tooling** (host-side, never shipped). `panelshot.py` photographs the LED panel over the LAN; `dashshot.py` screenshots and audits the web dashboard in headless Chromium. See `dev/README.md`, and "Seeing the UI" below.
- `tests/` — **sanity suite** (host-side, zero dependencies, no pytest). `python tests/run.py`; `--only espn` needs no device and pins every assumption `espn_api.h` makes about the undocumented ESPN API. See `tests/README.md`.
- Docs at root: `README.md`, `CLAUDE.md`, `FLASHING.md`.

## Build / Flash

Arduino IDE **or** arduino-cli (installed, reuses `%LOCALAPPDATA%\Arduino15`):

```
arduino-cli compile --fqbn esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=custom,CDCOnBoot=cdc --output-dir build firmware/DigiFrame
```

- **Board:** ESP32S3 Dev Module — Flash 16MB, PSRAM "OPI PSRAM", partition scheme **Custom** (`PartitionScheme=custom`), which uses the sketch's `partitions.csv` — a 16 MB layout with **4 MB OTA app slots** (app0/app1) + ~7.9 MB `ffat` data. (The old `fatflash` scheme's 2 MB app got tight; `partitions.csv` doubles it for future features.) Arduino IDE reports "% of 16 MB" for Custom, but the real ceiling is the 4 MB app slot. Growing the app slots moved the data partition, so the first flash of this layout wipes LittleFS once (GIFs/config re-seed on next boot).
- **Libraries** (Library Manager): `ESP32 HUB75 LED MATRIX PANEL DMA Display` (mrfaptastic), `Adafruit GFX Library`, `AnimatedGIF` (Larry Bank), `UniversalTelegramBot` (Brian Lough), `ArduinoJson`, `PubSubClient` (Nick O'Leary — the Home Assistant MQTT client). QR codes use the `espressif__qrcode` component bundled with the ESP32 core (`#include <qrcode.h>` resolves to it — do NOT install the ricmoo "QRCode" library, it gets shadowed).
- **Flashing:** see FLASHING.md. `build/DigiFrame_flash_at_0x0.bin` (compact, flash at 0x0) preserves LittleFS; `build/DigiFrame.ino.merged.bin` (16MB padded) wipes it. App-only reflash lives at 0x10000. Typical:
  ```
  esptool --chip esp32s3 --port COM5 write-flash 0x0 build/DigiFrame_flash_at_0x0.bin        # keep GIFs/config
  esptool --chip esp32s3 --port COM5 write-flash 0x10000 build/DigiFrame.ino.bin              # app only, fastest
  esptool --chip esp32s3 --port COM5 write-flash 0x0 build/DigiFrame.ino.merged.bin           # factory reset (wipes LittleFS)
  ```
- **Default GIF pack:** `firmware/gifs/*.gif` are embedded in the app image via the auto-generated `firmware/DigiFrame/default_gifs.h` (regenerate with `firmware/tools/make_default_gifs.ps1` after changing `firmware/gifs/`) and copied to LittleFS **once** on first boot (`seedDefaultGifs()`, marker `/.gifs_seeded`) — after that they are ordinary files the user can delete from the dashboard, and deletions stick. Additional GIFs are uploaded via the web dashboard (`http://digiframe.local`). Telegram GIF upload was removed intentionally.

There are no linters or CI. Verification is a clean arduino-cli compile plus `python tests/run.py` (see `tests/README.md`) — and, for anything that draws, a capture from `dev/panelshot.py`.

## Runtime configuration

Compile-time **defaults** live in `config.h` (WiFi SSID/pass, `BOT_TOKEN`, `ALLOWED_CHAT_ID`, timezone, location, brightness, hotspot `AP_SSID`/`AP_PASS`, `CLOUD_SITE_URL` for the setup QR, and `MQTT_*` for Home Assistant — all placeholders, no personal data). At runtime they are overridden by `/config.json` on LittleFS (keys `ssid`, `pass`, `tgToken`, `tgChat`, `bright`, `charMin`, `lat`, `lon`, `tz`, `mqttEn`, `mqttHost`, `mqttPort`, `mqttUser`, `mqttPass`, `sportEn`, `sportSrc`, `sportHold`, `sportFx`, `sportOn`, `sportRot`), editable from the web dashboard and (for some) Telegram. Weather lat/lon live in fixed `char` buffers (`cfgLat`/`cfgLon`), not `String`, because core 1 writes them while `weatherTask` (core 0) reads. **Special days** persist separately in `/events.json` as `{d,t,m}` = date/type/message (type = `custom`|`birthday`); no default events are seeded. **Follows** for the live-score widget persist in `/teams.json` as `{i,n,a,s,e,l,k}` = id/name/abbr/sport-index/ESPN-team-id/ESPN-league/kind (`k`: 0 = team, 1 = whole league; absent reads as team, so older files load unchanged). No defaults. Ids are only unique *within* a sport ("eng" is both a cricket and a rugby side), so anything addressing a follow from outside uses `"sportKey/id"`. An empty `e` means catalogue-only (demo feed works, the live provider skips it). `l` is not optional decoration: the ESPN team endpoint answers **200 with an empty `nextEvent[]`** for a team looked up under the wrong league, so a favourite stored without its own league is discovered against the sport module's default and silently never appears. **Favourite sports** are not a third kind — there are exactly `NUM_SPORTS` of them, so they are the `sportOn` bitmask in `/config.json` (all-ones default) alongside `sportRot`, the seconds each live match holds the panel before the next takes a turn (0 = no rotation). **Note:** any `BOT_TOKEN`/`WIFI_PASS`/`MQTT_PASS` you compile in are sensitive — the shipped defaults are placeholders, keep them that way in commits.

## Architecture

Single translation unit: `DigiFrame.ino` includes ordered `.h` files (order matters — later headers may call earlier ones; forward decls for `handleTelegram()`/`fetchWeather()` sit in `globals.h`). The actual include order in `DigiFrame.ino` is:

```
config → globals → gif_player → events_store → sports_core → weather → scene
       → score_gfx → score_fx → sports_registry → score_widget
       → scroll → party → updater → control → telegram → web_portal → mqtt_ha → qr_display → wifi_manager
```

Preserve this order when adding a new header — e.g. anything using the DMA panel or `logLine` must come after `globals.h`; anything driving `MODE_SETUP` must come after `qr_display.h`.

| File | Contents |
|---|---|
| `config.h` | user config + pin map (compile-time defaults) |
| `build_opt.h` | `-D` flags the ESP32 core passes to **every** translation unit, including the panel library's own `.cpp`s. Currently just `PIXEL_COLOR_DEPTH_BITS`, which **must** be kept equal to `PANEL_COLOR_DEPTH` in `config.h` — the library picks its CIE luminance LUT from this macro at compile time while the framebuffer depth is set at runtime, and a mismatch silently corrupts mid-tone colours. |
| `globals.h` | globals, runtime config strings, TgCmd queue, `logLine`, `tgTask`/`weatherTask`, colors |
| `gif_player.h` | GIF decode callbacks, `openGif`/`closeGif`, character pack, `seedDefaultGifs` |
| `default_gifs.h` | auto-generated embedded default GIF pack (do not edit — run `firmware/tools/make_default_gifs.ps1`) |
| `events_store.h` | `/events.json` special days + `/config.json` persisted config |
| `sports_core.h` | **live scores, data layer** (no drawing): `LiveMatch`/`Side`/`EventKind`/`SportModule`, `/teams.json` favourites store, the provider seam `sportsPoll()`, `sportsTask` (core 0), event diffing + the 4-slot priority event ring, and `sportsTick()` (core 1) which drives the `SUB_SCORE` sub-mode |
| `score_gfx.h` | live scores, shared drawing toolkit: card chrome, team bars, the auto-sizing score line, ticker, pills, bars, odometer, and the flash/wipe/ring/shards/scanline/spark building blocks |
| `score_fx.h` | live scores, the ten generic per-`EventKind` animations (burst, shockwave, digit flip, shatter, rising numerals, card flip, scanline, bar swap, wipe, laurel) |
| `sport_*.h` | **one file per sport** (`cricket`, `football`, `basketball`, `nfl`, `hockey`, `rugby`): team catalogue, native-event map, its own `drawBody` layout, optional bespoke `drawEvent` animations, and a demo `simulate()` feed |
| `espn_api.h` | **live scores, the ESPN provider** behind `sportSrc="http"`: the buffered + chunk-decoding stream, the three poll cadences, discovery, and the cricket score-string parser. Runs on core 0; posts `TGC_ESPN_CAT` so core 1 does the LittleFS write |
| `sports_registry.h` | the one table binding the sport modules — see "Adding a sport" below |
| `score_widget.h` | `drawScoreWidget()`, the dispatcher `renderClock()` calls: chrome → the sport's `drawBody` → at most one event animation (the sport's own if it has one, else `score_fx.h`) |
| `updater.h` | the **OTA install path**, shared by the manual `.bin` upload and the one-click update: the progress screen, the `netQuiesce` handshake, the app-image magic check, `panelTeardown()`, and `updateInstall()` which pulls a release asset from GitHub. The version *check* is not here — it runs in the browser (see "Auto-update" below) |
| `weather.h` | Open-Meteo fetch + weather icons |
| `scene.h` | clock face + ambient scene (sprites, `renderClock`) — the big one |
| `scroll.h` | scrolling text renderer |
| `party.h` | **celebration** (special-day) mode + `/test` mode: `startCelebration(type,message)`/`runCelebration()`; visual by type (`birthday`→cake+confetti, `custom`→fireworks) then a scrolling message banner |
| `control.h` | **shared control layer**: one `ctl*` function per action (msg, brightness, play/del/upload GIF, interval, celebrate/stop, wifi, loc, tg, tgtest, add/del/list special days, MQTT config, status/list/logs JSON). HTTP handlers, Telegram and MQTT all call these — they run on core 1. |
| `telegram.h` | bot commands, reply keyboard, inline keyboards, callback queries |
| `web_portal.h` | dashboard HTML + `/api/*` handlers (thin wrappers over `control.h`) + OTA + captive-portal redirect (endpoints: `GET /`, `GET /api/logs`, `GET /api/list`, `GET /api/config`, `POST /api/msg`, `/api/brightness`, `/api/play`, `/api/del`, `/api/interval`, `/api/celebrate`, `/api/stop`, `/api/events`, `/api/eventdel`, `/api/upload`, `/api/tgtest`, `/api/wifi`, `/api/tgconfig`, `/api/loc`, `/api/mqtt`, `/api/ota`, `/api/update`, and for live scores `GET /api/catalogue`, `GET|POST /api/teams`, `/api/teamdel`, `/api/espnfollow`, `/api/espnteams`, `/api/espnrefresh`, `/api/leaguefollow`, `/api/sportsel`, `/api/pin`, `/api/unpin`, `/api/sports`, `/api/scorepreview`, `/api/scoreevent`). The team picker runs **in the browser** — see "Picking teams" below |
| `mqtt_ha.h` | optional **Home Assistant integration over MQTT** (`PubSubClient`, `mqttTask` on core 0): MQTT discovery for brightness/message/celebrate/stop + temperature/mode sensors; commands `postTgCmd()` to core 1. Off unless enabled + a broker host is set. |
| `qr_display.h` | `renderSetupQR()` — QR on the panel in `MODE_SETUP` (encodes `CLOUD_SITE_URL/#d=<bleName>`) |
| `wifi_manager.h` | `wifiConnect`, `startPortal`/`stopPortal`, `wifiManagerTick` |

Everything runs on a dual-core FreeRTOS setup. The critical structural fact is the **core split and command queue** — get this wrong and you will race LittleFS against the DMA renderer.

- **Core 1 (`loop()`):** render loop. Owns the HUB75 DMA panel, AnimatedGIF decoder, mode state (`MODE_CLOCK/MSG/GIF/CELEBRATE/TEST/SETUP`), `WebServer` (port 80), DNSServer processing, and `wifiManagerTick()`.
- **Core 0 tasks:** `tgTask` (Telegram polling; also applies dashboard token changes via the `tgTokenDirty` flag — only this task touches the bot client), `weatherTask`, `mqttTask` (Home Assistant — only active when MQTT is enabled with a broker host), and `sportsTask` (live scores; 8192 stack because a real provider's JSON is far larger than Open-Meteo's). Score data is *strings*, so unlike `wTemp`/`wCode` it cannot be handed over lock-free: `sportsTask` fills `scoreBack[]` under `sportsMutex` and `sportsTick()` (core 1) copies into `scoreFront[]`, which the renderer then reads unlocked. Every string in `LiveMatch` is a fixed `char[]` for the same reason `cfgLat`/`cfgLon` are.
- **Cross-core handoff:** `tgTask` and **`mqttTask`** (both core 0) parse input and call `postTgCmd(...)` (single `TgRequest` slot guarded by `tgReqMutex`); `loop()` drains it and calls the `control.h` `ctl*` functions on core 1 (`openGif`/mode/LittleFS/`saveConfig`). New actions from either task must follow this pattern — never touch LittleFS/DMA from core 0. `TgRequest` carries a second string (wifi pass / lon / chat) and a PSRAM `buf` for GIF uploads (freed by core 1 after `TGC_GIF_COMMIT`).
- **One implementation per action:** the HTTP dashboard (`web_portal.h`, core 1) calls `ctl*` directly; Telegram and MQTT (core 0) marshal to them via the queue. Every front-end therefore behaves identically — add new config actions in `control.h`, then wire a thin handler per front-end.
- **Web → WiFi handoff:** `/api/wifi` → `ctlSetWifi` sets `wifiRetryNow`; `wifiManagerTick()` (core 1) performs the actual reconnect.
- **OTA** (`updater.h`): flashes an app image (`DigiFrame.ino.bin`) into the spare OTA slot via `Update.h` (the custom partition table has `app0`/`app1`, now 4 MB each), then reboots. Two front doors end in the same quiesce → validate → write: `/api/ota` (a `.bin` the user picked, streamed in by the browser) and `/api/update` (a release URL the dashboard found, downloaded by the frame). Both suspend the core-0 tasks for the duration and reject non-app images by checking the `esp_app_desc_t` magic `0xABCD5432` at offset 0x20 of the first chunk. Like the rest of the dashboard they are unauthenticated LAN-only. After an OTA the device may boot from `app1` — a serial app-only flash at 0x10000 then needs otadata cleared (flash the `_0x0` image, or `esptool erase-region 0xe000 0x2000`).

### Auto-update: the check is in the browser, the download is not

Same browser/frame split as the pickers, forced by one missing header. Both
halves are pinned by `tests/test_update.py`.

- **`api.github.com` sends `Access-Control-Allow-Origin: *`**, so the dashboard
  runs the check itself on load: fetch `/releases/latest`, compare `tag_name`
  against the `fw` field of `/api/config`, show a banner. That reply is 6.6 KB
  of JSON — nothing in a browser, and only worth knowing while somebody is
  looking at the page. On the device it would have cost a TLS session, a
  filtered JSON parse and a periodic timer for the same answer. **There is no
  version check in the firmware at all**, and adding one is a regression, not
  a feature.
- **The release asset does not.** `github.com` 302s it to
  `release-assets.githubusercontent.com`, whose response carries no CORS
  header, so `fetch()` can never read those 1.6 MB to hand them to `/api/ota`.
  That is the entire reason `updateInstall()` exists and pays for a TLS
  session. The asset is https-only too (the redirect's SAS signature carries
  `spr=https`), so unlike the ESPN provider it cannot be done in the clear.
- **The page sends a URL; the frame decides whether to trust it.**
  `updUrlAllowed()` accepts only
  `https://github.com/<UPDATE_REPO>/releases/download/…`, so an
  unauthenticated LAN endpoint cannot be turned into "fetch and run arbitrary
  bytes from anywhere". The app-image magic catches the rest.
- **The dashboard picks the app-only asset by `UPDATE_ASSET_SUFFIX`.** The
  other release binary is the merged full-flash image, which starts with the
  bootloader — it fails the magic check, and would wipe LittleFS if it didn't.
- **`/api/update` answers before it installs.** `updateInstall()` runs on core
  1 from `loop()`, which is also what services the socket; doing it inside the
  handler means the browser reports a timeout on an update that is working
  fine. The handler queues `updInstallNow` and returns; the panel is the
  progress bar, and the page polls `/api/config` until the frame comes back
  with a different `fw`.
- **The download deletes the panel, and that is not optional.** Measured on
  the device: with the framebuffer up there is ~32 KB of internal DRAM free
  and the largest contiguous block is **11 KB**. mbedTLS wants a 16 KB input
  *and* a 16 KB output buffer, so the handshake fails with `SSL - Memory
  allocation failed` however the rest of the firmware is arranged. Quiescing
  the other network tasks recovers **2 KB** — the Telegram bot connects and
  closes per poll, so between polls there is nothing to free. The occupant is
  the 64 KB double-buffered framebuffer (`MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA`,
  because the DMA engine cannot read PSRAM), and `panelTeardown()` gives it
  back. This is the same wall that keeps the ESPN provider on plain HTTP.
- **After the teardown every exit is a reboot.** `stopDMAoutput()` is
  documented as black-until-reboot, so a failed download cannot put the panel
  back and calls `ESP.restart()` instead. The panel is therefore dark for the
  ~25 s download with no progress indicator, and the log ring — which is RAM —
  does not survive: the failure reason goes to `/update_err.txt` first and
  `loadUpdateErr()` reports it once at the next boot. The **upload** path keeps
  the panel and its KB progress bar, because it needs no TLS.
- **`netQuiesce` is a handshake, not a `stop()` call.** Each network task drops
  its session at the top of its own loop and sets its idle flag; the installer
  waits for the acks before suspending. Freeing another task's TLS context
  from core 1 would be a use-after-free the moment it resumed. It buys little
  memory, but a task suspended mid-handshake was a real hazard. Allow it a
  generous timeout — weatherTask ticks every 5 s, and a short wait silently
  suspends a task that has not parked, which is what made the first
  measurement of this look conclusive when it was not.
- **The version is stamped from the git tag** by `.github/workflows/release.yml`
  before it compiles. `FW_VERSION` in `config.h` is only what a local build
  calls itself — a hand-maintained one would eventually be forgotten, and a
  build reporting a stale version either hides a real update or offers one it
  already has.

- **Logging:** `logLine()` → mutex-guarded ring buffer (`logBuf`, 40 lines) shown on the dashboard, mirrored to Serial.

### WiFi / setup-portal flow

Boot: `wifiConnect(20s)` → on failure `startPortal()` (AP `DigiFrame`/`digiframe123` + captive DNS + `MODE_SETUP` QR). While the portal is up, STA retries stored creds every 30 s; any successful connect (old router back, or new creds saved over HTTP) triggers `stopPortal()`. At runtime, a sustained outage of `WIFI_FAIL_PORTAL_MIN` (5 min) reopens the portal.

The `MODE_SETUP` QR encodes a `WIFI:T:WPA;S:<AP_SSID>;P:<AP_PASS>;;` payload, so scanning it joins the setup hotspot directly (native in both Android and iOS cameras). Once a station connects, the QR switches to `http://192.168.4.1` so the on-device dashboard opens even if the captive popup didn't fire. Provisioning is HTTP-only.

### Cloud dashboard (why there is no direct browser → frame path)

An `https://` page **cannot** call this device's `http://` LAN API — browsers block active mixed content, and no header on the frame overrides it. Putting TLS on the device doesn't help either: a self-signed cert fails `fetch()` outright, and a real one would mean shipping a private key in a source-available firmware. The frame also has no IP at all until it's provisioned, so no LAN protocol can bootstrap it.

Bluetooth used to bridge that gap and was removed — it cost ~67 KB of internal DRAM, which is the binding constraint on this board (see the `PANEL_COLOR_DEPTH` note in `config.h`). Today: **setup and LAN control** go through the on-device dashboard (hotspot → `http://192.168.4.1`, or `http://digiframe.local`), and **control from anywhere** is the Telegram bot.

The intended replacement is an **outbound relay** — the frame connects out to a broker (WSS or MQTT over TLS), and the cloud page talks to that broker. Outbound solves both mixed content and NAT. It should be wired as another thin front-end over `control.h`, exactly as the existing ones are. `website/` still contains the old Web Bluetooth client and does not work until this lands.

### Mode invariants worth preserving

- **Double-buffered DMA** (`cfg.double_buff = true`). Draw a full frame, then `dma->flipDMABuffer()` — do not draw incrementally on the visible buffer or you will tear. The setup QR is static: it paints **both** buffers once and redraws only when its text changes. On the ESP32-S3 `flipDMABuffer()` is **non-blocking** — it only re-points the DMA descriptor chain, and the controller keeps scanning the buffer just handed over until the end of the current pass. Code that flips twice back to back must wait `panelScanPeriodMs()` (globals.h) in between; the normal one-flip-per-frame path is already spaced by the frame clock.
- **One frame clock, no private gates.** `loop()` owns the only render cadence: it advances a phase-correcting deadline in exact `FRAME_MS` steps (`RENDER_FPS` in config.h), sets `frameDue` for exactly one pass per frame, and increments `frameNo`. Renderers test `frameDue` and read `frameNo`; they must **not** reintroduce their own `millis() - last > N` gate or call `frameNo++`. Independent gates drift out of phase and each one resets to *now* after an overrun, so a single slow pass permanently shifts that renderer's cadence — accumulated phase error is what the stutter was. GIF playback is the one exception: it paces on the GIF's own inter-frame delay via `playGifFrameIfDue()` (gif_player.h), which is deliberately non-blocking, unlike `playFrame(bSync=true)` which `delay()`s inside the decoder and stalls the whole core.
- **Core 1's loop() is its scheduler.** The Arduino core calls `loop()` back to back with no yield, so anything unconditional there runs tens of thousands of times a second. Network servicing (`web.handleClient()` + `wifiManagerTick()`) is throttled to `NET_SERVICE_MS`, `wifiManagerTick()`'s housekeeping to `WIFI_TICK_MS` (its captive-portal DNS still runs at the full rate), and the pass ends with `vTaskDelay(1)` when no frame is due. `Serial.setTxTimeoutMs(SERIAL_TX_TIMEOUT_MS)` is applied after `setup()` so a `logLine()` cannot block core 1 on the USB-CDC tx lock.
- **Night mode** is applied every second in `loop()` and is intentionally **skipped during `MODE_CELEBRATE` and `MODE_SETUP`** (QR must stay scannable).
- **Auto-celebration trigger** compares `todayMMDD()` to `lastCelebDate` and, on a match, runs `startCelebration(day.type, day.message)`; skipped while `portalActive`. Manual `/celebrate` (and the HA celebrate) deliberately does **not** set `lastCelebDate` — preserve this distinction so a manual preview isn't force-ended at date rollover.
- **GIF playback:** `gifIsUserPlay` distinguishes user plays (loop forever) from ambient cameos (play once). On decode error, close and fall back to `MODE_CLOCK`.
- **`SUB_SCORE` is a sub-mode of `MODE_CLOCK`, not a mode.** `renderClock()` always draws the clock block in rows 0-18; only rows 19-63 branch — ambient scene + footer, or the live-score card. Keep it that way: the clock must never disappear, and every other mode (`MSG`/`GIF`/`CELEBRATE`/`TEST`/`SETUP`) keeps working untouched. `sportsTick()` owns the switch and returns early during `MODE_TEST` so `/test` can drive the card itself. On leaving the card it sets `sceneNeedsReset` so the ambient scene restarts instead of resuming a sprite mid-walk.

### Seeing the UI (dev tooling)

Panel work is otherwise unreviewable without a camera pointed at the frame. `dev/panelshot.py` fixes that: the firmware mirrors every draw call into an RGB565 shadow (`capture.h`) and serves the presented frame from `GET /api/frame`, so the tool produces **true screenshots, not a simulation**. `POST /api/dev` drives the panel to the screen worth photographing — `hour`/`wcode` (the ambient scene branches on both, so without them only the current time-of-day and sky are reachable), `sport`, `event`, `celebrate`, `test`. Both endpoints compile out with `DEV_ENDPOINTS 0` in `config.h`.

Design points worth preserving:

- **The tee, not a readback.** The HUB75 library stores bitplanes with the CIE curve already applied, so reading a pixel back is gamma-mangled and depth-truncated. `CapturePanel` overrides the five virtual GFX primitives (`drawPixel`, `fillScreen`, `fillRect`, `drawFastHLine`, `drawFastVLine`) that every draw in the sketch funnels through, mirroring the *input*. No call site changes; the sketch must therefore keep using the RGB565 forms, never the RGB888/CRGB overloads, or those draws go uncaptured.
- **`panelPresent()`, not `dma->flipDMABuffer()`.** `flipDMABuffer()` is not virtual, so the shadow can only stay in step through that one choke point. Use it everywhere.
- **Costs nothing when off.** Shadows are PSRAM (internal DRAM is the binding resource), allocated only while armed; arming is lazy on first request and auto-disarms after 30 s. Disarmed, each override is one predicted branch.
- **The handler must never block waiting for a frame.** It runs inside `loop()` on core 1, which is also the only thing that draws — waiting would deadlock the renderer it waits on. It answers 503 and the client retries. `MODE_SETUP` is static, so arming clears `qrLastText` to force one repaint.
- **Forcing a score card needs `sportsFreeze`.** `sportsDemoForce()` writes `scoreFront` directly, but `sportsTick()` publishes `scoreBack` over it a second later — which is why `/test` works only by returning early from `sportsTick()`. `/api/dev?sport=` sets `sportsFreeze`; `ctlStop()` clears it along with the hour/weather overrides.
- `/api/dev` is deliberately **not** routed through `control.h`. That convention exists so every front end behaves identically, which is exactly what should not happen for debug affordances with no Telegram or HA equivalent.

### The card's layout grid and its signature

Every sport body lands on constants in `score_gfx.h`, not on hardcoded rows.
Before that, each `sport_*.h` carried its own `y` values and the card ended up
**welded to the clock** — one blank row above the divider and one below, which
is invisible at this pitch — while **eight rows at the bottom held nothing but
the edge bars**. Three of the six sports had no bottom element at all, and of
the three that did, two were fed by data the provider never sends.

```
19  blank        the gap. The clock's own tail ends at row 17.
20  SW_RULE      divider hairline, in the sport's accent
23  SW_SCORE     score pair, size 2
40  SW_ABBR      team abbreviations, size 1
49  SW_BAND      period | detail, size 1
58  SW_METER     the match comet
61+ margin       only the 3px edge bars reach here
```

**The signature is the second comet.** The clock draws a hairline across row 17
that grows with the seconds and carries a pulsing head (`scene.h`); `gfxMeter()`
is its twin at row 58, same `C_TRAIL` colour and same blinking head, tracking
the match instead of the minute. It is fed by `SportModule.progress()` — data,
not rendering code — and returns **negative for "cannot say"**, which draws
nothing rather than lying (a Test match has no over limit to measure against).
`test_match_comet_matches_the_clock_comet` pins the two together so retuning
one without the other is caught.

**Palette tokens come from the clock, not from each sport file.** `C_BAND` is
the clock's own `C_DATE` lavender; it replaced four near-identical pale tints
(220,220,150 / 230,200,150 / 200,225,255 / 200,235,200) that each sport had
invented and which read as one colour anyway.

**Five of six bodies are `gfxBodyStd()`.** Only cricket differs, because its
score is a string and both innings matter at once — it spends on two score rows
what the others spend on abbreviations, so it keeps its own tighter row set
(22 / 31 / pills at 40 / `SW_BAND`). NFL adds only the possession chevron.

**Per-sport poll cadence.** `SportModule.liveMs` — basketball 10 s, NFL and
hockey 15 s, football, rugby and cricket 20 s. A basketball score moves every
few seconds and a Test match does not; one shared `ESPN_LIVE_MS` either wasted
requests or showed a stale score. `espnLiveIntervalMs()` takes the fastest
among the matches currently live, because the panel rotates between them.

**Chrome must be fed or deleted.** Three markers looked like data and were not:
the possession chevron and the team-bar "breathe" (`Side.active` was set only in
the provider's cricket branch, so every other sport drew them on a fixed side)
and NFL's field-position bar (`sportData[4]`, written only by the demo
simulator, so its marker sat pinned at yard 0 on every real match). The chevron
is now fed by the 615-byte `/situation` endpoint; the field bar is gone. When
adding a per-sport indicator, check it has a real source first.

### Score-card design rules (learned from `dev/panelshot.py` captures)

The card is read from across a room, in about a second, on a panel whose only
font is the 5×7 built-in (size 1 = 10 chars/row, size 2 = 5). Three rules hold
the whole thing together — breaking any of them produced a defect visible in
every sport:

- **An event never composites over the body.** Every takeover effect opens with `fxStage()` (score_fx.h), which *clears* the card to a dark tint of the event colour. It replaced `gfxFlash()`, which covered the card at full brightness — a strobe that also hid the score at the moment you looked up. Anything drawn afterwards lands on empty pixels; that alone fixed `NEW LEAD` printing over `22' 43%`, `WON` through the score digits, and `HAT-TRICK` over the stats row, across all six sports.
- **Text goes through `gfxPunch()`**, which owns and clears its band and auto-drops to size 1 when a word will not fit. Size 2 is 12px/char, so only five fit — a hard-coded `x` with size 2 is how cricket's `100` used to render as `10`. Never position a punch word by hand, and never ride a moving element with text (the period label used to trail the wipe off the right edge and render `END OF Q` as `ND OF Q`).
- **Show less.** The ticker was removed from every body: it restated the score already on screen one row below it, in smaller moving type. Broadcast furniture (shots on goal, possession %, down-and-distance) is texture at this pitch, not information. **The demo simulators must produce the same shape as the provider**, or Preview demonstrates a card that cannot occur: football's sim used to put possession % in `detail` and NFL's put down-and-distance there, while the ESPN path sends the game clock — which also fed both `progress()` functions a number that was not a minute.
- **`gfxBand`'s right field yields on collision, with 3px of clearance.** Both band fields are variable-width, and overlapped 5×7 glyphs read as garbage — hockey's `"P2 18'"` printed over `"SOG 21"` rendered as `$8G`, hybrid glyphs no firmware string contained (proven by a pixel-exact union match against a capture). Dropping the lesser field beats truncating it: a partial stat misleads, a missing one is just quiet. The clearance is **3px, not zero**: fields that merely abut still read as one word — rugby's `"H1 22'"` beside `"TRY"` rendered as `22'TRY`, and cricket's overs beside its run rate as `7.3RR12.1`. Both cleared the old zero-gap test by exactly one pixel, which made a layout bug look like a font bug.
- **Team colours are floored on entry** via `teamInk()` (sports_core.h) at the two points data enters `LiveMatch` — the All Blacks' colour is literally `RGB565(20,20,20)`, which made NZL's score, abbreviation and every event invisible on the black card. Renderers never handle this themselves.
- **The clock block (rows 0–18) is inviolate from below too:** ambient-scene sprites must clip at row 19-20 (the balloon used to rise over the AM/PM text). `drawWeatherBg()` is the one sanctioned full-screen painter, and only because it draws *before* the clock text.
- **While `sportsFreeze` is set, `sportsTask` skips polling entirely** — publishing was already blocked, but `fetchScores()` still pushed the demo sim's events into the 4-slot ring, where they raced with (and could evict) the event being tested via `/api/dev`.

`sportFx` defaults to **1 = major only**; `evIsMajor()` in sports_core.h is the set. It is deliberately *not* an `evPriority()` threshold — priority ranks which event preempts another, a different question from whether an event earns the panel. A cricket wicket is `EV_TURNOVER` and a century is `EV_MILESTONE`, both headline; a VAR check, yellow card, lead change and end-of-quarter are chatter on a clock.

### The browser/frame split (pickers, "live now", pinning)

Three dashboard features deliberately break the "one implementation per action
in `control.h`" rule on the *read* side and run in the **browser**: the team
picker, the league picker, and the "live now" list on the Now tab. The reasons
are specific and worth not re-litigating:

- **Use `site.web.api.espn.com` for every browser call, never `site.api`.**
  They carry the same data but not the same door policy: `site.api` 403s any
  browser-shaped User-Agent at the Akamai edge — the same allowlist that makes
  the *firmware* send `ESP32HTTPClient` — and a 403 page carries no CORS
  header, so the browser reports a CORS error and the real cause is invisible.
  A page cannot override its User-Agent (`fetch` forbids the header), so the
  host *is* the fix. The frame keeps `site.api`, where its own honest UA is
  welcome. `test_browser_host_accepts_browser_user_agents` pins both halves.
- **The browser has the memory the ESP32 does not.** Team search is one call
  against every club in all six sports; the league list is 570 KB; the
  scoreboard behind "live now" is up to 147 KB for an in-season NFL day. All
  three are nothing in a browser and impossible on a ~39 KB free-heap budget —
  and an on-device version could still only offer what the firmware was
  compiled with.
- **The frame stores and polls; it does not browse.** A click POSTs
  `/api/espnfollow`, `/api/leaguefollow` or `/api/pin`, and everything after
  that is ordinary `control.h`. Pinning exploits this hardest: because a
  **competitor id is the team id**, the browser can hand over every id the live
  tick needs, so a pinned match skips discovery entirely and starts on the next
  poll.
- **The hardcoded `SportModule.catalogue` tables stay** as the offline
  fallback, shown automatically when the search fetch fails. That is not
  hypothetical: the dashboard is also served from the setup hotspot, where
  there is no internet at all. The Now tab says so rather than hanging.
- `/api/catalogue` exposes each module's `espnSport` slug and default league so
  the pickers map a result onto a sport from the registry — adding a sport
  stays a one-file job.

ESPN ranks search results by relevance, which floats age-group and university
sides up next to the senior club ("chennai" returns ten of them). Entries with
no `abbreviation` are exactly those, so the picker sinks them rather than
dropping them.

### Choosing what the panel shows

`sportsPickActive()` (sports_core.h) answers this in three tiers:

1. **A pin wins outright.** Held as an *event id* (`scorePinned`), not an index,
   because a poll reorders `scoreFront`. It releases itself the moment the match
   stops being live, after which the normal `sportHoldMin` hold applies.
2. **Rotation.** Every eligible match gets `sportRot` seconds in turn. This
   replaced "most recently changed wins", which had a real failure: with two
   matches live the card could flip on every tick as their `changedAt` stamps
   raced. `sportRot = 0` restores the old rule. A switch is **deferred while an
   event animation is playing** (`scoreFxBusy`, mirrored out of score_widget.h)
   so a goal never finishes on another match's scoreline.
3. **Eligibility** = live/break, sport enabled in `sportOn`, and `home.fav ||
   away.fav`. Note `fetchScores()` **ORs** those flags rather than assigning
   them: a league follow and a pinned match have no favourite side for
   `isFavTeam()` to recognise, so the provider marks them eligible itself.
   Overwriting there made league matches invisible — polled, parsed, then
   silently dropped.

### Adding a sport (live scores)

The score feature is layered so a new sport is data, not new rendering code:

1. write `firmware/DigiFrame/sport_<name>.h` exporting one `static const SportModule SPORT_<NAME>` — team catalogue, native-event map (each native name folds onto an `EventKind` plus a ≤5-char `punch` word and a longer label), a `drawBody()` for rows 21-54, an optional `drawEvent()` that returns `false` to fall back to the generic animation, and a `simulate()` demo feed
2. `#include` it in `sports_registry.h`
3. add it to `SPORTS_[]`

Nothing else changes — the dashboard dropdown, `/teams.json`, the poll task, the event ring and `/test` all walk that table. Two constraints bind every layout: rows 19-63 only, and the built-in GFX font (size 1 = 10 chars/row, **size 2 = 5 chars/row**, no custom fonts exist in this project) — which is why `gfxScorePair()` auto-drops to size 1 for three-digit scores like basketball's.

**Data source: the ESPN public API** (`espn_api.h`), no key and no account. `sportsPoll()` still dispatches `demo` (the simulator) or `http` (ESPN). Everything below was measured on 2026-08-15 and is the reason the code looks the way it does — none of it is guessable from the docs:

- **Plain HTTP, deliberately.** `site.api.espn.com` and `sports.core.api.espn.com` both answer on port 80 with no redirect. HTTPS would have meant a third concurrent TLS session (~32 KB internal DRAM) alongside Telegram and weather, against a ~39 KB free budget. Scores are public read-only data. Free heap measured identical before and after the feature.
- **Three cadences, because the scoreboard is unpollable.** NFL's scoreboard is **281 KB** (`odds` alone is 6.6 KB/event and cannot be excluded); at 15 s that is ~1.6 GB/day. Instead: catalogue `/teams` (~135 KB, weekly, cached to `/espn_<sport>.json`), discovery `/teams/{id}` → `nextEvent` (~25 KB, lazy), live core `score`×2 + `status` (**~830 B**, every 15 s). `nextEvent` also yields the league slug and kickoff time, so the frame sleeps until the match starts.
- **The User-Agent is load-bearing.** ESPN's edge 403s on `Mozilla/5.0`, `Wget/1.21` and any product name, while accepting `ESP32HTTPClient`, `curl/…` and `python-requests`. Do not "improve" `ESPN_USER_AGENT` — see the table in config.h.
- **The response stream needs both buffering and chunk decoding.** ArduinoJson reads one byte at a time and each `read()` on a `WiFiClient` is a socket call, so parsing 93 KB straight from `getStream()` never finishes and looks exactly like a hang. Separately, HTTPClient only unchunks inside `getString()`/`writeToStream()` — `/teams` sends Content-Length but the cricket header **and every core-API live endpoint** send `Transfer-Encoding: chunked`, so reading the socket yourself gets hex chunk-size lines mixed into the JSON. `EspnBufferedStream` does both. Also raise `NestingLimit` (ESPN nests 12 deep; the default is 10) — a filter still walks the whole document.
- **Events are derived, not fed.** The cheap endpoints carry scores, not plays, so `sportsDiff()` turns a score delta into a native event through the sport module's `eventForDelta()` (+3 = a three-pointer, +6 = a touchdown, a wicket moves `score2`). This is why `evIsMajor()` matters: the derivable events are exactly the major ones. Penalties, reviews and lead changes are not derivable and stay simulator-only.
- **Following a whole league is a scan, not a scoreboard poll.** Even filtered
  to one day the NFL scoreboard measured 147 KB in season. The core API has a
  far cheaper "what is on today": `events?dates=` is **~1 KB** for a full slate,
  `competitions/{id}/status` is **~350 B**, and `competitions/{id}/competitors`
  is **~1.4 KB** once. `espnScanLeague()` walks at most `ESPN_SCAN_MAX` events
  and stops at the first live one, so a normal scan is two calls.
- **Ask for a two-day UTC span, never "today".** ESPN files a fixture under its
  own local date, which is not the frame's: from IST the NFL games in progress
  are filed under *yesterday*, and a local-today query returns zero events with
  no error. Both `espnScanLeague()` and the dashboard's "live now" use
  `dates=<yesterday>-<today>` in UTC. This cost real debugging time twice, once
  on each side.
- **Identity for matches with no followed team** comes from a small RAM table
  (`espnIdent[]`), filled by the same filtered `/teams` parse
  `espnFetchCatalogue()` already runs — core 0 cannot read the flash catalogue,
  so this is the only way a league match learns names and colours without extra
  calls. It also finally names the *opponent* in a followed team's match, which
  used to render as a grey "OPP".
- **Prefer a team's `alternateColor` when the primary is too dark.**
  `teamInkPair()` — Cleveland's primary is `472a08`, a brown that `teamInk()`
  lifts to barely legible and which read as a smudge on the card; their
  alternate is `ff3c00`. `teamInk()` is a legibility floor, not a palette, so
  it cannot fix this on its own. Nothing is invented: both colours are the
  team's own.
- **A team is discovered under its own league, not its sport's default.** `SportModule.espnLeague` is only a starting point; `FavTeam.league` overrides it. Looking Real Madrid (`esp.1`) up under football's default `eng.1` returns **HTTP 200 with an empty `nextEvent[]`** — no error, the card just never appears. Anything that adds a favourite must carry the league through.
- **Cricket's ball-by-ball is `playbyplay`, and its paging is backwards.** `?event={id}&limit=N&page=P` windows from the **start** of the innings, so `limit=6` returns the first over of the day, not the last. The response carries `count`/`pageCount`, so `espnCricketBalls()` **rebuilds** the strip from the tail page. It deliberately does not append by id: **commentary ids restart at each innings**, not at the match (a measured first innings ran `110 … 115060` and the second opened at `210`), so a high-water mark carried across the break sits above every id of the new innings and freezes the strip on the old one. Ids are monotonic only *within* an innings. The reach-back to `page-1` is **conditional** — a page is six deliveries, so a full tail page already is the last six, and a page costs ~16 KB because every item carries both squads' bowling figures. The strip lives in `EspnWatch`, not `LiveMatch`, because the provider rebuilds the match every poll. **Every page is padded with a sentinel** — id `999999999999999`, empty text, but a valid-looking `playType` of `2`/no run — filtered on empty `shortText` *and* id magnitude.
- **Cricket's overs belong to the side at the crease, and only `linescores` knows which that is.** From the second innings on **both** competitors carry a `(… ov)`: a completed innings keeps its own, so `VKK 171/3 (16/16 ov)` sits beside `DD 88/7 (11/16 ov)`. Parsing both into one shared `overs` buffer meant the card printed whichever competitor the array listed last — and the array is ordered home/away, not by who is batting — so a chase at 11 overs was captioned 16, `cricketProgress()` read the comet as 100% for the whole innings, and both team bars breathed because `Side.active` was `score contains '('`. All three were the same bug and all three appeared *only* after the first innings. The fix reads `competitors[].linescores[]`: exactly one side has `isBatting` on its `isCurrent` innings. Do not substitute `order` vs `status.period` — it agrees in limited-overs but not after a follow-on, where one side bats two innings running (verified against ENG v PAK at period 3).
- **The strip refreshes when the score moves, not on a timer.** `espnCricketStrip()` signs the card's own numbers (both sides' runs/wickets plus overs) and refetches when that signature changes, with `ESPN_BALLS_MS` surviving only as the idle refresh for deliveries that move no number (dots, a maiden). A flat 60 s timer against cricket's 20 s score tick meant the card could show a wicket in `64/3` with no `W` in the pills beneath it for two polls — the two halves of one card disagreeing about the same delivery. The **pinned** path calls it too; it previously had no strip fetch at all, so a pinned cricket match ran its whole innings with an empty pill row.
- **Match-state keywords are matched case-insensitively.** ESPN writes them as prose as often as labels: `"Match delayed by a wet outfield"` is a suspension, and a case-sensitive search for `"Delay"` misses it, leaving a stopped match reading as in progress.
- **Cricket is special in every layer.** Its `/teams` 404s and its "leagues" are per-tour series whose ids change, so discovery uses the personalized header (all active series in one call) and the live tick uses the series scoreboard (6.7 KB) — the core score endpoints are useless because cricket's score is a preformatted **string** (`"198 & 161/4 (53 ov)"` → last innings, runs, wickets, overs). A completed innings is bare runs (`"426"`), which means 10 wickets, not "has not batted".
