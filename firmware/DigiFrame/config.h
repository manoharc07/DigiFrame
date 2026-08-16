/* DigiFrame — user config + pin map (compile-time DEFAULTS; runtime overrides live in /config.json on LittleFS) */
#pragma once

/**********************  1. USER CONFIG  ******************************/
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASS "YOUR_WIFI_PASSWORD"

// Create a bot with @BotFather on Telegram, paste the token here.
#define BOT_TOKEN "YOUR_BOT_TOKEN"
// Send /start to your bot, then visit
// https://api.telegram.org/bot<TOKEN>/getUpdates to find your chat id.
// Only THIS chat id can control the frame (security!).
#define ALLOWED_CHAT_ID "YOUR_CHAT_ID"

#define TZ_OFFSET_SEC 19800 // default UTC+5:30 (override at runtime)
#define LATITUDE "12.97"    // default location — set your own from the dashboard
#define LONGITUDE "77.59"

#define DAY_BRIGHTNESS 100  // 0-255
#define NIGHT_BRIGHTNESS 3  // midnight sleep glow — very dim (1 = dimmest visible)
#define CELEBRATE_BRIGHTNESS 140  // brightness during a special-day celebration
#define NIGHT_START_HR 0 // 00:00
#define NIGHT_END_HR 7   // 07:00
#define MSG_MINUTES 10   // how long /msg scrolls

/* ---- setup hotspot: started when WiFi can't connect. The panel shows a
   QR code that joins this network; a captive portal then opens the
   config page (http://192.168.4.1) to set the real WiFi. ---- */
#define AP_SSID "DigiFrame"
#define AP_PASS "digiframe123" // >= 8 chars (WPA2)
#define WIFI_FAIL_PORTAL_MIN 5 // runtime outage (min) before portal reopens

/* ---- cloud dashboard. NOT currently used by the firmware: the setup QR
   now encodes a WIFI: hotspot-join payload, because an https:// page cannot
   call this device's http:// LAN API (browser mixed-content rules). Reaching
   the frame from a cloud page requires an outbound relay connection from the
   device — reserved here for that work. Setup and LAN control both go
   through the on-device dashboard (hotspot -> http://192.168.4.1, or
   http://digiframe.local once on your network). ---- */
#define CLOUD_SITE_URL "https://digiframe.pages.dev"

/* ---- Home Assistant integration over MQTT (all overridable at runtime from
   the dashboard). Off by default so non-HA users are unaffected. Point
   MQTT_HOST at your broker (e.g. the Mosquitto add-on) and enable it. The
   clock announces itself to Home Assistant via MQTT discovery. ---- */
#define MQTT_ENABLE false
#define MQTT_HOST ""          // broker IP/hostname, e.g. "192.168.1.10"
#define MQTT_PORT 1883
#define MQTT_USER ""
#define MQTT_PASS ""

/* ---- render loop / core-1 pacing ----
   Core 1 runs the renderer, the WebServer, the captive DNS and every
   LittleFS touch in one cooperative loop(), and the ESP32 core calls
   loop() back-to-back with no yield at all. Three knobs keep that loop
   from stealing time from the render cadence: */
#define RENDER_FPS      15   // content frame rate (see FRAME_MS in globals.h).
                             // The ambient scene, celebration and score
                             // animations are all step-tuned for 15 — raising
                             // this speeds every animation up proportionally.
#define NET_SERVICE_MS   2   // how often loop() polls the WebServer + WiFi
                             // manager. These are lwIP/WiFi-driver calls that
                             // take the same locks the WiFi task holds; every
                             // pass costs contention and buys no latency, so
                             // poll at ~500 Hz instead of ~50 kHz.
#define WIFI_TICK_MS   250   // wifiManagerTick() housekeeping cadence (the
                             // captive-portal DNS still runs at NET_SERVICE_MS)
#define GIF_MIN_FRAME_MS 20  // floor for a GIF's own frame delay — many GIFs
                             // declare 0, which would busy-blit the panel
#define SERIAL_TX_TIMEOUT_MS 10  // applied after setup(). Serial.print blocks
                             // on the USB-CDC tx lock for up to this long, and
                             // a blocked logLine() on core 1 is a dropped
                             // frame. Raise to 100 to debug boot output.

/* ---- live scores from the ESPN public API (espn_api.h) ----
   No key, no account. Verified 2026-08-15 to serve over plain HTTP, which
   is why this costs no internal DRAM: a TLS session is ~32 KB and this
   board only has ~39 KB free once WiFi and the panel are up (see the
   PANEL_COLOR_DEPTH note below). Scores are public read-only data, so
   plaintext is an acceptable trade — the worst a MITM can do is show the
   wrong score on a clock.

   Polling is tiered because the full scoreboard is far too heavy to poll:
   NFL measured 281 KB. Discovery finds *which* match to watch and when it
   starts; the live tick then costs ~830 bytes. */
#define ESPN_ENABLE        1
#define ESPN_LIVE_MS       15000UL      // live tick while a followed match is on
#define ESPN_DISCOVER_MS   600000UL     // 10 min: look for the next fixture
#define ESPN_PREGAME_MS    60000UL      // 1 min once kickoff is close
#define ESPN_CATALOGUE_H   168          // hours between team-catalogue refreshes
#define ESPN_MIN_GAP_MS    1200UL       // hard floor between any two requests
#define ESPN_HTTP_TIMEOUT  8000         // per-request timeout
/* ---- following a whole league ----
   A league follow cannot use the scoreboard: even filtered to one day, NFL's
   measured 147 KB on an in-season Sunday (271 KB unfiltered). The core API has
   a far cheaper "what is on today": events?dates is ~1 KB for a full slate and
   each competition status is ~350 B, so the scan stops at the first live match
   and usually costs two calls. */
#define ESPN_SCAN_MS       300000UL     // 5 min: re-scan a league for a live match
#define ESPN_SCAN_MAX      8            // events inspected per scan, newest first
/* Team names and colours for matches with no followed team, held in RAM
   because core 0 cannot read the flash catalogue. ~40 bytes each. */
#define ESPN_IDENT_MAX     48
#define NUM_SPORTS_MAX     8            // compile-time bound on SPORTS[]
/* ESPN's edge blocks by User-Agent, and it is NOT a simple bot filter —
   measured 2026-08-15, stable over repeated rounds:
       ESP32HTTPClient    200      curl/8.5.0          200
       python-requests    200      Mozilla/5.0         403
       Wget/1.21          403      DigiFrame/1.0       403
   So a browser-shaped string is rejected while an honest tool string is
   accepted. "ESP32HTTPClient" is Arduino HTTPClient's own default, which
   both works and truthfully identifies the client — do not "improve" this
   into a product name or a fake browser UA; both are 403. */
#define ESPN_USER_AGENT    "ESP32HTTPClient"

/* ---- development endpoints ----
   POST /api/dev (drive the panel into a given visual state) and the panel
   screenshot shadow behind GET /api/frame. Used by dev/panelshot.py to
   develop and review the on-panel UI without a camera. Set to 0 to compile
   both out; the capture shadow costs nothing until armed either way. */
#define DEV_ENDPOINTS 1

/**********************  2. PIN MAP  **********************************/
/* HUB75 (adjust freely if your wiring differs — every GPIO works,
   just avoid 0, 19/20 (USB), 26-32 (flash), 33-37 (PSRAM on N16R8),
   43/44 (UART), 45/46 (strapping). */
#define R1_PIN 4
#define G1_PIN 5
#define B1_PIN 6
#define R2_PIN 7
#define G2_PIN 15
#define B2_PIN 16
#define A_PIN 18
#define B_PIN 8
#define C_PIN 9
#define D_PIN 10
#define E_PIN 42 // REQUIRED on 64x64 (1/32 scan) — must be wired!
#define LAT_PIN 40
#define OE_PIN 2
#define CLK_PIN 41

#define PANEL_W 64
#define PANEL_H 64

/* HUB75 colour depth (bits/channel). The DMA framebuffer is
   (PANEL_H/2) * PANEL_W * 2 bytes * depth, DOUBLED because double_buff is on
   — and it must live in internal DRAM, which is the scarcest resource on this
   board (WiFi+AP wants ~70 KB of the same pool, and every concurrent TLS
   session ~32 KB).
     8 -> 64 KB   7 -> 56 KB   6 -> 48 KB   4 -> 32 KB

   Two constraints, both easy to get wrong:

   1. It MUST be one of the library's native CIE-luminance LUT depths —
      4, 6, 7, 8, 10 or 12 (see the library's cie_luts.h). 5 is NOT one.
   2. It MUST match -DPIXEL_COLOR_DEPTH_BITS in build_opt.h. The library picks
      its luminance table at COMPILE time from that macro, whereas the runtime
      cfg.setPixelColorDepthBits() call in DigiFrame.ino sets the framebuffer
      depth. A sketch #define cannot reach the library's own .cpp files, which
      is why build_opt.h exists — the ESP32 core passes it to every
      translation unit.

   This was 5 with the macro left at its default of 8: the luminance table
   then did not match the bitplanes in use, so individual colour values
   mapped non-monotonically and some mid-tones rendered black. Fully
   saturated colours were unaffected, which made it look like a panel fault.

   8 costs 64 KB and is affordable only because the Bluetooth stack (~67 KB)
   was removed. If DRAM gets tight again, drop to 6 — and change build_opt.h
   in the same commit. */
#define PANEL_COLOR_DEPTH 8

/* HUB75 pixel clock and the scan rate it buys.

   The library does NOT display all PANEL_COLOR_DEPTH bitplanes at full
   weight if that would scan too slowly: at begin() it raises an internal
   `lsbMsbTransitionBit` until the computed scan rate reaches
   PANEL_MIN_REFRESH, trading perceived colour depth for refresh. At the
   library default of 8 MHz, depth 8 on a 64x64 needs a large transition
   bit — so the panel was giving up colour gradation *and* still scanning
   near the 60 Hz floor, which is what low-brightness shimmer looks like.

   16 MHz doubles the budget: a lower transition bit (better mid-tones) at
   a higher scan rate. Both are reported at boot as "panel: N Hz" — check
   it after changing either knob. If you see ghosting, smearing or wrong
   columns, the ribbon cable can't take 16 MHz: drop back to HZ_8M (and
   raising `latch_blanking` in DigiFrame.ino helps ghosting specifically). */
#define PANEL_I2S_HZ       HUB75_I2S_CFG::HZ_16M
#define PANEL_MIN_REFRESH  100   // Hz floor the library must reach
