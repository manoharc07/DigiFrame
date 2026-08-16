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
  }
  f.close();
}
