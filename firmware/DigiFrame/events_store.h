/* DigiFrame — special days (/events.json) + persisted config (/config.json) */
#pragma once

/**********************  6. EVENTS (special days)  ********************/
/* A special day: a date, a TYPE that drives the celebration theme
   ("custom" -> fireworks, "birthday" -> cake + confetti), and a message. */
struct SpecialDay { String date; String type; String message; };   // date = "MM-DD"
#define MAX_EVENTS 12
SpecialDay events[MAX_EVENTS];
int numEvents = 0;

void saveEvents() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < numEvents; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["d"] = events[i].date;
    o["t"] = events[i].type;
    o["m"] = events[i].message;
  }
  File f = LittleFS.open("/events.json", "w");
  serializeJson(doc, f);
  f.close();
}
void loadEvents() {
  numEvents = 0;
  // No default events — this is a general-purpose clock. Users add their own.
  if (!LittleFS.exists("/events.json")) return;
  File f = LittleFS.open("/events.json", "r");
  JsonDocument doc;
  if (deserializeJson(doc, f) == DeserializationError::Ok) {
    for (JsonObject o : doc.as<JsonArray>()) {
      if (numEvents >= MAX_EVENTS) break;
      events[numEvents++] = {
        o["d"].as<String>(),
        o["t"].is<const char*>() ? o["t"].as<String>() : String("custom"),
        // "m" is the message; fall back to the legacy "n" key if present
        o["m"].is<const char*>() ? o["m"].as<String>()
          : (o["n"].is<const char*>() ? o["n"].as<String>() : String(""))
      };
    }
  }
  f.close();
}
String todayMMDD() {
  char b[6];
  snprintf(b, sizeof(b), "%02d-%02d", tmNow.tm_mon + 1, tmNow.tm_mday);
  return String(b);
}
// true if today matches a stored special day
bool isSpecialToday() {
  String t = todayMMDD();
  for (int i = 0; i < numEvents; i++)
    if (events[i].date == t) return true;
  return false;
}
// today's special day (type + message), or nullptr
SpecialDay *todaysEvent() {
  String t = todayMMDD();
  for (int i = 0; i < numEvents; i++)
    if (events[i].date == t) return &events[i];
  return nullptr;
}
// days until next event (searches up to 366 days ahead); -1 if none
int daysToNextEvent(String &nameOut) {
  if (numEvents == 0) return -1;
  time_t now = time(nullptr);
  for (int d = 0; d < 366; d++) {
    time_t t = now + (time_t)d * 86400;
    struct tm tmp;
    localtime_r(&t, &tmp);
    char b[6];
    snprintf(b, sizeof(b), "%02d-%02d", tmp.tm_mon + 1, tmp.tm_mday);
    for (int i = 0; i < numEvents; i++)
      if (events[i].date == String(b)) { nameOut = events[i].message; return d; }
  }
  return -1;
}

/**********************  6b. PERSISTED CONFIG  ************************/
void saveConfig() {
  JsonDocument d;
  d["charMin"] = charEveryMs / 60000UL;
  d["bright"]  = userBrightness;
  d["ssid"]    = cfgWifiSsid;
  d["pass"]    = cfgWifiPass;
  d["tgToken"] = botToken;
  d["tgChat"]  = allowedChatId;
  d["lat"]     = cfgLat;
  d["lon"]     = cfgLon;
  d["tz"]      = tzOffsetSec;
  d["mqttEn"]   = mqttEnable;
  d["mqttHost"] = mqttHost;
  d["mqttPort"] = mqttPort;
  d["mqttUser"] = mqttUser;
  d["mqttPass"] = mqttPass;
  d["sportEn"]   = sportEnable;
  d["sportSrc"]  = sportSrc;
  d["sportHold"] = sportHoldMin;
  d["sportFx"]   = sportFx;
  d["sportOn"]   = sportOnMask;
  d["sportRot"]  = sportRotSec;
  d["sportRef"]  = sportRefreshSec;
  d["orient"]    = cfgOrient;
  File f = LittleFS.open("/config.json", "w");
  serializeJson(d, f);
  f.close();
}
void loadConfig() {
  if (!LittleFS.exists("/config.json")) return;
  File f = LittleFS.open("/config.json", "r");
  JsonDocument d;
  if (deserializeJson(d, f) == DeserializationError::Ok) {
    if (d["charMin"].is<int>()) charEveryMs = (uint32_t)d["charMin"].as<int>() * 60000UL;
    if (d["bright"].is<int>())  userBrightness = constrain(d["bright"].as<int>(), 1, 255);
    if (d["ssid"].is<const char*>())    cfgWifiSsid   = d["ssid"].as<String>();
    if (d["pass"].is<const char*>())    cfgWifiPass   = d["pass"].as<String>();
    if (d["tgToken"].is<const char*>()) botToken      = d["tgToken"].as<String>();
    if (d["tgChat"].is<const char*>())  allowedChatId = d["tgChat"].as<String>();
    if (d["lat"].is<const char*>()) strlcpy(cfgLat, d["lat"], sizeof(cfgLat));
    if (d["lon"].is<const char*>()) strlcpy(cfgLon, d["lon"], sizeof(cfgLon));
    if (d["tz"].is<int>())               tzOffsetSec = d["tz"].as<int>();
    if (d["mqttEn"].is<bool>())          mqttEnable = d["mqttEn"].as<bool>();
    if (d["mqttHost"].is<const char*>()) mqttHost   = d["mqttHost"].as<String>();
    if (d["mqttPort"].is<int>())         mqttPort   = d["mqttPort"].as<int>();
    if (d["mqttUser"].is<const char*>()) mqttUser   = d["mqttUser"].as<String>();
    if (d["mqttPass"].is<const char*>()) mqttPass   = d["mqttPass"].as<String>();
    if (d["sportEn"].is<bool>())          sportEnable  = d["sportEn"].as<bool>();
    if (d["sportSrc"].is<const char*>())  sportSrc     = d["sportSrc"].as<String>();
    if (d["sportHold"].is<int>())         sportHoldMin = constrain(d["sportHold"].as<int>(), 0, 120);
    if (d["sportFx"].is<int>())           sportFx      = constrain(d["sportFx"].as<int>(), 0, 2);
    /* absent in files written before sports could be switched off: leave the
       all-ones default, which is the behaviour those files expect */
    if (d["sportOn"].is<int>())           sportOnMask  = (uint16_t)d["sportOn"].as<int>();
    if (d["sportRot"].is<int>())          sportRotSec  = constrain(d["sportRot"].as<int>(), 0, 300);
    if (d["sportRef"].is<int>())          sportRefreshSec = constrain(d["sportRef"].as<int>(), 0, 300);
    if (d["orient"].is<int>())            cfgOrient    = constrain(d["orient"].as<int>(), 0, 3);
  }
  f.close();
}

/**********************  6c. SAVED WIFI NETWORKS  *********************/
/* /wifi.json = [{s,p,b,c}] = ssid / pass / BSSID (12 hex chars, "" if
   unknown) / channel, most recently connected first. Entry 0 is mirrored
   into cfgWifiSsid/cfgWifiPass, and config.json keeps writing those as
   `ssid`/`pass`, so a firmware that predates this file still connects.

   The list operations take the table as arguments rather than reaching for
   knownNets, so /api/devtest can exercise ordering and eviction on a
   scratch copy without disturbing the networks the frame actually uses. */

static int netsFind(const KnownNet *t, uint8_t n, const char *ssid) {
  for (uint8_t i = 0; i < n; i++)
    if (!strcmp(t[i].ssid, ssid)) return i;
  return -1;
}

static void netsToFront(KnownNet *t, uint8_t i) {
  if (!i) return;
  KnownNet k = t[i];
  memmove(&t[1], &t[0], i * sizeof(KnownNet));
  t[0] = k;
}

/* Add or update a network and make it entry 0. A full table drops its last
   entry, which is the one connected to least recently. */
bool netsUpsertFront(KnownNet *t, uint8_t &n, const char *ssid, const char *pass) {
  if (!ssid || !*ssid || strlen(ssid) > 32 || strlen(pass) > 64) return false;
  int i = netsFind(t, n, ssid);
  if (i < 0) {
    if (n < WIFI_SAVED_MAX) n++;
    i = n - 1;
    memset(&t[i], 0, sizeof(KnownNet));
    strlcpy(t[i].ssid, ssid, sizeof(t[i].ssid));
  }
  strlcpy(t[i].pass, pass, sizeof(t[i].pass));
  netsToFront(t, i);
  return true;
}

bool netsForget(KnownNet *t, uint8_t &n, const char *ssid) {
  int i = netsFind(t, n, ssid);
  if (i < 0) return false;
  memmove(&t[i], &t[i + 1], (n - i - 1) * sizeof(KnownNet));
  n--;
  return true;
}

/* A connect succeeded: move that network to the front and remember where it
   was found. True only if anything changed, so a reconnect to the same AP
   costs no flash write. A network that is not in the table (forgotten while
   connected) is left alone. */
bool netsPromote(KnownNet *t, uint8_t n, const char *ssid,
                 const uint8_t *bssid, uint8_t ch) {
  int i = netsFind(t, n, ssid);
  if (i < 0) return false;
  bool changed = i != 0 || t[i].ch != ch || memcmp(t[i].bssid, bssid, 6);
  memcpy(t[i].bssid, bssid, 6);
  t[i].ch = ch;
  netsToFront(t, i);
  return changed;
}

static bool netHasBssid(const KnownNet &k) {
  static const uint8_t zero[6] = {0};
  return k.ch && memcmp(k.bssid, zero, 6);
}

/* Entry 0 is the network in use; keep the String mirror in step and tell
   the caller whether config.json needs rewriting for the downgrade path. */
static bool netsMirror() {
  String s = numKnownNets ? knownNets[0].ssid : "";
  String p = numKnownNets ? knownNets[0].pass : "";
  if (s == cfgWifiSsid && p == cfgWifiPass) return false;
  cfgWifiSsid = s;
  cfgWifiPass = p;
  return true;
}

void saveWifiNets() {
  JsonDocument d;
  JsonArray a = d.to<JsonArray>();
  for (uint8_t i = 0; i < numKnownNets; i++) {
    const KnownNet &k = knownNets[i];
    JsonObject o = a.add<JsonObject>();
    o["s"] = k.ssid;
    o["p"] = k.pass;
    char b[13] = "";
    if (netHasBssid(k))
      snprintf(b, sizeof(b), "%02x%02x%02x%02x%02x%02x",
               k.bssid[0], k.bssid[1], k.bssid[2], k.bssid[3], k.bssid[4], k.bssid[5]);
    o["b"] = b;
    o["c"] = k.ch;
  }
  File f = LittleFS.open("/wifi.json", "w");
  serializeJson(d, f);
  f.close();
  if (netsMirror()) saveConfig();
}

/* Call after loadConfig(): with no /wifi.json yet, the single network
   config.json (or config.h) already knew becomes the first saved one. */
void loadWifiNets() {
  numKnownNets = 0;
  if (!LittleFS.exists("/wifi.json")) {
    if (cfgWifiSsid.length() && cfgWifiSsid != "YOUR_WIFI_SSID")
      netsUpsertFront(knownNets, numKnownNets, cfgWifiSsid.c_str(), cfgWifiPass.c_str());
    return;
  }
  File f = LittleFS.open("/wifi.json", "r");
  JsonDocument d;
  if (deserializeJson(d, f) == DeserializationError::Ok) {
    for (JsonObject o : d.as<JsonArray>()) {
      if (numKnownNets >= WIFI_SAVED_MAX) break;
      const char *s = o["s"] | "";
      if (!*s || strlen(s) > 32) continue;
      KnownNet &k = knownNets[numKnownNets++];
      memset(&k, 0, sizeof(k));
      strlcpy(k.ssid, s, sizeof(k.ssid));
      strlcpy(k.pass, o["p"] | "", sizeof(k.pass));
      const char *b = o["b"] | "";
      if (strlen(b) == 12)
        for (int j = 0; j < 6; j++) {
          char h[3] = { b[2 * j], b[2 * j + 1], 0 };
          k.bssid[j] = (uint8_t)strtoul(h, nullptr, 16);
        }
      k.ch = (uint8_t)(o["c"] | 0);
    }
  }
  f.close();
  netsMirror();
}
