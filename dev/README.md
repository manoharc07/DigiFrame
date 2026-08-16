# dev/ — UI development tooling

Two tools for seeing and iterating on DigiFrame's two user interfaces. Neither
ships to the device; both talk to a running frame over the LAN.

| | tool | what it shows |
|---|---|---|
| LED panel | `panelshot.py` | the 64x64 output — clock face, ambient scene, score cards, celebrations |
| Web dashboard | `dashshot.py` | the HTML/CSS/JS served from `web_portal.h`, rendered in real Chromium |

Output lands in `dev/shots/` (git-ignored).

```
pip install requests pillow numpy playwright
python -m playwright install chromium      # dashshot only
```

---

## panelshot.py — photograph the LED panel

These are **true screenshots, not a simulation**. The firmware mirrors every
draw call into an RGB565 shadow buffer (`firmware/DigiFrame/capture.h`) and
serves the presented frame from `GET /api/frame`; the tool decodes it and
scales it up. Colour, layout and timing are the device's own.

```bash
python dev/panelshot.py shot                          # one frame, right now
python dev/panelshot.py film --seconds 3              # animated GIF + contact sheet
python dev/panelshot.py sweep --wcodes 0,2,45,61,73,95   # compare side by side
python dev/panelshot.py stop                          # back to the clock
```

### Reaching the screen you want

A screenshot is only useful if you can get to the screen worth photographing.
`POST /api/dev` drives the panel into a given state, and every capture command
accepts the same flags:

```bash
python dev/panelshot.py shot --hour 22 --wcode 0      # the night scene, at noon
python dev/panelshot.py shot --sport 0 --event WICKET # a sport's card + animation
python dev/panelshot.py film --celebrate birthday --seconds 5
python dev/panelshot.py shot --msg "HELLO WORLD"
python dev/panelshot.py sheet --test --steps 17 --every 4   # every screen /test walks
```

`--hour` and `--wcode` matter more than they look: the ambient scene branches on
the time of day and the weather code, so without them the only scene you can
ever photograph is whatever the real clock and sky happen to be doing.

`sweep` varies one axis and holds the rest, producing a labelled grid — the
fastest way to catch a difference of a few pixels:

```bash
python dev/panelshot.py sweep --hours 0,6,9,12,15,18,21 --hold-wcode 0
python dev/panelshot.py sweep --sports 0,1,2,3,4,5
```

### How it behaves on the device

* Capture **arms lazily** on the first `/api/frame` request and **auto-disarms
  30 s** after the last one. When nobody is watching, the render path costs one
  predicted branch per draw primitive and nothing else.
* The shadow buffers (2 x 8 KB) are allocated in **PSRAM**, not internal DRAM —
  internal DRAM is the binding resource on this board.
* The first request after arming answers **503** until a frame is presented; the
  tool retries. It cannot block waiting: the handler runs inside `loop()` on
  core 1, which is also the only thing that draws, so waiting there would
  deadlock the renderer it is waiting on.
* Capture frame rate is limited by **HTTP round-trips over WiFi**, not by the
  panel. `--fps` is a request; the actual rate is reported.

Set `DEV_ENDPOINTS` to 0 in `config.h` to compile `/api/frame` and `/api/dev`
out entirely.

---

## dashshot.py — screenshot and audit the dashboard

Loads the dashboard from the live device in headless Chromium, so you get the
actual served HTML rendered by a real engine rather than a guess about how the
`DASH_HTML` string will lay out.

```bash
python dev/dashshot.py                    # phone/tablet/desktop + health report
python dev/dashshot.py --widths 390       # one width
python dev/dashshot.py --no-shots         # audit only
python dev/dashshot.py --url file:///C:/tmp/dash.html   # offline iteration
```

Alongside the screenshots it reports what a source read cannot tell you:
JavaScript errors, failed `/api/*` requests, horizontal overflow (with the
offending elements named), tap targets under 24px, missing viewport meta, and
form controls with no accessible name.

---

## Notes

* Both tools default to `digiframe.local`; pass `--host 192.168.x.x` if mDNS is
  unreliable on your network.
* The frame must be powered on and on the same LAN. There is no offline
  simulator — the panel renderer needs the device (`dashshot --url` can work
  against a local file).
* `panelshot.py stop` returns the panel to the clock and disarms capture. The
  dev overrides are also cleared by any normal Stop, from any front end.
