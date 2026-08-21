/* DigiFrame — shared control layer (one implementation per action) */
#pragma once

/**********************  10b. CONTROL LAYER  **************************/
/* Every config / command action lives here exactly once, so the two
 * front-ends stay in lock-step and can't drift:
 *   - the local HTTP dashboard (web_portal.h) runs on core 1 and calls
 *     these ctl* functions directly;
 *   - the Telegram bot (telegram.h) and the Home Assistant MQTT client
 *     (mqtt_ha.h) run on core 0 and must marshal to core 1 via
 *     postTgCmd() — loop() then calls the ctl*.
 * ALL of these run on core 1: they touch LittleFS / the DMA panel /
 * openGif / saveConfig, none of which are safe from a core-0 task. */

void ctlSendMsg(const String &text, bool pin) {
  scrollText = text;
  scrollText.toUpperCase();
  scrollX = PANEL_W;
  closeGif();
  mode      = MODE_MSG;
  msgEndsAt = pin ? 0 : (millis() + MSG_MINUTES * 60000UL);
}

void ctlSetBrightness(int v) {
  userBrightness = constrain(v, 1, 255);
  dma->setBrightness8(userBrightness);
  saveConfig();
}

bool ctlPlayGif(const String &name) {          // name = "foo.gif" or "/foo.gif"
  String p = name.startsWith("/") ? name : "/" + name;
  if (openGif(p, true)) { mode = MODE_GIF; return true; }
  return false;
}

bool ctlDelGif(const String &name) {
  String p = name.startsWith("/") ? name : "/" + name;
  return LittleFS.remove(p);
}

void ctlSetInterval(int minutes) {
  charEveryMs = (minutes <= 0) ? 0 : (uint32_t)minutes * 60000UL;
  saveConfig();
}

void ctlCelebrate() {                          // celebrate today's special day, else a generic one
  SpecialDay *e = todaysEvent();
  if (e) startCelebration(e->type, e->message);
  else   startCelebration("custom", "");       // startCelebration supplies a default banner
}

/* ---- special days (date + type + message) ---- */
bool ctlAddEvent(const String &date, const String &type, const String &message) {
  String d = date; d.trim();
  if (d.length() != 5 || d[2] != '-') return false;       // "MM-DD"
  String t = (type == "birthday") ? "birthday" : "custom";
  for (int i = 0; i < numEvents; i++)                     // update an existing date in place
    if (events[i].date == d) { events[i] = { d, t, message }; saveEvents(); logLine("event updated " + d); return true; }
  if (numEvents >= MAX_EVENTS) return false;
  events[numEvents++] = { d, t, message };
  saveEvents();
  logLine("event added " + d + " (" + t + ")");
  return true;
}
bool ctlDelEvent(const String &date) {
  String d = date; d.trim();
  for (int k = 0; k < numEvents; k++)
    if (events[k].date == d) {
      for (int j = k; j < numEvents - 1; j++) events[j] = events[j + 1];
      numEvents--;
      saveEvents();
      logLine("event deleted " + d);
      return true;
    }
  return false;
}
String ctlListEventsJson() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < numEvents; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["date"]    = events[i].date;
    o["type"]    = events[i].type;
    o["message"] = events[i].message;
  }
  String out; serializeJson(doc, out); return out;
}
/* BLE passes the whole event as JSON {date,type,message} */
bool ctlAddEventJson(const String &json) {
  JsonDocument d;
  if (deserializeJson(d, json)) return false;
  return ctlAddEvent(d["date"] | "", d["type"] | "custom", d["message"] | "");
}

/* ---- Home Assistant / MQTT config ---- */
void ctlSetMqtt(bool en, const String &host, int port, const String &user, const String &pass) {
  mqttEnable = en;
  mqttHost = host; mqttHost.trim();
  if (port > 0) mqttPort = port;
  mqttUser = user;
  if (pass.length()) mqttPass = pass;          // keep existing if the field is left blank
  saveConfig();
  mqttConfigDirty = true;                       // mqttTask reconnects on core 0
  logLine("MQTT config updated (enable=" + String(en ? "yes" : "no") + ", host=" + mqttHost + ")");
}
void ctlSetMqttJson(const String &json) {
  JsonDocument d;
  if (deserializeJson(d, json)) return;
  ctlSetMqtt(d["enable"] | false, d["host"] | "", d["port"] | 1883, d["user"] | "", d["pass"] | "");
}

/* ---- live scores: favourite teams + widget config ----
   Team ids are only unique within a sport ("eng" is both a cricket and a rugby
   side), so anything addressing a favourite from outside uses "sportKey/id". */
static int favIndexOf(const String &sportKey, const String &teamId) {
  int s = sportIndexByKey(sportKey.c_str());
  if (s < 0) return -1;
  for (int i = 0; i < numFavTeams; i++)
    if (favTeams[i].sport == (uint8_t)s && favTeams[i].id == teamId) return i;
  return -1;
}
bool ctlAddTeam(const String &sportKey, const String &teamId) {
  int s = sportIndexByKey(sportKey.c_str());
  if (s < 0) return false;
  const SportModule *mod = SPORTS[s];
  const TeamEntry *te = nullptr;
  for (uint8_t i = 0; i < mod->numTeams; i++)
    if (teamId == mod->catalogue[i].id) { te = &mod->catalogue[i]; break; }
  if (!te) return false;
  if (favIndexOf(sportKey, teamId) >= 0) return true;      // already following
  if (numFavTeams >= MAX_FAV_TEAMS) return false;
  favTeams[numFavTeams++] = { String(te->id), String(te->name), String(te->abbr),
                              (uint8_t)s, String(""), String("") };
  saveTeams();
  demoBuilt = false;                     // rebuild the demo feed around it
  sportsNow = true;
  logLine("following " + String(te->name) + " (" + mod->label + ")");
  return true;
}
/* key is "sportKey/teamId" */
bool ctlDelTeam(const String &key) {
  int slash = key.indexOf('/');
  if (slash < 0) return false;
  int k = favIndexOf(key.substring(0, slash), key.substring(slash + 1));
  if (k < 0) return false;
  String gone = favTeams[k].name;
  for (int j = k; j < numFavTeams - 1; j++) favTeams[j] = favTeams[j + 1];
  numFavTeams--;
  saveTeams();
  demoBuilt = false;
  sportsNow = true;
  logLine("unfollowed " + gone);
  return true;
}
/* ---- ESPN team catalogue (espn_api.h) ----
   Written here rather than in the fetcher because the fetch runs on core 0,
   which must never touch LittleFS. Core 0 parses and posts; this runs on
   core 1 and does the write. */
void ctlSaveEspnCatalogue(uint8_t sportIdx, const String &json) {
  if (sportIdx >= NUM_SPORTS) return;
  File f = LittleFS.open(espnCataloguePath(sportIdx), "w");
  if (!f) { logLine("ESPN catalogue: write failed"); return; }
  f.print(json);
  f.close();
}

/* The cached list for one sport, or "[]" plus a background refresh request. */
String ctlEspnTeamsJson(const String &sportKey) {
  int s = sportIndexByKey(sportKey.c_str());
  if (s < 0) return "[]";
  String path = espnCataloguePath((uint8_t)s);
  if (!LittleFS.exists(path)) { espnCatWanted = s; sportsNow = true; return "[]"; }
  File f = LittleFS.open(path, "r");
  String out = f.readString();
  f.close();
  return out.length() ? out : String("[]");
}

void ctlRefreshEspnCatalogue(const String &sportKey) {
  int s = sportIndexByKey(sportKey.c_str());
  if (s < 0) return;
  espnCatWanted = s;
  sportsNow = true;
  logLine("ESPN catalogue refresh queued: " + sportKey);
}

/* Follow an ESPN team. Unlike ctlAddTeam this does not need a catalogue
   entry — the id, name, abbreviation and league come from ESPN's own search,
   which is the only way to follow a side the hardcoded tables never knew
   about. `league` may be empty (cricket has none, and discovery there goes
   through the personalized header anyway). */
bool ctlAddEspnTeam(const String &sportKey, const String &espnId,
                    const String &name, const String &abbr, const String &league) {
  int s = sportIndexByKey(sportKey.c_str());
  if (s < 0 || !espnId.length()) return false;
  for (int i = 0; i < numFavTeams; i++)
    if (favTeams[i].sport == (uint8_t)s && favTeams[i].espn == espnId) return true;
  if (numFavTeams >= MAX_FAV_TEAMS) return false;
  favTeams[numFavTeams++] = { espnId, name.length() ? name : abbr,
                              abbr.length() ? abbr : String("?"),
                              (uint8_t)s, espnId, league };
  saveTeams();
  demoBuilt = false;
  sportsNow = true;
  logLine("following " + (name.length() ? name : abbr) + " (ESPN " + espnId +
          (league.length() ? " / " + league : String("")) + ")");
  return true;
}

/* Follow a whole league. `league` is ESPN's slug or numeric series id
   ("eng.1", "270559") as picked in the browser from ESPN's own league list. */
bool ctlFollowLeague(const String &sportKey, const String &league, const String &name) {
  int s = sportIndexByKey(sportKey.c_str());
  if (s < 0 || !league.length()) return false;
  for (int i = 0; i < numFavTeams; i++)
    if (favTeams[i].sport == (uint8_t)s && followIsLeague(favTeams[i]) &&
        favTeams[i].id == league) return true;
  if (numFavTeams >= MAX_FOLLOWS) return false;
  String nm = name.length() ? name : league;
  /* abbr is what the dashboard list shows; a league has no 3-letter form, so
     take the slug's leading segment ("eng.1" -> "ENG") */
  String ab = league.substring(0, league.indexOf('.') > 0 ? league.indexOf('.') : 4);
  ab.toUpperCase();
  favTeams[numFavTeams++] = { league, nm, ab, (uint8_t)s, String(""), league, FOLLOW_LEAGUE };
  saveTeams();
  sportsNow = true;
  logLine("following league " + nm + " (" + league + ")");
  return true;
}

/* Which sports may take the panel. Not a poll of its own: it gates the
   leagues and teams already followed under that sport. */
void ctlSetSportEnabled(uint8_t sportIdx, bool on) {
  if (sportIdx >= NUM_SPORTS) return;
  if (on) sportOnMask |=  (uint16_t)(1u << sportIdx);
  else    sportOnMask &= (uint16_t)~(1u << sportIdx);
  saveConfig();
  sportsNow = true;
  logLine(String(SPORTS[sportIdx]->label) + (on ? " on" : " off"));
}
/* 0 = each sport picks its own; otherwise one tick for all of them.
   Clamped at 5 s because sportsTask only wakes that often. */
void ctlSetRefresh(int secs) {
  sportRefreshSec = secs <= 0 ? 0 : constrain(secs, 5, 300);
  saveConfig();
  sportsNow = true;
  logLine("score refresh " + String(sportRefreshSec ? String(sportRefreshSec) + "s"
                                                    : String("per sport")));
}
void ctlSetRotate(int secs) {
  sportRotSec = constrain(secs, 0, 300);
  saveConfig();
  logLine("score rotation " + String(sportRotSec ? String(sportRotSec) + "s" : "off"));
}

/* Put a specific live match on the panel. Everything needed comes from the
   caller — the dashboard reads it off ESPN's scoreboard in the browser — so
   the frame skips discovery entirely and starts ticking on the next poll. */
bool ctlPinMatch(const String &sportKey, const String &league, const String &eventId,
                 const String &homeId, const String &awayId) {
  int s = sportIndexByKey(sportKey.c_str());
  if (s < 0 || !eventId.length() || !homeId.length() || !awayId.length()) return false;
#if ESPN_ENABLE
  espnSetPin((uint8_t)s, league.c_str(), eventId.c_str(), homeId.c_str(), awayId.c_str());
#endif
  strlcpy(scorePinned, eventId.c_str(), sizeof(scorePinned));
  if (!sportEnable) { sportEnable = true; saveConfig(); }   // or it can never show
  sportsNow = true;
  logLine("pinned event " + eventId + " (" + sportKey + ")");
  return true;
}
void ctlUnpin() {
  if (!scorePinned[0]) return;
  scorePinned[0] = 0;
#if ESPN_ENABLE
  espnClearPin();
#endif
  sportsNow = true;
  logLine("pin cleared");
}

bool ctlAddTeamJson(const String &json) {
  JsonDocument d;
  if (deserializeJson(d, json)) return false;
  return ctlAddTeam(d["sport"] | "", d["id"] | "");
}
String ctlListTeamsJson() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < numFavTeams; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["key"]   = String(SPORTS[favTeams[i].sport]->key) + "/" + favTeams[i].id;
    o["sport"] = SPORTS[favTeams[i].sport]->label;
    o["name"]  = favTeams[i].name;
    o["abbr"]  = favTeams[i].abbr;
    /* The dashboard shows these so a favourite that will never go live is
       visible as such: no espn id means the demo feed only, and a missing
       league is why an otherwise-correct team never gets discovered. */
    o["espn"]  = favTeams[i].espn;
    o["lg"]    = favTeams[i].league;
    o["kind"]  = followIsLeague(favTeams[i]) ? "league" : "team";
    o["si"]    = favTeams[i].sport;
    o["on"]    = sportIsOn(favTeams[i].sport);
  }
  String out; serializeJson(doc, out); return out;
}
/* the whole registry, so the dashboard dropdown needs no hardcoded lists —
   this is what makes "add a sport" a one-file job */
String ctlCatalogueJson() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (uint8_t s = 0; s < NUM_SPORTS; s++) {
    JsonObject o = arr.add<JsonObject>();
    o["key"]   = SPORTS[s]->key;
    o["label"] = SPORTS[s]->label;
    /* ESPN's own sport slug, so the dashboard can map a search result onto a
       sport without a second hardcoded table — "add a sport" stays one file. */
    o["espn"]  = SPORTS[s]->espnSport;
    o["dl"]    = SPORTS[s]->espnLeague;    // default league, for the picker
    o["on"]    = sportIsOn(s);             // sport favourite: may it take the panel
    JsonArray t = o["teams"].to<JsonArray>();
    for (uint8_t i = 0; i < SPORTS[s]->numTeams; i++) {
      JsonObject e = t.add<JsonObject>();
      e["id"]   = SPORTS[s]->catalogue[i].id;
      e["name"] = SPORTS[s]->catalogue[i].name;
    }
    JsonArray ev = o["fx"].to<JsonArray>();      // every animation this sport can fire
    for (uint8_t i = 0; i < SPORTS[s]->numEvents; i++)
      ev.add(SPORTS[s]->events[i].native);
  }
  String out; serializeJson(doc, out); return out;
}
void ctlSetSports(bool en, const String &src, int holdMin, int fx) {
  sportEnable  = en;
  if (src.length()) sportSrc = src;
  sportHoldMin = constrain(holdMin, 0, 120);
  sportFx      = (uint8_t)constrain(fx, 0, 2);
  saveConfig();
  sportsNow = true;
  logLine("live scores " + String(en ? "on" : "off") + " (source " + sportSrc + ")");
}
void ctlSetSportsJson(const String &json) {
  JsonDocument d;
  if (deserializeJson(d, json)) return;
  ctlSetSports(d["enable"] | false, d["src"] | "", d["hold"] | 5, d["fx"] | 2);
}
void ctlScorePreview(bool on) {
  scorePreview = on;
  demoBuilt = false;
  sportsNow = true;
  logLine(String("score preview ") + (on ? "on" : "off"));
}

void ctlStop() {
  if (mode == MODE_TEST) wCode = testSavedWCode;  // restore spoofed weather
  if (scorePreview) ctlScorePreview(false);
  ctlUnpin();                    // "back to clock" also means "stop showing that match"
  // lift any /api/dev scene overrides too, so "stop" always means "back to
  // the real clock" no matter which front end put the panel where it is
  if (devWCode >= 0 && devSavedWCode >= 0) wCode = devSavedWCode;
  devHour = devWCode = devSavedWCode = -1;
  sportsFreeze = false;          // let real polls drive the score card again
  closeGif();
  mode = MODE_CLOCK;
}

bool ctlSetLoc(const String &lat, const String &lon) {
  String la = lat; la.trim();
  String lo = lon; lo.trim();
  float flat = la.toFloat(), flon = lo.toFloat();
  if (!la.length() || !lo.length() || flat < -90 || flat > 90 || flon < -180 || flon > 180)
    return false;
  strlcpy(cfgLat, la.c_str(), sizeof(cfgLat));
  strlcpy(cfgLon, lo.c_str(), sizeof(cfgLon));
  saveConfig();
  weatherNow = true;                           // weatherTask refetches right away
  logLine("location updated -> " + la + "," + lo);
  return true;
}

bool ctlSetWifi(const String &ssid, const String &pass) {
  String s = ssid; s.trim();
  if (!s.length()) return false;
  cfgWifiSsid = s;
  cfgWifiPass = pass;
  saveConfig();
  wifiRetryNow = true;                         // wifiManagerTick() reconnects on core 1
  logLine("WiFi creds updated -> '" + cfgWifiSsid + "'");
  return true;
}

void ctlSetTg(const String &token, const String &chat) {
  String tk = token; tk.trim();
  String ch = chat;  ch.trim();
  if (tk.length()) { botToken = tk; tgTokenDirty = true; }  // tgTask applies the token
  if (ch.length()) allowedChatId = ch;
  saveConfig();
  logLine("Telegram config updated (chat_id=" + allowedChatId + ")");
}

void ctlSetTz(int seconds) {
  tzOffsetSec = constrain(seconds, -12 * 3600, 14 * 3600);
  configTime(tzOffsetSec, 0, "pool.ntp.org", "time.google.com");  // re-apply offset live
  saveConfig();
  logLine("timezone set: UTC" + String(tzOffsetSec >= 0 ? "+" : "") + String(tzOffsetSec / 3600.0, 2) + "h");
}

void ctlTgTest() {
  if (WiFi.status() != WL_CONNECTED) { logLine("tgtest: no WiFi"); return; }
  logLine("tgtest: sending to " + allowedChatId + " ...");
  bool ok = bot.sendMessage(allowedChatId, "DigiFrame test message", "");
  logLine("tgtest: sendMessage returned " +
          String(ok ? "true (check your phone)" : "FALSE — token/chat_id bad"));
}

/* Write an uploaded GIF (buffered by the BLE task) to LittleFS. Runs on
 * core 1 via TGC_GIF_COMMIT; the caller frees the buffer afterwards. */
bool ctlCommitGif(const String &name, bool pack, const uint8_t *buf, size_t len) {
  if (!buf || !len) return false;
  String nm = name; nm.replace(" ", "_");
  if (!nm.endsWith(".gif")) nm += ".gif";
  if (pack && !nm.startsWith("c_")) nm = "c_" + nm;
  if (!nm.startsWith("/")) nm = "/" + nm;
  File f = LittleFS.open(nm, "w");
  if (!f) { logLine("BLE GIF upload FAILED to open " + nm); return false; }
  size_t w = f.write(buf, len);
  f.close();
  logLine("BLE GIF upload: " + nm + " (" + String(len / 1024) + " KB) " +
          (w == len ? "ok" : "SHORT WRITE"));
  return w == len;
}

/* ---- read-only views shared by HTTP + BLE (must run on core 1) ---- */

String ctlListGifsJson() {
  String out = "[";
  File root = LittleFS.open("/");
  File f = root.openNextFile();
  bool first = true;
  while (f) {
    String nm = f.name();
    if (nm.endsWith(".gif")) {
      if (!first) out += ",";
      out += "\"" + nm + "\"";
      first = false;
    }
    f = root.openNextFile();
  }
  out += "]";
  return out;
}

/* ---- one-click firmware update -----------------------------------------
   The dashboard did the checking (browser side, see config.h) and hands over
   the release tag and the app-image URL it found. All that is decided here is
   whether to accept them; loop() does the work on the next pass, because
   updateInstall() stops servicing sockets and this call still owes the
   browser a reply.

   Returns the reason it refused, or "" if the install is queued.        */
String ctlUpdateStart(const String &url, const String &tag, uint32_t size) {
  if (updBusy || updInstallNow)   return "an update is already running";
  if (!updUrlAllowed(url))        return "url is not a " UPDATE_REPO " release asset";
  if (url.length() >= sizeof(updUrl)) return "url too long";
  strlcpy(updUrl, url.c_str(), sizeof(updUrl));
  strlcpy(updTag, tag.length() ? tag.c_str() : "update", sizeof(updTag));
  updSize   = size;
  updErr[0] = ' ';
  updInstallNow = true;
  logLine("update queued: " + String(updTag));
  return "";
}

String ctlStatusJson() {
  String tk = botToken;                        // token is masked for display
  if (tk.length() > 10) tk = tk.substring(0, 6) + "..." + tk.substring(tk.length() - 4);
  JsonDocument d;
  d["ssid"]     = cfgWifiSsid;
  d["chat"]     = allowedChatId;
  d["token"]    = tk;
  d["lat"]      = cfgLat;
  d["lon"]      = cfgLon;
  d["tz"]       = tzOffsetSec;
  d["bright"]   = userBrightness;
  d["interval"] = charEveryMs / 60000UL;
  d["mode"]     = (int)mode;
  d["heap"]     = ESP.getFreeHeap() / 1024;
  /* What the browser needs to run the update check itself: the version this
     build calls itself, and which repo/asset to look for. Serving the repo
     rather than hardcoding it in the page keeps a fork working unchanged. */
  d["fw"]       = FW_VERSION;
  d["updRepo"]  = UPDATE_REPO;
  d["updAsset"] = UPDATE_ASSET_SUFFIX;
  d["updBusy"]  = (bool)updBusy || (bool)updInstallNow;
  d["updErr"]   = updErr;
  d["ip"]       = WiFi.localIP().toString();
  d["wifi"]     = (WiFi.status() == WL_CONNECTED)
                    ? "connected, IP " + WiFi.localIP().toString()
                    : String(portalActive ? "hotspot mode — enter your WiFi above"
                                          : "disconnected");
  d["mqttEn"]   = mqttEnable;
  d["mqttHost"] = mqttHost;
  d["mqttPort"] = mqttPort;
  d["mqttUser"] = mqttUser;
  d["sportEn"]   = sportEnable;
  d["sportSrc"]  = sportSrc;
  d["sportHold"] = sportHoldMin;
  d["sportFx"]   = sportFx;
  d["sportPrev"] = scorePreview;
  d["sportRot"]  = sportRotSec;
  d["sportRef"]  = sportRefreshSec;
  d["sportPoll"] = (int)sportPollMs;
  d["sportMask"] = sportOnMask;
  d["sportPin"]  = scorePinned;       // event id the user pinned, or ""
  d["sportEv"]     = evLastNative;
  d["sportEvAge"]  = evLastAt ? (int)((millis() - evLastAt) / 1000) : -1;
  d["sportEvN"]    = evLastCount;
  d["sportOn"]   = (clockSub == SUB_SCORE);
  d["sportAge"]  = lastScoreAt ? (int)((millis() - lastScoreAt) / 1000) : -1;
  d["sportErr"]  = sportLastErr;
  {
    const LiveMatch *am = activeMatchPtr();
    d["sportNow"] = am ? String(am->home.abbr) + " " + am->home.score + "-" +
                         am->away.score + " " + am->away.abbr
                       : String("");
  }
  /* What the poll actually produced. Without this, a match that is fetched and
     parsed but then rejected by sportsEligible() is indistinguishable from one
     that was never fetched — which is exactly the failure that cost the most
     time to find. The dashboard shows it when nothing is on the panel. */
  {
    static const char *ST[] = {"upcoming", "live", "break", "ended", "suspended"};
    JsonArray dbg = d["sportDbg"].to<JsonArray>();
    for (uint8_t i = 0; i < numFront; i++) {
      const LiveMatch &m = scoreFront[i];
      JsonObject o = dbg.add<JsonObject>();
      o["id"]    = m.id;
      o["sport"] = SPORTS[m.sport]->key;
      o["state"] = ST[m.state % 5];
      o["fav"]   = m.home.fav || m.away.fav;
      o["s"]     = String(m.home.abbr) + " " + m.home.score + "-" +
                   m.away.score + " " + m.away.abbr;
      /* the recent-events strip as characters — cricket's last six deliveries.
         Reading it here beats decoding pill colours out of a panel capture,
         where the black digit splits every pill into two coloured runs. */
      o["strip"] = m.strip;
    }
  }
  String out;
  serializeJson(d, out);
  return out;
}

String ctlLogsText() {
  String out;
  if (logMutex) xSemaphoreTake(logMutex, portMAX_DELAY);
  for (int i = 0; i < LOG_LINES; i++) {
    String &l = logBuf[(logHead + i) % LOG_LINES];
    if (l.length()) out += l + "\n";
  }
  if (logMutex) xSemaphoreGive(logMutex);
  if (!out.length()) out = "(no logs yet)";
  return out;
}
