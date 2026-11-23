/* DigiFrame — runtime HUB75 panel wiring, with a trial boot that reverts */
#pragma once

/**********************  6d. PANEL WIRING  ****************************/
/* The 14 HUB75 pins plus the options that also differ between boards
   (driver chip, clock phase, latch blanking) used to be compile-time only.
   On an all-in-one ESP32 matrix board those pins are fixed by the PCB, so a
   board that differs from config.h needed a recompile. They now come from
   /panel.json, read in setup() BEFORE dma->begin() — the panel cannot be
   re-pinned once it is running, which is why every change is a reboot.

   A wrong pin usually means a dark panel, and a dark panel cannot show the
   user what went wrong. So a new wiring is never simply saved; it is TRIED:

     /panel_try.json     written by the dashboard, then the frame reboots
     /panel_trying.json  the try, renamed at boot — "this boot is the trial"
     /panel.json         the confirmed wiring (absent = config.h defaults)

   At boot: a leftover /panel_trying.json means the last trial was never
   confirmed — delete it and boot on /panel.json. Otherwise a /panel_try.json
   is renamed to /panel_trying.json and used for this boot only. Keep renames
   it to /panel.json; Revert deletes it and reboots; PANEL_TRIAL_MS without
   either reverts on its own. Every state is decided from which files exist,
   with no counters, so a power cut at any point lands somewhere sane — and
   the user's recovery from a dark panel is just to power-cycle it.

   Deliberately NOT on Telegram or MQTT: wiring is set up once, at the frame,
   and a remote command that can darken the panel has no place on a chat bot. */

#define PANEL_PIN_COUNT 14
static const char *const PANEL_PIN_NAMES[PANEL_PIN_COUNT] = {
  "R1", "G1", "B1", "R2", "G2", "B2", "A", "B", "C", "D", "E", "LAT", "OE", "CLK"
};
/* The HUB75 library's shift_driver enum, in its order. */
static const char *const PANEL_DRIVERS[] = {
  "SHIFTREG", "FM6124", "FM6126A", "ICN2038S", "MBI5124", "DP3246"
};
#define PANEL_DRIVER_COUNT (sizeof(PANEL_DRIVERS) / sizeof(PANEL_DRIVERS[0]))

/* GPIOs a HUB75 line may use on THIS build (ESP32-S3, octal PSRAM, USB-CDC
   console). Everything else is refused with its reason (panelGpioWhy):
     0, 45, 46   strapping pins — sampled at reset, the board may not boot
     19, 20      USB D-/D+ — the serial console and flashing port
     22-25       do not exist on the ESP32-S3
     26-32       the SPI flash the firmware runs from
     33-37       octal PSRAM (N16R8 / N8R8 modules)
     43, 44      UART0 TX/RX
   GPIO 3 is a strapping pin too, but only selects the JTAG source and is
   harmless at runtime, so it stays usable. */
static const uint8_t PANEL_GPIO_OK[] = {
  1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
  21, 38, 39, 40, 41, 42, 47, 48
};

struct PanelCfg {
  int8_t  pins[PANEL_PIN_COUNT];   // order of PANEL_PIN_NAMES
  uint8_t drv;                     // index into PANEL_DRIVERS
  bool    clkph;                   // HUB75_I2S_CFG::clkphase
  uint8_t latblk;                  // HUB75_I2S_CFG::latch_blanking, 1-4
};

PanelCfg panelActive;                  // what dma->begin() was given this boot
bool     panelTrial       = false;     // this boot is an unconfirmed trial
uint32_t panelTrialEndsAt = 0;         // millis() at which it reverts itself
uint32_t panelRebootAt    = 0;         // loop() restarts at this millis(); 0 = no

static PanelCfg panelDefaults() {
  PanelCfg c = { { R1_PIN, G1_PIN, B1_PIN, R2_PIN, G2_PIN, B2_PIN, A_PIN, B_PIN,
                   C_PIN, D_PIN, E_PIN, LAT_PIN, OE_PIN, CLK_PIN },
                 PANEL_DRIVER, PANEL_CLKPHASE, PANEL_LATCH_BLANKING };
  return c;
}

/* nullptr if `p` may carry a HUB75 line, else why not */
static const char *panelGpioWhy(int p) {
  for (uint8_t ok : PANEL_GPIO_OK) if (ok == p) return nullptr;
  if (p == 0 || p == 45 || p == 46) return "is a strapping pin";
  if (p == 19 || p == 20)           return "is USB";
  if (p >= 22 && p <= 25)           return "does not exist on the ESP32-S3";
  if (p >= 26 && p <= 32)           return "is the flash chip";
  if (p >= 33 && p <= 37)           return "is the octal PSRAM";
  if (p == 43 || p == 44)           return "is UART0";
  return "is not a GPIO";
}

/* "" if the whole set is usable, else the first problem, in words */
String panelCfgCheck(const PanelCfg &c) {
  for (int i = 0; i < PANEL_PIN_COUNT; i++) {
    if (const char *why = panelGpioWhy(c.pins[i]))
      return String(PANEL_PIN_NAMES[i]) + ": GPIO " + c.pins[i] + " " + why;
    for (int j = 0; j < i; j++)
      if (c.pins[j] == c.pins[i])
        return String(PANEL_PIN_NAMES[i]) + " and " + PANEL_PIN_NAMES[j] +
               " are both GPIO " + c.pins[i];
  }
  if (c.drv >= PANEL_DRIVER_COUNT) return "unknown driver";
  if (c.latblk < 1 || c.latblk > 4) return "latch blanking must be 1-4";
  return "";
}

/* JSON -> cfg. Missing fields keep what `c` already holds, so a caller can
   start from the active wiring and change one pin. */
String panelCfgFromJson(JsonVariantConst j, PanelCfg &c) {
  JsonArrayConst p = j["pins"].as<JsonArrayConst>();
  if (!p.isNull()) {
    if (p.size() != PANEL_PIN_COUNT) return "pins needs " + String(PANEL_PIN_COUNT) + " entries";
    for (int i = 0; i < PANEL_PIN_COUNT; i++) {
      if (!p[i].is<int>()) return String(PANEL_PIN_NAMES[i]) + ": not a number";
      c.pins[i] = (int8_t)constrain(p[i].as<int>(), -1, 99);
    }
  }
  if (j["drv"].is<int>())     c.drv    = (uint8_t)constrain(j["drv"].as<int>(), 0, 255);
  if (j["clkph"].is<bool>())  c.clkph  = j["clkph"].as<bool>();
  if (j["latblk"].is<int>())  c.latblk = (uint8_t)constrain(j["latblk"].as<int>(), 0, 255);
  return panelCfgCheck(c);
}

void panelCfgToJson(const PanelCfg &c, JsonObject o) {
  JsonArray p = o["pins"].to<JsonArray>();
  for (int i = 0; i < PANEL_PIN_COUNT; i++) p.add(c.pins[i]);
  o["drv"]    = c.drv;
  o["clkph"]  = c.clkph;
  o["latblk"] = c.latblk;
}

/* A file that is missing, unreadable or fails validation is treated as
   absent: a hand-edited /panel.json with a typo boots on the defaults rather
   than on a pin that could keep the board from starting. */
static bool panelCfgRead(const char *path, PanelCfg &c) {
  if (!LittleFS.exists(path)) return false;
  File f = LittleFS.open(path, "r");
  JsonDocument d;
  bool ok = f && deserializeJson(d, f) == DeserializationError::Ok;
  if (f) f.close();
  if (!ok) { logLine(String("panel: ") + path + " unreadable - ignored"); return false; }
  PanelCfg t = panelDefaults();
  String err = panelCfgFromJson(d.as<JsonVariantConst>(), t);
  if (err.length()) { logLine(String("panel: ") + path + " invalid (" + err + ") - ignored"); return false; }
  c = t;
  return true;
}

static bool panelCfgWrite(const char *path, const PanelCfg &c) {
  JsonDocument d;
  panelCfgToJson(c, d.to<JsonObject>());
  File f = LittleFS.open(path, "w");
  if (!f) return false;
  bool ok = serializeJson(d, f) > 0;
  f.close();
  return ok;
}

/* setup(): decide this boot's wiring. Needs LittleFS mounted, nothing else. */
void panelCfgBoot() {
  panelActive = panelDefaults();
  panelCfgRead("/panel.json", panelActive);       // keeps the defaults if absent
  if (LittleFS.exists("/panel_trying.json")) {     // last boot was a trial nobody kept
    LittleFS.remove("/panel_trying.json");
    logLine("panel: new wiring was not confirmed - reverted");
    return;
  }
  if (LittleFS.exists("/panel_try.json")) {
    LittleFS.rename("/panel_try.json", "/panel_trying.json");   // consumed: one boot only
    PanelCfg t;
    if (panelCfgRead("/panel_trying.json", t)) {
      panelActive      = t;
      panelTrial       = true;
      panelTrialEndsAt = millis() + PANEL_TRIAL_MS;
      logLine("panel: TRIAL of new wiring - confirm on the dashboard");
    } else {
      LittleFS.remove("/panel_trying.json");
    }
  }
}

/* ---- the actions behind /api/panel* (called on core 1) ---- */

/* Validate, stage as the next boot's trial, schedule the reboot. Returns ""
   or the reason it was refused. The reboot is deferred so the HTTP reply
   reaches the browser first (loop() acts on panelRebootAt). */
String panelApply(const PanelCfg &c) {
  String err = panelCfgCheck(c);
  if (err.length()) return err;
  if (!panelCfgWrite("/panel_try.json", c)) return "could not write /panel_try.json";
  LittleFS.remove("/panel_trying.json");   // a fresh trial replaces a running one
  logLine("panel: new wiring staged - rebooting to try it");
  panelRebootAt = millis() + 1500;
  return "";
}

bool panelKeep() {
  if (!panelTrial) return false;
  LittleFS.remove("/panel.json");
  if (!LittleFS.rename("/panel_trying.json", "/panel.json")) {
    panelCfgWrite("/panel.json", panelActive);          // belt and braces
    LittleFS.remove("/panel_trying.json");
  }
  panelTrial = false;
  if (mode == MODE_PANELTEST) mode = MODE_CLOCK;
  logLine("panel: new wiring kept");
  return true;
}

void panelRevert(const char *why) {
  LittleFS.remove("/panel_trying.json");
  LittleFS.remove("/panel_try.json");
  logLine(String("panel: reverting wiring (") + why + ") - rebooting");
  panelRebootAt = millis() + 1500;
}

/* loop(), once a second: the trial's own deadline */
void panelTick() {
  if (panelTrial && !panelRebootAt && (int32_t)(millis() - panelTrialEndsAt) >= 0)
    panelRevert("not confirmed in time");
}

int panelTrialLeftSec() {
  if (!panelTrial) return 0;
  int32_t left = (int32_t)(panelTrialEndsAt - millis());
  return left > 0 ? left / 1000 : 0;
}

/* ---- the test pattern shown while a trial runs ----
   Built to diagnose wiring by eye, since panelshot captures the image the
   firmware meant to draw, not what the wires made of it:
     R G B W bars   a swapped or dead colour line shows as a wrong/black bar
     1px border     a missing address line (often E) shows as missing or
                    doubled rows, and the edges make a shifted image obvious
     corner block   top-left, so a mirrored or rotated panel is plain
     KEEP?          the question the dashboard banner is asking */
void renderPanelTest() {
  dma->fillScreen(0);
  const uint16_t W = dma->color565(255, 255, 255);
  dma->drawFastHLine(0, 0, PANEL_W, W);
  dma->drawFastHLine(0, PANEL_H - 1, PANEL_W, W);
  dma->drawFastVLine(0, 0, PANEL_H, W);
  dma->drawFastVLine(PANEL_W - 1, 0, PANEL_H, W);
  dma->fillRect(2, 2, 5, 5, dma->color565(255, 0, 255));           // top-left marker
  static const uint8_t RGBW[4][3] = { {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255} };
  static const char *LBL[4] = { "R", "G", "B", "W" };
  for (int i = 0; i < 4; i++) {
    int x = 4 + i * 14;
    dma->fillRect(x, 10, 12, 28, dma->color565(RGBW[i][0], RGBW[i][1], RGBW[i][2]));
    drawText(x + 3, 40, LBL[i], 1, W);
  }
  const char *q = tr("panel.keepq");
  drawText((PANEL_W - textWidth(q, 1)) / 2, 52, q, 1, W);
}
