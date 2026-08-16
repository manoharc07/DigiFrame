# DigiFrame ⏰

[![Featured on XDA Developers](https://img.shields.io/badge/Featured%20on-XDA%20Developers-EE4C2C)](https://www.xda-developers.com/this-awesome-esp32-clock-lets-you-send-messages-to-it-anywhere-in-the-world/)
[![Sponsor](https://img.shields.io/badge/Sponsor-%E2%9D%A4-EA4AAA?logo=githubsponsors&logoColor=white)](https://github.com/sponsors/manoharc07)
[![License: PolyForm Noncommercial](https://img.shields.io/badge/license-PolyForm%20Noncommercial-blue)](LICENSE.md)

A **64×64 HUB75 LED matrix smart clock** running on an ESP32-S3. It shows an
NTP clock, live weather, a living ambient scene, looping GIFs, scrolling
messages, **live sports scores** for teams and leagues you follow, and runs
themed celebrations on your special days. Configure and control it from the
**clock's own web dashboard** on your WiFi, a **Telegram bot**, or
**Home Assistant** (MQTT).

> Source-available for **DIY / noncommercial** use — contributions welcome. Commercial use needs a [separate license](#license).

> 📰 **[Featured on XDA Developers](https://www.xda-developers.com/this-awesome-esp32-clock-lets-you-send-messages-to-it-anywhere-in-the-world/)** —
> *"This awesome ESP32 clock lets you send messages to it anywhere in the world."*

<p align="center">
  <img src="images/clock-poster-photo.png" alt="DigiFrame — a 64×64 LED matrix smart clock" width="400">
  <img src="images/clock-with-frame.jpeg" alt="DigiFrame in its 3D-printed glass frame" width="400">
</p>

<p align="center">
  <img src="images/clock-view.gif" alt="DigiFrame clock face — time, weather and the ambient scene" width="420">
  <br><em>The clock face — time, weather and the living ambient scene.</em>
</p>

<p align="center">
  <img src="images/clock-live-video.gif" alt="DigiFrame running live in its glass frame" width="280">
  <br><em>Clock in action.</em>
</p>



## Features

- **Clock + weather** — NTP time and Open-Meteo weather (no API key), with an
  animated ambient scene and automatic night dimming.
- **GIFs** — a default pack is embedded and seeded to flash on first boot;
  upload your own. Any `c_*.gif` joins a "character pack" that makes random
  cameos.
- **Messages** — scroll a note for a while, or pin one until you stop it.
- **Live sports scores** — follow teams *or* whole leagues across cricket,
  football, basketball, American football, ice hockey and rugby. When something
  you follow kicks off, the lower two-thirds of the clock face becomes a score
  card with the teams' own colours, and per-sport animations fire on the
  moments worth looking up for. Several matches live at once rotate in turn,
  and you can pin any match to the panel from the dashboard. Scores come
  straight from ESPN's public API — no key, no account, no relay.
- **Special days** — give a date a **type** (`custom` → fireworks, `birthday` →
  cake + confetti) and a message; at midnight the clock runs that themed
  celebration all day. Add them from any dashboard or Telegram.
- **Telegram bot** — control playback, messages, brightness, special days and
  more from anywhere, with tap-able button menus.
- **Home Assistant** — optional MQTT integration with auto-discovery: brightness,
  a message box, celebrate/stop buttons, and temperature/mode sensors.
- **Two ways to configure**: the on-device web dashboard, or Telegram.
- **OTA firmware updates** from the on-device dashboard.

## Hardware

- ESP32-S3 dev board (tested on N16R8: 16 MB flash, 8 MB OPI PSRAM)
- 64×64 HUB75 RGB LED matrix, 2.5 mm pitch — e.g. the **Waveshare P2.5 64×64** panel
- **5 V power supply** for the panel (≈2–4 A; sized for brightness), plus the
  16-pin HUB75 ribbon + a few jumper wires
- Optional: the 3D-printed enclosure below + a 3.5 mm thin glass front

## Wiring

The panel is driven over the 16-pin **HUB75E** header on the panel's **input**
side (the arrow points *away* from IN; some panels label it `J1`/`IN`). The
GPIO assignments live in [`config.h`](firmware/DigiFrame/config.h) — change them freely, just
avoid the reserved pins noted there. Defaults for the ESP32-S3:

| HUB75 signal | Meaning            | ESP32-S3 GPIO |
|--------------|--------------------|:-------------:|
| R1           | red (top half)     | 4  |
| G1           | green (top half)   | 5  |
| B1           | blue (top half)    | 6  |
| R2           | red (bottom half)  | 7  |
| G2           | green (bottom half)| 15 |
| B2           | blue (bottom half) | 16 |
| A            | row address A      | 18 |
| B            | row address B      | 8  |
| C            | row address C      | 9  |
| D            | row address D      | 10 |
| E            | row address E      | 42 |
| CLK          | pixel clock        | 41 |
| LAT / STB    | latch              | 40 |
| OE           | output enable      | 2  |
| GND          | ground             | GND (shared) |

HUB75E header layout (pin 1 is usually marked on the connector):

```
      ┌──────────┐
 R1 → │ 1     2  │ ← G1
 B1 → │ 3     4  │ ← GND
 R2 → │ 5     6  │ ← G2
 B2 → │ 7     8  │ ← E      (E on 1/32-scan 64×64 panels; GND on 1/16 panels)
  A → │ 9    10  │ ← B
  C → │11    12  │ ← D
 CLK→ │13    14  │ ← LAT
 OE → │15    16  │ ← GND
      └──────────┘
```

**Power & grounding (important):**
- Power the **panel from the 5 V supply**, into its power lugs/terminals — do
  **not** run the panel off the ESP32's 5 V pin (it can't supply the current).
- The ESP32-S3 is powered over USB (or its own 5 V).
- Tie the **PSU ground, panel ground, and ESP32 ground together** (common GND) —
  the HUB75 `GND` pins above cover the signal ground.
- `E` is **required** for 64×64 (1/32-scan) panels. Keep signal jumpers short
  (< ~15 cm) or use a HUB75 adapter board to avoid flicker/ghosting.

On first power-up the panel shows `HELLO`, then the WiFi **setup QR** — follow
*Setup & control* below.

## Enclosure (3D-printable)

A two-part frame is in [`stl/glass-frame/`](stl/glass-frame):

- `frame-box-v1.stl` — the body that holds the panel and electronics
- `frame-lid-v1.stl` — the back lid

It's designed around the **Waveshare P2.5 64×64** panel with a **3.5 mm thin
glass** sheet at the front (the glass diffuses the LEDs and gives a clean
finish). Print in PLA/PETG; the glass drops into the front recess, the panel
sits behind it, and the ESP32 + wiring tuck inside before the lid closes.

## Build & flash

Full instructions in [`FLASHING.md`](FLASHING.md). In short:

```
arduino-cli compile --fqbn esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=custom,CDCOnBoot=cdc --output-dir build firmware/DigiFrame
```

Board: **ESP32S3 Dev Module**, Flash 16 MB, PSRAM **OPI PSRAM**, Partition
scheme **Custom** (uses the sketch's `partitions.csv` — 4 MB OTA app slots +
~7.9 MB data). Libraries (Library Manager): `ESP32 HUB75 LED MATRIX PANEL DMA
Display`, `Adafruit GFX Library`, `AnimatedGIF`, `UniversalTelegramBot`,
`ArduinoJson`, `PubSubClient`.

## Setup & control

On first boot, if the frame can't reach a stored WiFi it opens a hotspot and
shows a QR code.

### 1. On-device dashboard (any browser, incl. iPhone)

Scanning the panel QR joins the `DigiFrame` hotspot directly; the captive page
then opens at `http://192.168.4.1`. Enter your WiFi there, and once the frame
is online the same dashboard lives at `http://digiframe.local`, in four tabs:

- **Now** — what's live right now, a status line, and one tap to put any match
  on the panel. Messages and brightness.
- **Scores** — search ESPN for any team or league to follow, switch whole
  sports on and off, and set rotation, effects and refresh rate.
- **Content** — GIF upload, random cameos, special days.
- **Settings** — WiFi, Telegram, weather location, timezone, MQTT, OTA, logs.

The team and league pickers search ESPN from *your browser*, not the frame — it
has neither the memory to hold a league list nor a reason to.

### 2. Telegram bot (anywhere)

Create a bot with @BotFather, set the token + your chat id (via any dashboard),
and control the clock remotely. Send `/menu` for the button menu.

### 3. Home Assistant (MQTT)

Enable MQTT and set your broker (e.g. the Mosquitto add-on) from any dashboard.
The clock announces itself to Home Assistant via MQTT discovery as a device with
brightness, a message text box, celebrate/stop buttons, and temperature/mode
sensors. Off by default.

## How it works

Single Arduino sketch, dual-core FreeRTOS. **Core 1** owns the LED panel,
GIF decoder, web server and mode state; **core 0** runs Telegram polling,
weather fetches, and the MQTT client. A shared
[`control.h`](firmware/DigiFrame/control.h) layer holds one implementation per action, so every
front-end (HTTP dashboard, Telegram, Home Assistant) behaves
identically; core-0 tasks marshal work to core 1 through a command queue (they
never touch LittleFS or the panel directly).

Architecture details and invariants are in [`CLAUDE.md`](CLAUDE.md).

### Testing and UI tooling

There is no CI — verification is a clean compile plus two host-side tools that
need no dependencies beyond the standard library:

```bash
python tests/run.py                 # everything
python tests/run.py --only espn     # ESPN contract only — no device needed
python dev/panelshot.py shot        # a true screenshot of the LED panel
python dev/dashshot.py              # screenshot + audit the web dashboard
```

[`tests/`](tests/README.md) mocks nothing. The `espn` group pins every
assumption the score feature makes about ESPN's undocumented API, so when
scores break it tells you *which* assumption died — separating "ESPN changed"
from "we broke it". [`dev/`](dev/README.md) makes the panel reviewable: the
firmware mirrors every draw call into a shadow buffer and serves the presented
frame over HTTP, so `panelshot.py` produces real screenshots rather than a
simulation.

## Configuration & security

`config.h` holds only compile-time **defaults** (placeholders like
`YOUR_WIFI_SSID` / `YOUR_BOT_TOKEN`); real values are entered at runtime and
persisted to `/config.json` on the device. Treat any token/password you flash
in as sensitive and don't commit real ones.

The web dashboard is **unauthenticated** and LAN-only — anyone on your network
can reach it. Keep that in mind before exposing the frame on a shared or public
network, and never port-forward it.

## Roadmap

- **More widgets** — now-playing, stock/finance tickers and other at-a-glance
  panels on the clock face, built the way live scores already is.
- **Ball-accurate score events** — derived events currently infer a boundary
  from a score delta; using the over count would tell a genuine four from three
  singles in the same poll window.
- **Cloud relay backend** — an outbound connection from the frame to a broker,
  so a cloud page can reach the clock from anywhere. A browser can't call the
  frame's LAN API from an `https://` page (mixed content), so a relay is the
  only route. Today Telegram and Home Assistant fill that role.

## Support the project

DigiFrame is free for DIY / noncommercial use. If it helped you or you just want
to say thanks, you can sponsor me on GitHub ❤️ — it funds more features:

[![Sponsor](https://img.shields.io/badge/Sponsor%20on%20GitHub-support-EA4AAA?logo=githubsponsors&logoColor=white)](https://github.com/sponsors/manoharc07)

## License

**Source-available, noncommercial** — free for personal, hobby/DIY, educational,
and research use under the [PolyForm Noncommercial License 1.0.0](LICENSE.md).

**Commercial use** (selling the device or firmware, or bundling it into a paid
product or service) **requires a separate license** — contact
[@manoharc07](https://github.com/manoharc07) to arrange terms.
