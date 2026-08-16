/**********************************************************************
 *  DIGIFRAME  —  64x64 HUB75 Smart Clock
 *  ESP32-S3 (N16R8)  +  P2.5 64x64 HUB75 panel
 *
 *  FEATURES
 *   - NTP clock + Open-Meteo weather (no API key needed) + living ambient scene
 *   - Automatic night mode (dim glow after midnight)
 *   - On-device dashboard (http://digiframe.local): GIF upload, WiFi,
 *     brightness, messages, logs
 *   - WiFi setup mode: if WiFi can't connect, the clock starts its own
 *     hotspot and shows a QR code on the panel — scan to join, then the
 *     config page opens (captive portal) to enter your WiFi. When the
 *     stored network comes back, it reconnects automatically.
 *   - Telegram bot remote control with tap-able button menus (/menu):
 *       /msg <text>        scroll a message for ~10 minutes
 *       /pin <text>        scroll a message until Stop
 *       /play <name>       play a stored GIF on loop (or tap ▶ Play GIF)
 *       /list  /del <name>
 *       /brightness <1-255> (or tap 💡 Brightness)
 *       /event add MM-DD <type> <message>   /event del MM-DD   /events
 *       /celebrate  /test  /stop  /status  /help
 *     (GIF upload is done on the dashboard, not through Telegram)
 *   - Special days: give a date a TYPE (custom / birthday) and a message; at
 *     00:00 the clock auto-runs the themed celebration (fireworks or cake +
 *     rotating banner) all day long. Manual preview via /celebrate.
 *   - Home Assistant integration over MQTT (optional): brightness, message,
 *     celebrate/stop, plus temperature/mode sensors, via MQTT discovery.
 *
 *  FILES (Arduino IDE shows these as tabs; include order matters)
 *   config.h        user config + pin map (compile-time defaults)
 *   globals.h       globals, cross-core queue, background tasks, colors
 *   gif_player.h    GIF decode callbacks + open/close + character pack
 *   events_store.h  special days + persisted config on LittleFS
 *   weather.h       Open-Meteo fetch + weather icons
 *   scene.h         clock face + ambient scene
 *   scroll.h        scrolling text renderer
 *   party.h         celebration (special-day) mode + test mode
 *   control.h       shared control layer (HTTP + MQTT call the same ctl*)
 *   telegram.h      Telegram bot commands + menus
 *   web_portal.h    web dashboard + captive portal pages
 *   mqtt_ha.h       Home Assistant integration over MQTT (optional)
 *   qr_display.h    on-panel QR codes for setup mode
 *   wifi_manager.h  WiFi connect, hotspot fallback, auto-reconnect
 *
 *  LIBRARIES (Arduino IDE -> Library Manager unless noted)
 *   - "ESP32 HUB75 LED MATRIX PANEL DMA Display" (mrfaptastic)
 *   - "Adafruit GFX Library"
 *   - "AnimatedGIF" (Larry Bank)
 *   - "UniversalTelegramBot" (Brian Lough)  + "ArduinoJson"
 *   - "PubSubClient" (Nick O'Leary)
 *   (QR codes use the espressif__qrcode component bundled with the core)
 *
 *  BOARD SETTINGS (Tools menu)
 *   Board: "ESP32S3 Dev Module"
 *   Flash Size: 16MB | PSRAM: "OPI PSRAM"
 *   Partition Scheme: "Custom" (uses this sketch's partitions.csv —
 *     4MB OTA app slots + ~7.9MB ffat data for LittleFS)
 *
 *  CLI BUILD (see FLASHING.md)
 *   arduino-cli compile --fqbn esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=custom,CDCOnBoot=cdc --output-dir build firmware/DigiFrame
 *
 *  FILES to place on LittleFS (upload via the dashboard, or once via
 *  the "ESP32 Sketch Data Upload" plugin):
 *   /cake.gif       64x64 animation used by the "birthday" celebration type
 *   /idle.gif       optional cute idle animation
 *********************************************************************/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <time.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <AnimatedGIF.h>
#include <UniversalTelegramBot.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>

#include "config.h"
#include "capture.h"          // dev tool: panel screenshot shadow (must precede globals.h)
#include "globals.h"
#include "gif_player.h"
#include "events_store.h"
#include "sports_core.h"      // live scores: model, favourites store, poll task
#include "espn_api.h"         // live scores: the ESPN provider behind sportSrc="http"
#include "weather.h"
#include "scene.h"
#include "score_gfx.h"        // live scores: shared drawing toolkit
#include "score_fx.h"         // live scores: generic event animations
#include "sports_registry.h"  // live scores: the sport modules
#include "score_widget.h"     // live scores: the dispatcher renderClock() calls
#include "scroll.h"
#include "party.h"
#include "control.h"
#include "telegram.h"
#include "web_portal.h"
#include "mqtt_ha.h"
#include "qr_display.h"
#include "wifi_manager.h"

/* Boot-time memory probe. Internal (non-PSRAM) DRAM is the binding resource
   on this board — WiFi, the panel's DMA framebuffer and every TLS session
   compete for it, and PSRAM cannot substitute (see config.h). Allocators
   that need a *contiguous* block care about `largest`, not the total, so
   print both around anything big in setup(). */
static void heapReport(const char *where) {
  Serial.printf("[heap] %-22s internal free %6u  largest %6u | psram free %7u\n",
                where,
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  Serial.flush();
  delay(20);        // USB-CDC: let the line drain in case the next call abort()s

}

/**********************  13. SETUP  ***********************************/
void setup() {
  Serial.begin(115200);
  delay(300);

  /* ---- matrix ---- */
  HUB75_I2S_CFG::i2s_pins pins = { R1_PIN, G1_PIN, B1_PIN, R2_PIN, G2_PIN,
                                   B2_PIN, A_PIN, B_PIN, C_PIN, D_PIN, E_PIN,
                                   LAT_PIN, OE_PIN, CLK_PIN };
  HUB75_I2S_CFG cfg(PANEL_W, PANEL_H, 1, pins);
  cfg.latch_blanking = 2;          // helps ghosting on some P2.5 panels
  cfg.clkphase = false;            // flip if you see column shift
  cfg.double_buff = true;          // back-buffer drawing; flip atomically → zero tearing/flicker
  cfg.setPixelColorDepthBits(PANEL_COLOR_DEPTH);   // internal-DRAM budget — see config.h
  cfg.i2sspeed        = PANEL_I2S_HZ;      // pixel clock — see config.h
  cfg.min_refresh_rate = PANEL_MIN_REFRESH;
  dma = new CapturePanel(cfg);     // MatrixPanel_I2S_DMA + screenshot tee (capture.h)
  dma->begin();
  // What the library actually achieved: below PANEL_MIN_REFRESH it will have
  // spent colour depth to get there, and a low number here is exactly what
  // low-brightness shimmer looks like on the panel.
  Serial.printf("[panel] %d Hz scan @ %d bpp, %u MHz pixel clock\n",
                dma->calculated_refresh_rate, PANEL_COLOR_DEPTH,
                (unsigned)(PANEL_I2S_HZ / 1000000));
  heapReport("after dma->begin()");
  dma->setBrightness8(DAY_BRIGHTNESS);
  dma->fillScreen(0);
  dma->setTextSize(1);
  dma->setTextColor(C_TIME);
  dma->setCursor(1, 12);
  dma->print("HELLO");

  /* ---- filesystem + persisted config (may override WiFi/TG creds) ----
     NB: the fatflash partition scheme labels the data partition "ffat"
     (not the LittleFS default "spiffs") — pass the label explicitly or
     the mount fails and nothing (config/GIFs) ever persists. */
  if (!LittleFS.begin(true, "/littlefs", 10, "ffat"))
    Serial.println("LittleFS mount failed!");
  seedDefaultGifs();          // one-time copy of the embedded default pack
  loadEvents();
  loadTeams();                // favourite teams for the live-score widget
  loadConfig();
  dma->setBrightness8(userBrightness);

  /* ---- wifi (falls back to hotspot + on-screen QR portal) ---- */
  if (wifiConnect(20000)) {
    logLine("WiFi OK, IP " + WiFi.localIP().toString());
  } else {
    logLine("WiFi FAILED — starting setup hotspot");
    startPortal();
  }

  /* ---- time (IST) — SNTP keeps retrying once a network appears ---- */
  configTime(tzOffsetSec, 0, "pool.ntp.org", "time.google.com");
  if (WiFi.status() == WL_CONNECTED) {
    time_t now = 0;
    uint32_t t0 = millis();
    while (now < 8 * 3600 && millis() - t0 < 15000) { now = time(nullptr); delay(200); }
    localtime_r(&now, &tmNow);
    logLine("Time sync: " + String(now > 8 * 3600 ? "OK" : "FAILED"));
  }

  /* ---- telegram ---- */
  tgClient.setCACert(TELEGRAM_CERTIFICATE_ROOT);
  bot.updateToken(botToken);       // config.json may hold a newer token
  bot.longPoll = 0;                // never block the render loop
  logLine("Telegram ready, allowed chat_id=" + allowedChatId);

  /* ---- local dashboard: http://digiframe.local / http://192.168.4.1 ---- */
  setupWeb();
  if (WiFi.status() == WL_CONNECTED && MDNS.begin("digiframe"))
    MDNS.addService("http", "tcp", 80);

  /* ---- first weather ---- */
  if (WiFi.status() == WL_CONNECTED) fetchWeather();
  gif.begin(LITTLE_ENDIAN_PIXELS);

  /* ---- create mutexes and start background tasks on core 0 ---- */
  logMutex    = xSemaphoreCreateMutex();
  tgReqMutex  = xSemaphoreCreateMutex();
  sportsMutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(tgTask,      "telegram", 8192, NULL, 1, &tgTaskHandle,      0);
  xTaskCreatePinnedToCore(weatherTask, "weather",  4096, NULL, 1, &weatherTaskHandle, 0);
  xTaskCreatePinnedToCore(mqttTask,    "mqtt",     6144, NULL, 1, &mqttTaskHandle,    0);
  // 8192 rather than weather's 4096: a real score provider's JSON is far larger
  xTaskCreatePinnedToCore(sportsTask,  "sports",   8192, NULL, 1, &sportsTaskHandle,  0);

  heapReport("end of setup()");
  Serial.println("DigiFrame ready.");

  /* Boot diagnostics are worth waiting for; per-frame logging is not.
     Serial.print() blocks on the USB-CDC tx lock (default 100 ms, and
     longer still if a monitor is attached but not draining), so from here
     on a logLine() from any core can cost core 1 a frame. Cap the wait.

     Guarded because setTxTimeoutMs exists on HWCDC only. Built without
     CDCOnBoot=cdc, `Serial` is a UART HardwareSerial with no such method and
     no such problem — a UART write does not take that lock. Building that way
     is a legitimate choice, so it should not fail to compile. */
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(SERIAL_TX_TIMEOUT_MS);
#endif
  frameDueAt = millis();
}

/**********************  14. MAIN LOOP  *******************************/
/* The ESP32 core calls loop() back-to-back with no yield of its own, so
   this function IS core 1's scheduler: whatever it does every pass, it
   does tens of thousands of times a second. Hence the shape below —
   network servicing is throttled, rendering runs off the shared frame
   clock (globals.h), and the pass ends by giving the core back. */
void loop() {
  uint32_t ms = millis();

  /* --- network servicing, throttled (see NET_SERVICE_MS in config.h) ---
     handleClient() polls the listen socket and wifiManagerTick() calls
     into the WiFi driver; both take locks shared with tasks on core 0, and
     polling them at loop speed only buys contention. A few hundred Hz is
     imperceptible on a dashboard click. --- */
  static uint32_t lastNetAt = 0;
  if (ms - lastNetAt >= NET_SERVICE_MS) {
    lastNetAt = ms;
    web.handleClient();            // local dashboard / captive portal
    wifiManagerTick();             // portal DNS, STA retries, auto-recover
  }

  /* --- once per second: time, night mode, celebration trigger --- */
  if (ms - lastSecondAt >= 1000) {
    lastSecondAt = ms;
    time_t now = time(nullptr);
    localtime_r(&now, &tmNow);
    // dev overrides (POST /api/dev) — re-applied here because this tick and
    // weatherTask would otherwise overwrite them a second later
    if (devHour  >= 0) tmNow.tm_hour = devHour;
    if (devWCode >= 0) wCode = devWCode;

    // night mode (skipped during a celebration; setup QR must stay scannable).
    // Night is a *cap*, not a fixed level: it dims down to NIGHT_BRIGHTNESS,
    // but a lower manual setting still wins — so the slider works at night.
    if (mode != MODE_CELEBRATE && mode != MODE_SETUP) {
      bool night = (tmNow.tm_hour >= NIGHT_START_HR && tmNow.tm_hour < NIGHT_END_HR);
      uint8_t target = userBrightness;
      if (night && target > NIGHT_BRIGHTNESS) target = NIGHT_BRIGHTNESS;
      dma->setBrightness8(target);
    }

    // auto-start the celebration at 00:00 on a special day (not while in setup)
    String today = todayMMDD();
    if (!portalActive && today != lastCelebDate) {
      for (int i = 0; i < numEvents; i++) {
        if (events[i].date == today) {
          lastCelebDate = today;
          startCelebration(events[i].type, events[i].message);
          break;
        }
      }
    }
    // celebration day sanity: if an auto-started celebration is no longer
    // today's date (i.e. the day rolled over), return to clock.
    // lastCelebDate is only set by the auto-trigger, so a manual /celebrate
    // (lastCelebDate stays "") is never force-exited here.
    if (mode == MODE_CELEBRATE && lastCelebDate.length() && todayMMDD() != lastCelebDate) {
      closeGif(); mode = MODE_CLOCK;
      logLine("celebration auto-ended (date rolled over)");
    }

    // publish the latest score poll and decide whether the clock face shows
    // its ambient scene or the live-score card (sports_core.h)
    sportsTick();
  }

  /* --- weather every 20 min — handled by weatherTask on core 0 --- */

  /* --- telegram every 3 s — handled by tgTask on core 0 --- */

  /* --- consume a pending command posted from core 0 (Telegram or BLE) on
         this core, where LittleFS / DMA / openGif are safe. All the real
         work lives in control.h so HTTP + BLE + Telegram stay in sync. --- */
  if (tgReqReady && tgReqMutex && xSemaphoreTake(tgReqMutex, 0) == pdTRUE) {
    TgRequest req = tgReq;
    tgReqReady = false;
    tgReq.buf  = nullptr;          // ownership moved to local `req`
    xSemaphoreGive(tgReqMutex);
    switch (req.cmd) {
      case TGC_PLAY_GIF:   ctlPlayGif(req.strArg);                       break;
      case TGC_MSG:        ctlSendMsg(req.strArg, false);                break;
      case TGC_PIN:        ctlSendMsg(req.strArg, true);                 break;
      case TGC_STOP:       ctlStop();                                    break;
      case TGC_CELEBRATE:  (req.strArg.length() || req.strArg2.length())
                             ? startCelebration(req.strArg, req.strArg2)
                             : ctlCelebrate();                            break;
      case TGC_TEST:       startTest(req.strArg);                        break;
      case TGC_BRIGHTNESS: ctlSetBrightness(req.intArg);                 break;
      case TGC_DEL_GIF:    ctlDelGif(req.strArg);                        break;
      case TGC_INTERVAL:   ctlSetInterval(req.strArg.toInt());           break;
      case TGC_SET_WIFI:   ctlSetWifi(req.strArg, req.strArg2);          break;
      case TGC_SET_LOC:    ctlSetLoc(req.strArg, req.strArg2);           break;
      case TGC_SET_TG:     ctlSetTg(req.strArg, req.strArg2);            break;
      case TGC_TGTEST:     ctlTgTest();                                  break;
      case TGC_GIF_COMMIT:
        ctlCommitGif(req.strArg, req.intArg != 0, req.buf, req.bufLen);
        if (req.buf) free(req.buf);                                      break;
      case TGC_EVENT_ADD:  ctlAddEventJson(req.strArg);                  break;
      case TGC_EVENT_DEL:  ctlDelEvent(req.strArg);                     break;
      case TGC_SET_MQTT:   ctlSetMqttJson(req.strArg);                  break;
      case TGC_SET_TZ:     ctlSetTz(req.strArg.toInt());                break;
      case TGC_TEAM_ADD:   ctlAddTeamJson(req.strArg);                  break;
      case TGC_TEAM_DEL:   ctlDelTeam(req.strArg);                      break;
      case TGC_SET_SPORTS: ctlSetSportsJson(req.strArg);                break;
      case TGC_ESPN_CAT:   ctlSaveEspnCatalogue(req.intArg, req.strArg); break;
      default: break;
    }
  }

  /* --- frame clock: one phase-correcting deadline for every renderer ---
     Advancing by exact FRAME_MS steps means a slow pass costs one frame
     instead of permanently shifting the cadence; the resync guard stops a
     long stall from triggering a catch-up burst. See globals.h. --- */
  frameDue = false;
  if ((int32_t)(ms - frameDueAt) >= 0) {
    frameDue    = true;
    frameDueAt += FRAME_MS;
    if ((int32_t)(ms - frameDueAt) >= 0) frameDueAt = ms + FRAME_MS;  // stalled: resync
    frameNo++;                     // the animation step counter every renderer reads
  }

  /* --- render by mode --- */
  switch (mode) {
    case MODE_CLOCK: {
      if (frameDue) renderClock();
      // random character cameo: open the GIF and switch to MODE_GIF for one pass;
      // loop() drives playback naturally — no blocking busy-wait
      if (charEveryMs && ms - lastIdleAt > charEveryMs) {
        lastIdleAt = ms;
        String p = pickRandomChar();
        if (p.length() && openGif(p)) mode = MODE_GIF;  // returns to MODE_CLOCK when GIF ends
      }
      break;
    }
    case MODE_MSG:
      if (renderScroll(C_MSG)) panelPresent();
      if (msgEndsAt && ms > msgEndsAt) { mode = MODE_CLOCK; }
      break;
    case MODE_GIF:
      // Paced by the GIF's own inter-frame delay, not the frame clock —
      // playGifFrameIfDue() decodes without blocking the loop (gif_player.h).
      if (gifOpen) {
        int res = 1;
        if (playGifFrameIfDue(&res)) {
          panelPresent();
          int err = gif.getLastError();
          if (err != GIF_SUCCESS && err != GIF_EMPTY_FRAME) {
            // A GIF that can't be decoded would otherwise loop forever on a
            // black screen — report it once and return to the clock.
            // (GIF_EMPTY_FRAME is harmless — some GIFs have blank frames.)
            logLine("GIF decode error err=" + String(err) + " (" + currentGifPath + ") -> clock");
            closeGif();
            mode = MODE_CLOCK;
          } else if (res == 0) {
            if (gifIsUserPlay) {
              gif.reset();
            } else {
              closeGif();
              mode = MODE_CLOCK;
            }
          }
        }
      } else mode = MODE_CLOCK;
      break;
    case MODE_CELEBRATE:
      runCelebration();
      break;
    case MODE_TEST:
      runTest();
      break;
    case MODE_SETUP:
      renderSetupQR();             // static QR; redraws only when it changes
      break;
  }

  /* Give core 1 back between frames. Without this loop() never blocks, so
     core 1's idle task never runs and the loop spins at full speed doing
     nothing but re-checking deadlines — burning the core and hammering the
     locks the WiFi/lwIP side wants. One tick (1 ms) is far below FRAME_MS,
     so it costs no render latency; the phase-correcting deadline absorbs
     the sub-tick rounding. */
  if (!frameDue) vTaskDelay(1);
}
