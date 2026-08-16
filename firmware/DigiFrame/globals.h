/* DigiFrame — globals, cross-core command queue, background tasks, colors */
#pragma once

/**********************  3. GLOBALS  **********************************/
/* The panel is a CapturePanel (capture.h) — a MatrixPanel_I2S_DMA that can
   mirror what it draws into an RGB565 shadow for the /api/frame screenshot
   endpoint. It behaves exactly like the base class until capture is armed. */
CapturePanel *dma = nullptr;
AnimatedGIF gif;

/* One panel scan period in ms, derived from the refresh rate the library
   actually achieved at begin() (it may have traded colour depth for scan
   rate — see PANEL_I2S_HZ in config.h). On the ESP32-S3 flipDMABuffer()
   is NOT blocking: it only re-points the DMA descriptor chain, and the
   controller keeps scanning the buffer just handed over until it finishes
   the current pass. Anything that flips twice back-to-back must therefore
   wait this long in between, or the second draw lands on pixels still
   being displayed. */
static inline uint32_t panelScanPeriodMs() {
  int hz = dma ? dma->calculated_refresh_rate : 0;
  if (hz < 1) hz = 60;
  return (uint32_t)(1000 / hz) + 1;
}

/* Present the completed back buffer. Use this everywhere instead of calling
   dma->flipDMABuffer() directly: flipDMABuffer() is not virtual, so the
   screenshot shadow can only be kept in step through a single choke point. */
static inline void panelPresent() {
  dma->flipDMABuffer();
  dma->present();          // keep the /api/frame shadow in step (no-op when disarmed)
}
WiFiClientSecure tgClient;
UniversalTelegramBot bot(BOT_TOKEN, tgClient);

/* ---- runtime config: seeded from config.h defines, overridden by
        /config.json on LittleFS (editable from the web dashboard) ---- */
String cfgWifiSsid   = WIFI_SSID;
String cfgWifiPass   = WIFI_PASS;
String botToken      = BOT_TOKEN;
String allowedChatId = ALLOWED_CHAT_ID;
/* weather location — fixed char buffers (not String) because the web
   handler (core 1) writes while weatherTask (core 0) reads; a short
   strlcpy race is harmless, a String heap realloc race is not. */
char cfgLat[16]      = LATITUDE;
char cfgLon[16]      = LONGITUDE;
int  tzOffsetSec     = TZ_OFFSET_SEC;   // UTC offset in seconds (runtime-configurable)
/* ---- Home Assistant / MQTT (off by default; editable at runtime) ---- */
bool   mqttEnable = MQTT_ENABLE;
String mqttHost   = MQTT_HOST;
int    mqttPort   = MQTT_PORT;
String mqttUser   = MQTT_USER;
String mqttPass   = MQTT_PASS;
volatile bool mqttConfigDirty = false;  // set by web/BLE (core 1), applied by mqttTask (core 0)
/* ---- live scores (sports_core.h + the sport_*.h modules) ---- */
bool    sportEnable  = false;      // master switch for the score widget
String  sportSrc     = "demo";     // "demo" simulator | "http" real provider
int     sportHoldMin = 5;          // minutes the final score stays up after FT
uint8_t sportFx      = 1;          // event animations: 0 off, 1 major only, 2 every event
                                   // Default is major-only: the panel is a clock first, and
                                   // animating every yellow card and lead change turns a
                                   // glanceable score into television furniture. evIsMajor()
                                   // in sports_core.h defines the set.
/* Which sports may take the panel. A bitmask over SPORTS[], not a per-sport
   poll: following a sport enables the leagues and teams already followed under
   it (and orders the rotation), it does not go looking for matches by itself.
   All-ones default, so a /config.json written before this existed is unchanged. */
uint16_t sportOnMask = 0xFFFF;
/* Live refresh override in seconds. 0 = let each sport pick (SportModule.liveMs
   — basketball 10 s, a Test match 20 s). A single number here overrides them
   all, for when you would rather have one predictable tick than a sensible one.
   The floor is real: every request is held ESPN_MIN_GAP_MS apart, so a poll of
   N live matches cannot finish faster than ~4.8 s each, and sportsTask only
   wakes every 5 s. */
int      sportRefreshSec = 0;
uint32_t sportPollMs = 0;          // how long the last poll took, measured
int      sportRotSec = 30;         // seconds each live match holds the panel
                                   // before the next one gets a turn; 0 = no
                                   // rotation (most recently changed wins, the
                                   // behaviour before rotation existed)
/* An event animation is on screen right now. Mirrored out of score_widget.h
   (core 1, per frame) so the rotation in sports_core.h can decline to swap
   matches mid-effect — score_widget.h is included much later and owns the
   real flag. */
volatile bool scoreFxBusy  = false;
volatile bool sportsNow    = false;  // ask sportsTask for an immediate poll
volatile int  espnCatWanted = -1;    // sport index whose ESPN team catalogue core 1
                                     // wants refreshed; sportsTask (core 0) does the
                                     // fetch and hands the JSON back to be written
/* which half of the clock face is showing: the living scene or the score card */
enum ScoreSub : uint8_t { SUB_AMBIENT, SUB_SCORE };
volatile uint8_t clockSub  = SUB_AMBIENT;
volatile bool sceneNeedsReset = false;  // restart the ambient scene on re-entry
volatile bool weatherNow   = false;  // web handler asks for an immediate refetch
volatile bool tgTokenDirty = false;  // set by web handler (core 1), applied by tgTask (core 0)
bool          portalActive = false;  // setup hotspot + captive portal active
volatile bool wifiRetryNow = false;  // web handler asks for an immediate STA (re)connect

/* defined in later headers, called from the tasks below */
void handleTelegram();
void fetchWeather();
void fetchScores();                  // sports_core.h
void drawScoreWidget(uint32_t f);    // score_widget.h — called by renderClock()

/* ---- cross-core command queue: the Telegram task AND the BLE config
        task (both core 0) post here; the render loop (core 1) consumes it —
        this is the ONLY safe way for core-0 work to touch LittleFS / the DMA
        panel / openGif / saveConfig. New actions from either task must go
        through postTgCmd(), never call the ctl* functions directly. ---- */
enum TgCmd { TGC_NONE, TGC_PLAY_GIF,
             TGC_MSG, TGC_PIN, TGC_STOP, TGC_CELEBRATE, TGC_BRIGHTNESS, TGC_TEST,
             /* config actions added for the BLE dashboard (see control.h) */
             TGC_DEL_GIF, TGC_INTERVAL, TGC_SET_WIFI, TGC_SET_LOC,
             TGC_SET_TG, TGC_TGTEST, TGC_GIF_COMMIT,
             /* typed special-days + Home Assistant config (JSON in strArg) */
             TGC_EVENT_ADD, TGC_EVENT_DEL, TGC_SET_MQTT, TGC_SET_TZ,
             /* live scores: favourite teams + widget config (JSON in strArg) */
             TGC_TEAM_ADD, TGC_TEAM_DEL, TGC_SET_SPORTS,
             /* ESPN team catalogue fetched on core 0, written on core 1 */
             TGC_ESPN_CAT };
struct TgRequest {
  TgCmd    cmd     = TGC_NONE;
  String   strArg  = "";       // primary string (name / text / ssid / lat / token)
  String   strArg2 = "";       // secondary string (pass / lon / chat)
  uint8_t  intArg  = 0;        // brightness value, or GIF-upload pack flag
  uint8_t *buf     = nullptr;   // TGC_GIF_COMMIT: PSRAM buffer (core 1 frees it)
  size_t   bufLen  = 0;
};
volatile bool    tgReqReady = false;
TgRequest        tgReq;
SemaphoreHandle_t tgReqMutex = NULL;   // guards tgReq / tgReqReady

/* ---- background task handles (network work runs on core 0) ---- */
TaskHandle_t tgTaskHandle      = NULL;
TaskHandle_t weatherTaskHandle = NULL;
TaskHandle_t mqttTaskHandle    = NULL;
TaskHandle_t sportsTaskHandle  = NULL;
SemaphoreHandle_t logMutex     = NULL;   // guards logBuf / logHead / logSeq
SemaphoreHandle_t sportsMutex  = NULL;   // guards scoreBack / the event ring
void postTgCmd(TgCmd cmd, const String &str = "", uint8_t i = 0,
               const String &str2 = "", uint8_t *buf = nullptr, size_t buflen = 0) {
  if (!tgReqMutex) return;
  xSemaphoreTake(tgReqMutex, portMAX_DELAY);
  // single-slot queue: if an unconsumed GIF-upload commit is being
  // overwritten, free its buffer first so we don't leak PSRAM.
  if (tgReqReady && tgReq.cmd == TGC_GIF_COMMIT && tgReq.buf) free(tgReq.buf);
  tgReq.cmd     = cmd;
  tgReq.strArg  = str;
  tgReq.strArg2 = str2;
  tgReq.intArg  = i;
  tgReq.buf     = buf;
  tgReq.bufLen  = buflen;
  tgReqReady    = true;
  xSemaphoreGive(tgReqMutex);
}

enum Mode { MODE_CLOCK, MODE_MSG, MODE_GIF, MODE_CELEBRATE, MODE_TEST, MODE_SETUP };
Mode mode = MODE_CLOCK;

/* ---- frame clock (core 1) -------------------------------------------
   Every renderer used to carry its own `millis() - last > 66` gate — six
   of them, in scene/party/scroll. Independent gates on a free-running
   loop() drift out of phase with each other, and because each one resets
   its reference to *now* after firing, any slow pass (an HTTP request, a
   blocking CDC write, a GIF decode) permanently shifts that renderer's
   cadence instead of being corrected. Accumulated phase error is exactly
   what stutter looks like on a panel that is otherwise scanning fine.

   One deadline now paces the whole render side. It advances in exact
   FRAME_MS steps so error cannot accumulate, and only resyncs to wall
   time after a stall longer than a full frame (so a hiccup drops one
   frame rather than triggering a catch-up burst). `frameDue` is true for
   exactly one loop() pass per frame — renderers test it instead of
   calling millis(), and `frameNo` is incremented centrally in loop(). */
#define FRAME_MS (1000UL / RENDER_FPS)
uint32_t frameDueAt = 0;
bool     frameDue   = false;

/* GIF playback is paced by the GIF's own inter-frame delay, not by the
   frame clock, so it gets its own deadline (set by openGif / loop()). */
uint32_t gifNextFrameAt = 0;

/* ---- dev overrides, driven by POST /api/dev (see web_portal.h) ----
   The ambient scene branches on the hour and on the weather code, so
   without these the only scene reachable is whatever the real clock and
   sky happen to be doing. Both are re-applied every second in loop()
   because the second tick and weatherTask would otherwise overwrite them;
   -1 means "no override". ctlStop() clears them. */
int      devHour        = -1;         // spoof tmNow.tm_hour (0-23)
int      devWCode       = -1;         // spoof the Open-Meteo weather code
int      devSavedWCode  = -1;         // real wCode, restored when the override lifts
/* Hold a forced score card on screen. sportsDemoForce() writes scoreFront
   directly, but sportsTick() copies scoreBack over it a second later — which
   is why /test only works by returning early from sportsTick. This is that
   same escape hatch for /api/dev, which forces a card from MODE_CLOCK. */
volatile bool sportsFreeze = false;

String   qrLastText     = "";         // setup-QR text; cleared to force a repaint (qr_display.h)
String   scrollText     = "";
int      scrollX        = PANEL_W;
uint32_t msgEndsAt      = 0;          // millis when /msg expires (0 = pinned)
String   currentGifPath = "";
bool     gifOpen        = false;
bool     gifIsUserPlay  = false;   // true = /play or /celebrate, false = cameo (plays once)
File     fsGifFile;

/* ---- celebration (special-day) mode: merged party + special days ---- */
String   lastCelebDate  = "";         // "MM-DD" already celebrated today
String   celebMsg       = "";
String   celebType      = "custom";   // "custom" | "birthday" — drives the visual theme
uint8_t  celebPhase     = 0;          // 0 = visual, 1 = message banner
uint32_t celebPhaseAt   = 0;

/* ---- /test mode: cycles through every screen type ---- */
uint8_t  testStep       = 0;
uint32_t testStepAt     = 0;
int      testSavedWCode = -1;
String   testChat       = "";

float    wTemp          = NAN;
int      wCode          = -1;
uint32_t lastWeatherAt  = 0;
uint32_t lastBotAt      = 0;
uint32_t lastSecondAt   = 0;
uint32_t lastIdleAt     = 0;
uint8_t  userBrightness = DAY_BRIGHTNESS;
struct tm tmNow;

WebServer web(80);                    // local dashboard at http://digiframe.local

/* ---- in-memory log ring buffer, viewable from the dashboard ---- */
#define LOG_LINES 40
String   logBuf[LOG_LINES];
int      logHead = 0;
uint32_t logSeq  = 0;
void logLine(const String &s) {
  uint32_t sec = millis() / 1000;
  char ts[16];
  snprintf(ts, sizeof(ts), "[%lu:%02lu] ", (unsigned long)(sec / 60), (unsigned long)(sec % 60));
  if (logMutex) xSemaphoreTake(logMutex, portMAX_DELAY);
  logBuf[logHead] = String(ts) + s;
  logHead = (logHead + 1) % LOG_LINES;
  logSeq++;
  if (logMutex) xSemaphoreGive(logMutex);
  Serial.println(s);                  // also to Serial if a cable is attached
}
File     webUpload;
uint32_t charEveryMs    = 10UL * 60000UL;   // random character cameo interval

/* ---- background task: Telegram polling on core 0 ---- */
void tgTask(void *pv) {
  for (;;) {
    if (tgTokenDirty) {                 // token changed on the dashboard —
      tgTokenDirty = false;             // apply it here so only this task
      bot.updateToken(botToken);        // ever touches the bot client
      logLine("TG token updated");
    }
    if (WiFi.status() == WL_CONNECTED) {
      static uint32_t lastBeat = 0;
      uint32_t ms = millis();
      if (ms - lastBeat > 60000) {
        lastBeat = ms;
        logLine("poll heartbeat (WiFi up, heap " + String(ESP.getFreeHeap() / 1024) + "KB)");
      }
      handleTelegram();
    } else {
      static uint32_t lastWarn = 0;
      if (millis() - lastWarn > 30000) {
        lastWarn = millis();
        logLine("WiFi DOWN — Telegram paused");
      }
    }
    vTaskDelay(pdMS_TO_TICKS(3000));
  }
}

/* ---- background task: weather fetch on core 0 ---- */
void weatherTask(void *pv) {
  vTaskDelay(pdMS_TO_TICKS(2000));        // let setup() finish first
  uint32_t lastFetch = 0;
  for (;;) {
    if (WiFi.status() == WL_CONNECTED &&
        (weatherNow || !lastFetch || millis() - lastFetch > 20UL * 60000UL)) {
      weatherNow = false;
      fetchWeather();
      lastFetch = millis();
    }
    vTaskDelay(pdMS_TO_TICKS(5000));      // short tick so weatherNow applies fast
  }
}

/**********************  4. COLORS  ***********************************/
#define C_TIME   dma->color565(255, 179, 222)   // pastel pink
#define C_TEMP   dma->color565(173, 216, 255)   // pastel blue
#define C_DATE   dma->color565(200, 190, 255)   // lavender
#define C_MSG    dma->color565(255, 210, 150)   // warm peach
#define C_ACCENT dma->color565(255,  80, 120)    // neutral highlight (seconds head, sparkles)
