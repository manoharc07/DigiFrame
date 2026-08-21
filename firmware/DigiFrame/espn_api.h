/* DigiFrame — live scores from the ESPN public API.
 *
 * The provider behind sportsPoll()'s "http" source. Runs entirely on core 0
 * inside sportsTask; writes nothing but the `out` array the caller hands it,
 * so the cross-core contract in sports_core.h is untouched.
 *
 * WHY PLAIN HTTP: verified 2026-08-15 that site.api.espn.com and
 * sports.core.api.espn.com both answer on port 80 with no redirect. That
 * matters more than it looks — a TLS session costs ~32 KB of internal DRAM
 * and this board runs with ~39 KB free once WiFi and the panel are up (see
 * PANEL_COLOR_DEPTH in config.h). Going through https here would have meant
 * a third concurrent session alongside Telegram and weather. Scores are
 * public read-only data; the worst a MITM can do is show a wrong score.
 *
 * WHY THREE CADENCES: the obvious approach — poll the scoreboard every
 * 15 s — is not viable. The NFL scoreboard measured 281 KB (16 events,
 * `odds` alone is 6.6 KB per event and cannot be excluded). At 15 s that is
 * ~1.6 GB/day from one household IP. So:
 *
 *   catalogue   /teams per league        ~135 KB   weekly, cached to flash
 *   discovery   /teams/{id} -> nextEvent  ~25 KB   lazily, per followed team
 *   live        core score x2 + status     ~830 B  every 15 s
 *
 * Discovery is what makes this cheap: `team.nextEvent[]` returns the event
 * id, kickoff time, both competitor ids AND the league slug, so we learn
 * exactly what to watch and when, then sleep until it starts. It is also
 * league-agnostic — a cup fixture showed up through the league team endpoint,
 * and the core API resolved that event under either league slug.
 *
 * EVENTS ARE DERIVED. The cheap endpoints carry scores, not a play feed, so
 * a score delta is mapped onto a native event name by the sport module's
 * eventForDelta() and reported through simReport() exactly as the simulator
 * does. Everything above (the ring, the diffing, the 68 animations) is
 * unchanged and cannot tell the difference.
 *
 * EVERY PARSE FAILS SOFT. Returning false leaves the previous good poll in
 * scoreBack untouched, so a schema change degrades to a stale card, never a
 * blank one and never a crash.
 */
#pragma once

#if ESPN_ENABLE

/* ---- per-followed-team discovery cache -----------------------------
   Parallel to favTeams[], not inside LiveMatch: a fixture we are waiting
   on has no LiveMatch yet, and this must survive a match ending. */
struct EspnWatch {
  char     eventId[16];
  char     league[24];      // slug or numeric id, from nextEvent.league.slug
  char     homeComp[12];    // competitor ids, for the core score URLs
  char     awayComp[12];
  bool     homeIsFav;
  uint32_t kickoffUtc;      // epoch seconds; 0 = unknown
  uint32_t checkedAt;       // millis of the last discovery for this team
  uint8_t  lastState;       // MatchState from the last successful live fetch
  bool     valid;
  /* cricket only: the rolling last-six-deliveries strip. Held here rather than
     in LiveMatch because the provider rebuilds LiveMatch from scratch on every
     poll, while the strip has to survive between the slower ball fetches. */
  char     strip[8];
  uint16_t ballPage;        // cached pageCount, self-correcting
  uint32_t ballsAt;         // millis of the last ball fetch
  /* The score the strip was last fetched against — runs/wickets/overs of both
     sides, exactly what the card prints. The strip refreshes when this moves,
     not on a timer of its own; see espnCricketStrip(). */
  char     ballSig[24];
};
static EspnWatch espnWatch[MAX_FAV_TEAMS];

static uint32_t espnLastReqAt   = 0;    // enforces ESPN_MIN_GAP_MS
static uint32_t espnBackoffMs   = 0;    // grows on failure, cleared on success

/* ---- HTTP ----------------------------------------------------------

   ArduinoJson pulls its input ONE BYTE AT A TIME, and each read() on a
   WiFiClient is a socket call. Parsing straight from http.getStream() is
   therefore pathologically slow on anything large: the 93 KB cricket header
   did not finish in seven minutes and looked exactly like a hang. Wrapping
   the socket in a 512-byte buffer is the difference between a parse that
   completes in a second and one that never does.

   It also has to DECODE CHUNKED TRANSFER-ENCODING itself. HTTPClient only
   unchunks inside getString()/writeToStream(); read the stream yourself and
   the hex chunk-size lines are still in the bytes. That is not a corner
   case here — measured 2026-08-15, /teams sends Content-Length but the
   personalized cricket header AND every core-API live endpoint send
   chunked, so without this the 15 s live tick never parses at all. */
class EspnBufferedStream : public Stream {
 public:
  EspnBufferedStream(WiFiClient &src, bool chunked)
      : _src(src), _chunked(chunked) {}

  int available() override { return _done ? 0 : 1; }   // parser only needs "maybe"
  int read() override {
    if (!ready()) return -1;
    if (_chunked) _chunkLeft--;
    return _buf[_pos++];
  }
  int peek() override { return ready() ? _buf[_pos] : -1; }
  void flush() override {}
  size_t write(uint8_t) override { return 0; }

 private:
  /* one raw byte, buffered */
  bool fill() {
    if (_pos < _len) return true;
    _pos = _len = 0;
    uint32_t t0 = millis();
    while (millis() - t0 < (uint32_t)ESPN_HTTP_TIMEOUT) {
      int avail = _src.available();
      if (avail > 0) {
        size_t want = (size_t)avail < sizeof(_buf) ? (size_t)avail : sizeof(_buf);
        _len = _src.readBytes(_buf, want);
        if (_len) return true;
      }
      if (!_src.connected() && _src.available() <= 0) return false;
      vTaskDelay(1);                            // yield: this is a core-0 task
    }
    return false;                               // timed out waiting for data
  }
  int rawRead() { return fill() ? _buf[_pos++] : -1; }

  /* "1f4a\r\n" or "1f4a;ext\r\n" -> 8010, consuming the terminator */
  long readChunkSize() {
    long v = 0; int c; bool got = false, inExt = false;
    while ((c = rawRead()) >= 0) {
      if (c == '\r') { if (rawRead() != '\n') return -1; return got ? v : -1; }
      if (inExt) continue;
      if (c == ';') { inExt = true; continue; }
      int d = (c >= '0' && c <= '9') ? c - '0'
            : (c >= 'a' && c <= 'f') ? c - 'a' + 10
            : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
      if (d < 0) return -1;
      v = v * 16 + d; got = true;
    }
    return -1;
  }

  /* make sure at least one payload byte is buffered and in-chunk */
  bool ready() {
    if (_done) return false;
    if (!_chunked) { if (fill()) return true; _done = true; return false; }
    while (_chunkLeft == 0) {
      if (_started && (rawRead() != '\r' || rawRead() != '\n')) { _done = true; return false; }
      long sz = readChunkSize();
      _started = true;
      if (sz <= 0) { _done = true; return false; }   // 0 = last chunk, <0 = malformed
      _chunkLeft = (size_t)sz;
    }
    if (fill()) return true;
    _done = true;
    return false;
  }

  WiFiClient &_src;
  uint8_t     _buf[512];
  size_t      _len = 0, _pos = 0;
  bool        _chunked, _started = false, _done = false;
  size_t      _chunkLeft = 0;
};


/* One GET, parsed straight from the stream through an ArduinoJson filter.
   Never getString(): a 281 KB body would not fit anywhere on this board,
   and with a filter the document stays a few hundred bytes regardless of
   how large the response is. */
static bool espnGet(const String &url, JsonDocument &doc, JsonDocument &filter) {
  if (WiFi.status() != WL_CONNECTED) { sportLastErr = "wifi down"; return false; }

  // politeness floor between any two requests, whatever called us
  uint32_t since = millis() - espnLastReqAt;
  if (since < ESPN_MIN_GAP_MS) vTaskDelay(pdMS_TO_TICKS(ESPN_MIN_GAP_MS - since));
  espnLastReqAt = millis();

  /* One socket, kept open across requests. A poll hits the same host three or
     four times in a row and a fresh TCP handshake measured 0.25-0.43 s of the
     0.5-0.75 s each request took — reuse halves it, and it is strictly LESS
     load on ESPN than reconnecting every time. Safe as a static because only
     sportsTask (core 0) ever reaches this function; HTTPClient drops and
     redials by itself when the host changes. */
  static WiFiClient net;               // plain — see the header note
  HTTPClient  http;
  http.setTimeout(ESPN_HTTP_TIMEOUT);
  http.setConnectTimeout(ESPN_HTTP_TIMEOUT);
  http.setUserAgent(ESPN_USER_AGENT);
  http.setReuse(true);
  if (!http.begin(net, url)) { sportLastErr = "http begin failed"; return false; }

  int code = http.GET();
  if (code != 200) {
    sportLastErr = "http " + String(code);
    http.end();
    // 429/403 mean we are being told to slow down; back off hard and grow it
    if (code == 429 || code == 403) {
      espnBackoffMs = espnBackoffMs ? min(espnBackoffMs * 2, 1800000UL) : 60000UL;
      logLine("ESPN " + String(code) + " — backing off " + String(espnBackoffMs / 1000) + "s");
    }
    return false;
  }

  /* NestingLimit must be raised well above ArduinoJson's default of 10: a
     filter still walks the whole document to know what to skip, and ESPN
     nests deeper than that (sports[].leagues[].teams[].team.logos[].rel[]
     is 12 levels). Without this every /teams parse fails with TooDeep. */
  /* getSize() < 0 means chunked; the stream has to unframe it itself. */
  EspnBufferedStream in(net, http.getSize() < 0);
  DeserializationError err = deserializeJson(doc, in,
                                             DeserializationOption::Filter(filter),
                                             DeserializationOption::NestingLimit(24));
  http.end();
  if (err) { sportLastErr = String("json: ") + err.c_str(); return false; }
  espnBackoffMs = 0;
  return true;
}

/* ---- helpers ------------------------------------------------------- */

/* "e31837" -> RGB565. ESPN omits the colour for many teams (most cricket
   sides), so an empty or unparsable value falls back to the sport accent;
   teamInk() then floors it for legibility. */
static uint16_t espnColor(const char *hex, uint16_t fallback) {
  if (!hex || strlen(hex) < 6) return fallback;
  char buf[7];
  strlcpy(buf, hex[0] == '#' ? hex + 1 : hex, sizeof(buf));
  char *end = nullptr;
  long v = strtol(buf, &end, 16);
  if (end == buf) return fallback;
  return RGB565((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

/* "2026-08-21T19:00Z" -> epoch seconds. ESPN always emits UTC with a Z. */
static uint32_t espnParseUtc(const char *iso) {
  if (!iso || strlen(iso) < 17) return 0;
  struct tm t = {};
  if (sscanf(iso, "%4d-%2d-%2dT%2d:%2d", &t.tm_year, &t.tm_mon, &t.tm_mday,
             &t.tm_hour, &t.tm_min) != 5) return 0;
  t.tm_year -= 1900; t.tm_mon -= 1;
  time_t v = mktime(&t);                      // mktime is local; correct it
  return (v == (time_t)-1) ? 0 : (uint32_t)(v - tzOffsetSec);
}

/* ESPN state string -> MatchState */
/* `desc` is status.type.description and is NOT optional for cricket: at stumps
   ESPN reports state "in" with detail "Live" and only description says
   "Stumps". Searching detail alone — which is what this did — left a Test
   match reading as in-progress overnight, so the card sat on the panel until
   morning. Both strings are searched now; whichever carries the word wins. */
static uint8_t espnState(const char *state, const char *detail, const char *desc = nullptr) {
  if (!state) return MS_UPCOMING;
  if (!strcmp(state, "post")) return MS_ENDED;
  if (!strcmp(state, "pre"))  return MS_UPCOMING;

  /* Case-INSENSITIVE, because ESPN writes these as prose as often as labels:
     the live example that caught this was "Match delayed by a wet outfield",
     where a case-sensitive search for "Delay" misses and a stopped match reads
     as in progress. strcasestr is not portable here, so fold by hand. */
  /* Whole words only. A bare substring test is far too loose on free text:
     "tea" matches "Team" and "Steal", "rain" matches "Bahrain" — each of which
     would park the card on the clock mid-play. Require a non-alphanumeric
     boundary on both sides, which still matches "Match delayed - Rain" and
     multi-word needles like "bad light". */
  auto isWord = [](char c) { return isalnum((unsigned char)c) != 0; };
  auto has = [&](const char *hay, const char *needle) {
    if (!hay || !needle) return false;
    for (const char *p = hay; *p; p++) {
      const char *h = p, *n = needle;
      while (*n && *h && tolower((unsigned char)*h) == tolower((unsigned char)*n)) { h++; n++; }
      if (*n) continue;                                   // no match here
      if (p != hay && isWord(p[-1])) continue;            // "Bahrain" vs "rain"
      if (*h && isWord(*h))          continue;            // "Team"    vs "tea"
      return true;
    }
    return false;
  };
  auto says = [&](const char *needle) {
    return has(detail, needle) || has(desc, needle);
  };
  /* Play has stopped for the day or indefinitely: release the panel. */
  if (says("stumps") || says("bad light") || says("rain") ||
      says("suspend") || says("delay") || says("abandon") ||
      says("wet outfield"))
    return MS_SUSPENDED;
  /* A pause you sit through with the score still up. */
  if (says("half") || says("end of") || says("interm") ||
      says("lunch") || says("tea") || says("drinks") || says("innings"))
    return MS_BREAK;
  return MS_LIVE;
}

/* Cricket scores arrive preformatted: "305 & 128/3 (26 ov)". Take the last
   innings (after any '&'), then runs before '/' and wickets after it. The
   overs in parentheses become the period so cricketBody's layout is fed the
   same shape the simulator gives it. */
static void espnCricketScore(const char *s, int16_t &runs, int16_t &wkts, char *overs, size_t oversLen) {
  runs = 0; wkts = SCORE2_NONE;               // NONE = "has not batted yet"
  if (!s || !*s) return;                      // leave overs alone: see below
  const char *p = strrchr(s, '&');            // last innings of a Test
  p = p ? p + 1 : s;
  while (*p == ' ') p++;
  runs = (int16_t)atoi(p);
  const char *slash = strchr(p, '/');
  const char *paren = strchr(p, '(');
  if (slash && (!paren || slash < paren)) wkts = (int16_t)atoi(slash + 1);
  else                                   wkts = 10;
  /* An innings with no "/" is all out — ESPN writes a completed innings as
     bare runs ("426"). Reporting SCORE2_NONE there made cricketBody print
     "yet" (not batted) for a side that had actually scored 426. */

  /* Overs are only written when this side's score carries them, so a side
     with no "(53 ov)" leaves the buffer as the caller set it. The caller now
     passes a per-side buffer and picks the batting side's from linescores —
     sharing one buffer here is what captioned a chase with the first innings'
     over count. */
  if (paren && overs && oversLen) {
    int i = 0;
    for (const char *q = paren + 1; *q && *q != ' ' && *q != ')' && i < (int)oversLen - 1; q++)
      overs[i++] = *q;                        // "53" from "(53 ov)"
    overs[i] = 0;
  }
}

/* ---- filters -------------------------------------------------------
   Each keeps only the handful of fields we use. Without these the NFL team
   response alone would need ~25 KB of document; with them it needs ~400 B. */
static void espnFilterTeam(JsonDocument &f) {
  JsonObject t = f["team"].to<JsonObject>();
  t["id"] = true; t["abbreviation"] = true; t["displayName"] = true; t["color"] = true;
  JsonObject ne = t["nextEvent"][0].to<JsonObject>();
  ne["id"] = true; ne["date"] = true;
  ne["league"]["slug"] = true;
  JsonObject c = ne["competitions"][0].to<JsonObject>();
  JsonObject cc = c["competitors"][0].to<JsonObject>();
  cc["id"] = true; cc["homeAway"] = true;
  cc["team"]["id"] = true; cc["team"]["abbreviation"] = true;
  cc["team"]["displayName"] = true; cc["team"]["color"] = true;
}
static void espnFilterScore(JsonDocument &f)  { f["value"] = true; f["displayValue"] = true; }
static void espnFilterStatus(JsonDocument &f) {
  f["period"] = true; f["displayClock"] = true;
  JsonObject t = f["type"].to<JsonObject>();
  t["state"] = true; t["detail"] = true; t["shortDetail"] = true;
  t["description"] = true; t["completed"] = true;
}

/* ---- discovery ------------------------------------------------------
   One followed team per call. Fills espnWatch[i] with everything the live
   tick needs, so the live tick never has to touch a big endpoint. */
/* Cricket has no team endpoint (400) and its "leagues" are per-tour series
   whose ids change every few weeks, so neither half of the normal discovery
   works. The personalized header lists every active series with its events
   in one call, which finds a followed side whatever tour it is on and hands
   back the series id the live tick needs. Heavier than a team fetch (~93 KB
   filtered down to a few hundred bytes), but it runs on the lazy cadence. */
static bool espnDiscoverCricket(int favIdx) {
  const FavTeam &fav = favTeams[favIdx];
  EspnWatch &w = espnWatch[favIdx];
  w.checkedAt = millis();

  JsonDocument filter;
  JsonObject lg = filter["sports"][0]["leagues"][0].to<JsonObject>();
  lg["id"] = true;
  JsonObject ev = lg["events"][0].to<JsonObject>();
  ev["id"] = true; ev["date"] = true;
  ev["status"] = true;
  JsonObject c = ev["competitors"][0].to<JsonObject>();
  c["id"] = true; c["homeAway"] = true;

  JsonDocument doc;
  if (!espnGet("http://site.api.espn.com/apis/personalized/v2/scoreboard/header"
               "?sport=cricket&region=in", doc, filter)) return false;

  for (JsonObject league : doc["sports"][0]["leagues"].as<JsonArray>()) {
    for (JsonObject e : league["events"].as<JsonArray>()) {
      const char *state = e["status"] | "";
      if (strcmp(state, "in") && strcmp(state, "pre")) continue;   // skip finished
      for (JsonObject cm : e["competitors"].as<JsonArray>()) {
        if (strcmp(cm["id"] | "", fav.espn.c_str())) continue;
        strlcpy(w.eventId, e["id"] | "", sizeof(w.eventId));
        strlcpy(w.league,  league["id"] | "", sizeof(w.league));
        w.homeIsFav  = !strcmp(cm["homeAway"] | "", "home");
        w.kickoffUtc = espnParseUtc(e["date"] | "");
        w.homeComp[0] = w.awayComp[0] = 0;      // cricket polls the series board
        w.valid = w.eventId[0] && w.league[0];
        if (w.valid)
          logLine("ESPN " + fav.abbr + ": ev " + w.eventId + " (series " + w.league + ")");
        return true;
      }
    }
  }
  w.valid = false;
  return true;
}

static bool espnDiscover(int favIdx) {
  const FavTeam &fav = favTeams[favIdx];
  if (!fav.espn.length()) return false;               // catalogue-only entry
  const SportModule *mod = sportOf(fav.sport);
  if (!strcmp(mod->espnSport, "cricket")) return espnDiscoverCricket(favIdx);

  /* The team's own league, not the sport's default: a team looked up under the
     wrong league still answers 200, just with an empty nextEvent[]. */
  const char *lg = fav.league.length() ? fav.league.c_str() : mod->espnLeague;
  String url = String("http://site.api.espn.com/apis/site/v2/sports/")
             + mod->espnSport + "/" + lg + "/teams/" + fav.espn;

  JsonDocument filter; espnFilterTeam(filter);
  JsonDocument doc;
  if (!espnGet(url, doc, filter)) return false;

  EspnWatch &w = espnWatch[favIdx];
  w.checkedAt = millis();
  JsonObject ne = doc["team"]["nextEvent"][0];
  if (ne.isNull() || !ne["id"].is<const char *>()) { w.valid = false; return true; }

  strlcpy(w.eventId, ne["id"] | "", sizeof(w.eventId));
  strlcpy(w.league, ne["league"]["slug"] | lg, sizeof(w.league));
  w.kickoffUtc = espnParseUtc(ne["date"] | "");

  w.homeComp[0] = w.awayComp[0] = 0;
  w.homeIsFav = false;
  for (JsonObject c : ne["competitions"][0]["competitors"].as<JsonArray>()) {
    const char *ha = c["homeAway"] | "";
    const char *tid = c["team"]["id"] | "";
    bool isHome = !strcmp(ha, "home");
    strlcpy(isHome ? w.homeComp : w.awayComp, c["id"] | tid,
            isHome ? sizeof(w.homeComp) : sizeof(w.awayComp));
    if (!strcmp(tid, fav.espn.c_str())) w.homeIsFav = isHome;
  }
  w.valid = w.eventId[0] && w.homeComp[0] && w.awayComp[0];
  if (w.valid)
    logLine("ESPN " + fav.abbr + ": ev " + w.eventId + " (" + w.league + ")");
  return true;
}

/* ---- live tick ------------------------------------------------------ */

/* Fetch one competitor's score. 234 bytes on the wire. */
static bool espnScore(const EspnWatch &w, const SportModule *mod,
                      const char *compId, int16_t &out) {
  String url = String("http://sports.core.api.espn.com/v2/sports/") + mod->espnSport
             + "/leagues/" + w.league + "/events/" + w.eventId
             + "/competitions/" + w.eventId + "/competitors/" + compId + "/score";
  JsonDocument filter; espnFilterScore(filter);
  JsonDocument doc;
  if (!espnGet(url, doc, filter)) return false;
  out = (int16_t)(doc["value"] | 0.0f);
  return true;
}

/* Build a LiveMatch for one watched fixture from the cheap endpoints.
   Takes the watch rather than a favourites index so that a league scan and a
   pinned match — neither of which is a followed team — use this same path. */
static bool espnLiveW(const EspnWatch &w, uint8_t sport, LiveMatch &m) {
  const SportModule *mod = sportOf(sport);
  const uint8_t     favSport = sport;   // named for the assignments below

  /* Cricket cannot use the core score endpoints: its score is a preformatted
     string ("305 & 128/3 (26 ov)"), not a number. Its series scoreboard is
     only ~6.7 KB, so poll that instead — cheap enough for the 15 s tick. */
  if (!strcmp(mod->espnSport, "cricket")) {
    String url = String("http://site.api.espn.com/apis/site/v2/sports/cricket/")
               + w.league + "/scoreboard";
    JsonDocument filter;
    JsonObject ev = filter["events"][0].to<JsonObject>();
    ev["id"] = true;
    ev["status"]["period"] = true;
    ev["status"]["type"]["state"] = true;
    ev["status"]["type"]["detail"] = true;
    ev["status"]["type"]["description"] = true;   // "Stumps" lives only here
    JsonObject cc = ev["competitions"][0]["competitors"][0].to<JsonObject>();
    cc["homeAway"] = true; cc["score"] = true; cc["winner"] = true;
    cc["team"]["id"] = true; cc["team"]["abbreviation"] = true;
    cc["team"]["displayName"] = true; cc["team"]["color"] = true;
    /* Who is actually at the crease. Two fields, and both are needed: the
       CURRENT innings is the linescore with isCurrent, and within it exactly
       one side has isBatting. See the loop below for why guessing is wrong. */
    JsonObject ls = cc["linescores"][0].to<JsonObject>();
    ls["isCurrent"] = true; ls["isBatting"] = true;
    JsonDocument doc;
    if (!espnGet(url, doc, filter)) return false;

    for (JsonObject e : doc["events"].as<JsonArray>()) {
      if (strcmp(e["id"] | "", w.eventId)) continue;
      strlcpy(m.id, w.eventId, sizeof(m.id));
      m.sport = favSport;
      m.state = espnState(e["status"]["type"]["state"] | "",
                          e["status"]["type"]["detail"] | "",
                          e["status"]["type"]["description"] | "");
      /* The overs on the card belong to the side AT THE CREASE, and after the
         first innings that is not something the score strings can tell you.
         Both sides carry a "(… ov)" from then on — a completed innings keeps
         its own, measured live: "VKK 171/3 (16/16 ov)" alongside "DD 88/7
         (11/16 ov)". The old code parsed both into one shared buffer, so the
         card showed whichever competitor the array happened to list last, and
         a chase at 11 overs was captioned 16. The array is ordered by home/away,
         not by who is batting, so it went wrong for roughly half of all matches
         and only ever after the first innings — which is exactly how it read.

         Nor can it be derived. "The side whose score has parentheses" is what
         broke. "order == status.period" holds for limited-overs but not for a
         Test after a follow-on, where the same side bats two innings running.
         linescores answers it outright and holds on both: measured at ENG v PAK
         period 3, PAK (order 1) batting again with isBatting on its isCurrent
         innings alone. */
      char overs[8] = "";      // the batting side's
      char anyOvers[8] = "";   // fallback: a feed with no linescores at all
      bool haveBatting = false;
      for (JsonObject c : e["competitions"][0]["competitors"].as<JsonArray>()) {
        Side &sd = (!strcmp(c["homeAway"] | "", "home")) ? m.home : m.away;
        strlcpy(sd.id,   c["team"]["id"] | "", sizeof(sd.id));
        strlcpy(sd.abbr, c["team"]["abbreviation"] | "?", sizeof(sd.abbr));
        strlcpy(sd.name, c["team"]["displayName"] | "", sizeof(sd.name));
        sd.color = teamInk(espnColor(c["team"]["color"] | "", mod->accent));

        char so[8] = "";       // per side now, so neither can clobber the other
        espnCricketScore(c["score"] | "", sd.score, sd.score2, so, sizeof(so));

        bool batting = false, known = false;
        for (JsonObject l : c["linescores"].as<JsonArray>()) {
          if (!(l["isCurrent"] | 0)) continue;
          batting = l["isBatting"] | false;
          known   = true;
          break;
        }
        /* active drives the team bar's breathe, and had the same fault: from
           the second innings on, both sides' scores carry parentheses, so both
           bars breathed and the marker stopped meaning anything. */
        sd.active = known ? batting : (so[0] != 0);

        if (so[0]) {
          strlcpy(anyOvers, so, sizeof(anyOvers));
          if (known && batting) {
            strlcpy(overs, so, sizeof(overs));
            haveBatting = true;
          }
        }
      }
      /* Between innings nobody is batting; the last innings played is the
         truthful thing to caption, which is what the fallback holds. */
      if (!haveBatting) strlcpy(overs, anyOvers, sizeof(overs));
      strlcpy(m.period, overs, sizeof(m.period));
      m.detail[0] = 0;                       // no broadcast furniture on the card
      return true;
    }
    sportLastErr = "event not on series board";
    return false;
  }

  /* Every other sport: two tiny score calls plus one status call. */
  int16_t hs = 0, as = 0;
  if (!espnScore(w, mod, w.homeComp, hs)) return false;
  if (!espnScore(w, mod, w.awayComp, as)) return false;

  String url = String("http://sports.core.api.espn.com/v2/sports/") + mod->espnSport
             + "/leagues/" + w.league + "/events/" + w.eventId
             + "/competitions/" + w.eventId + "/status";
  JsonDocument filter; espnFilterStatus(filter);
  JsonDocument doc;
  if (!espnGet(url, doc, filter)) return false;

  strlcpy(m.id, w.eventId, sizeof(m.id));
  m.sport = favSport;
  m.state = espnState(doc["type"]["state"] | "", doc["type"]["detail"] | "",
                      doc["type"]["description"] | "");
  m.home.score = hs; m.away.score = as;
  m.home.score2 = m.away.score2 = SCORE2_NONE;

  int period = doc["period"] | 0;
  const char *clock = doc["displayClock"] | "";
  if (m.state == MS_ENDED)   strlcpy(m.period, "FT", sizeof(m.period));
  else if (period > 0)       snprintf(m.period, sizeof(m.period), "%s%d",
                                      !strcmp(mod->espnSport, "hockey") ? "P" :
                                      !strcmp(mod->espnSport, "soccer") ? "H" : "Q", period);
  else                       m.period[0] = 0;
  strlcpy(m.detail, clock, sizeof(m.detail));

  /* ---- possession ----
     615 bytes, and the only reason the NFL card's chevron ever means anything:
     `active` was previously set only in the cricket branch above, so every
     other sport drew the marker on a fixed side regardless of play. Fetched
     only for sports whose layout actually shows it — soccer's /situation is an
     empty 141-byte stub, and basketball's carries fouls and timeouts, which is
     broadcast furniture at this pitch. */
  if (!strcmp(mod->key, "nfl")) {
    String su = String("http://sports.core.api.espn.com/v2/sports/") + mod->espnSport
              + "/leagues/" + w.league + "/events/" + w.eventId
              + "/competitions/" + w.eventId + "/situation";
    JsonDocument sf, sd;
    sf["team"]["$ref"] = true;
    if (espnGet(su, sd, sf)) {
      /* the possessing team arrives as a $ref URL; its id is the last segment */
      const char *ref = sd["team"]["$ref"] | "";
      const char *p = strstr(ref, "/teams/");
      if (p) {
        char tid[12] = "";
        p += 7;
        uint8_t k = 0;
        while (*p && *p != '?' && *p != '/' && k < sizeof(tid) - 1) tid[k++] = *p++;
        tid[k] = 0;
        m.home.active = (w.homeComp[0] && !strcmp(tid, w.homeComp));
        m.away.active = (w.awayComp[0] && !strcmp(tid, w.awayComp));
      }
    }
  }
  return true;
}

/* thin wrapper: the followed-team path still addresses a watch by index */
static bool espnLive(int favIdx, LiveMatch &m) {
  return espnLiveW(espnWatch[favIdx], favTeams[favIdx].sport, m);
}

/* ---- identity for matches with no followed team ---------------------
   A league match and a pinned match have no favourite side to borrow a name
   and colour from, and the 830-byte live endpoints carry neither. The flash
   catalogue that the dashboard picker uses cannot help: it lives on LittleFS
   and this runs on core 0, which must never touch it.

   So keep a small RAM table, filled by the SAME filtered /teams fetch that
   espnFetchCatalogue() already performs — that function builds its result in
   core-0 RAM before handing the JSON to core 1 to write, so there is no new
   network cost and no new parser, just a second consumer of the same pass. */
struct EspnIdent { char id[10]; char abbr[4]; char name[20]; uint16_t color; uint8_t sport; };
static EspnIdent espnIdent[ESPN_IDENT_MAX];
static uint8_t   espnIdentN = 0;
static uint32_t  espnIdentAt[NUM_SPORTS_MAX] = {0};   // millis of last fill

static void espnIdentAdd(uint8_t sport, const char *id, const char *abbr,
                         const char *name, uint16_t color) {
  if (!id || !*id) return;
  for (uint8_t i = 0; i < espnIdentN; i++)                 // update in place
    if (espnIdent[i].sport == sport && !strcmp(espnIdent[i].id, id)) {
      strlcpy(espnIdent[i].abbr, abbr, sizeof(espnIdent[i].abbr));
      strlcpy(espnIdent[i].name, name, sizeof(espnIdent[i].name));
      espnIdent[i].color = color;
      return;
    }
  if (espnIdentN >= ESPN_IDENT_MAX) return;                // full: keep the first
  EspnIdent &e = espnIdent[espnIdentN++];
  e.sport = sport;
  strlcpy(e.id,   id,   sizeof(e.id));
  strlcpy(e.abbr, abbr, sizeof(e.abbr));
  strlcpy(e.name, name, sizeof(e.name));
  e.color = color;
}

/* Fill a Side from the table; false when the team is not known yet. */
static bool espnIdentApply(uint8_t sport, Side &s) {
  for (uint8_t i = 0; i < espnIdentN; i++) {
    if (espnIdent[i].sport != sport || strcmp(espnIdent[i].id, s.id)) continue;
    strlcpy(s.abbr, espnIdent[i].abbr, sizeof(s.abbr));
    strlcpy(s.name, espnIdent[i].name, sizeof(s.name));
    s.color = teamInk(espnIdent[i].color);
    return true;
  }
  return false;
}

/* Last resort when the catalogue has not been fetched yet: show the team id
   rather than nothing, so the card is still readable while identity catches up
   on the next poll. teamInk() keeps it off a black-on-black card. */
static void espnIdentFallback(uint8_t sport, Side &s) {
  if (!s.abbr[0] || !strcmp(s.abbr, "?"))
    strlcpy(s.abbr, s.id[0] ? s.id : "??", sizeof(s.abbr));
  if (!s.name[0]) strlcpy(s.name, s.abbr, sizeof(s.name));
  if (!s.color)   s.color = teamInk(sportOf(sport)->accent);
}

/* Identity (abbr/colour/name) comes from discovery, not the live tick — the
   830-byte endpoints carry none of it, and it never changes mid-match. */
static void espnApplyIdentity(int favIdx, LiveMatch &m) {
  const FavTeam &fav = favTeams[favIdx];
  const SportModule *mod = sportOf(fav.sport);
  Side &favSide = espnWatch[favIdx].homeIsFav ? m.home : m.away;
  if (!favSide.abbr[0] || !strcmp(favSide.abbr, "?")) {
    strlcpy(favSide.id,   fav.espn.c_str(),  sizeof(favSide.id));
    strlcpy(favSide.abbr, fav.abbr.c_str(),  sizeof(favSide.abbr));
    strlcpy(favSide.name, fav.name.c_str(),  sizeof(favSide.name));
  }
  if (!favSide.color) favSide.color = teamInk(mod->accent);

  /* The opponent used to be a flat "OPP" in grey, because the cheap live
     endpoints name nobody and only the followed team's details were stored.
     The identity table filled for league follows answers this too — the
     competitor id IS the team id, so the opponent is a plain lookup. */
  Side &other = espnWatch[favIdx].homeIsFav ? m.away : m.home;
  if (!other.id[0])
    strlcpy(other.id, espnWatch[favIdx].homeIsFav ? espnWatch[favIdx].awayComp
                                                  : espnWatch[favIdx].homeComp,
            sizeof(other.id));
  if (!other.abbr[0] && !espnIdentApply(fav.sport, other)) {
    strlcpy(other.abbr, "OPP", sizeof(other.abbr));
    other.color = teamInk(RGB565(150, 150, 165));
  }
  if (!other.color) other.color = teamInk(RGB565(150, 150, 165));
  /* No catalogue for this sport yet: ask for one, and the next poll names it. */
  if (fav.sport < NUM_SPORTS_MAX && espnIdentAt[fav.sport] == 0)
    espnCatWanted = fav.sport;
}

/* ---- team catalogue -------------------------------------------------
   The hardcoded TeamEntry tables in sport_*.h stay as an offline fallback,
   but they carry no ESPN ids, so following a team for the live provider
   needs ESPN's own list. Fetched rarely (a league's roster changes at most
   weekly), filtered down from ~135 KB to ~1-2 KB, and cached on LittleFS so
   the dashboard can populate its picker without a network round trip. */
static String espnCataloguePath(uint8_t sportIdx) {
  return String("/espn_") + sportOf(sportIdx)->key + ".json";
}

bool espnFetchCatalogue(uint8_t sportIdx) {
  const SportModule *mod = sportOf(sportIdx);
  bool isCricket = !strcmp(mod->espnSport, "cricket");

  JsonDocument filter, doc;
  String url;
  if (isCricket) {
    /* Cricket's /teams 404s for a series id, and squads change per tour, so
       take the sides from the series scoreboard instead — its competitors
       carry abbreviation and colour inline. */
    url = String("http://site.api.espn.com/apis/site/v2/sports/cricket/")
        + mod->espnLeague + "/scoreboard";
    JsonObject c = filter["events"][0]["competitions"][0]["competitors"][0].to<JsonObject>();
    c["team"]["id"] = true; c["team"]["abbreviation"] = true;
    c["team"]["displayName"] = true; c["team"]["color"] = true;
    c["team"]["alternateColor"] = true;
  } else {
    url = String("http://site.api.espn.com/apis/site/v2/sports/")
        + mod->espnSport + "/" + mod->espnLeague + "/teams";
    JsonObject t = filter["sports"][0]["leagues"][0]["teams"][0]["team"].to<JsonObject>();
    t["id"] = true; t["abbreviation"] = true; t["displayName"] = true;
    t["color"] = true; t["alternateColor"] = true;
  }
  if (!espnGet(url, doc, filter)) return false;

  JsonDocument out;
  JsonArray arr = out.to<JsonArray>();
  auto addTeam = [&](JsonObject t) {
    const char *id = t["id"] | "";
    if (!*id) return;
    for (JsonObject e : arr)                       // cricket repeats sides
      if (!strcmp(e["i"] | "", id)) return;
    JsonObject e = arr.add<JsonObject>();
    e["i"] = id;
    e["a"] = t["abbreviation"] | "?";
    e["n"] = t["displayName"] | "";
    e["c"] = t["color"] | "";
    /* second consumer of the same parse: the RAM table that names the sides of
       a league or pinned match, which has no followed team to borrow from */
    espnIdentAdd(sportIdx, id, t["abbreviation"] | "?", t["displayName"] | "",
                 teamInkPair(espnColor(t["color"] | "", mod->accent),
                             espnColor(t["alternateColor"] | "", 0)));
  };
  if (isCricket) {
    for (JsonObject ev : doc["events"].as<JsonArray>())
      for (JsonObject c : ev["competitions"][0]["competitors"].as<JsonArray>())
        addTeam(c["team"]);
  } else {
    for (JsonObject w : doc["sports"][0]["leagues"][0]["teams"].as<JsonArray>())
      addTeam(w["team"]);
  }
  if (arr.size() == 0) { sportLastErr = "catalogue empty"; return false; }

  /* This runs on core 0, which must never touch LittleFS (see the cross-core
     contract in sports_core.h). Hand the finished JSON to core 1 through the
     same queue Telegram and MQTT use; it does the write. ~1-2 KB of String. */
  String json; serializeJson(out, json);
  postTgCmd(TGC_ESPN_CAT, json, sportIdx);
  if (sportIdx < NUM_SPORTS_MAX) espnIdentAt[sportIdx] = millis() | 1;
  logLine("ESPN catalogue " + String(mod->key) + ": " + String(arr.size()) + " teams");
  return true;
}

/* ---- following a whole league ---------------------------------------
   Same EspnWatch as a followed team, reached a different way: instead of
   asking one team what it plays next, ask the league what is on today and
   take the first match actually in progress.

   Three calls, and it stops as early as it can:
     events?dates=<today>                  ~1 KB   the day's fixtures
     competitions/{id}/status              ~350 B  each, until one is live
     competitions/{id}/competitors         ~1.4 KB once, for the two team ids
   Identity comes from the RAM table above, so no further calls are made. */
static bool espnScanLeague(int favIdx) {
  const FavTeam &fav = favTeams[favIdx];
  EspnWatch     &w   = espnWatch[favIdx];
  const SportModule *mod = sportOf(fav.sport);
  w.checkedAt = millis() | 1;

  /* Ask for a TWO-DAY range in UTC, not "today" locally. ESPN indexes a
     fixture by its own local date, and the frame's date is not that date: at
     IST the NFL games in progress right now are filed under yesterday, so a
     local "today" query returned zero events while the range returns all
     seven. UTC bounds take the frame's timezone out of it entirely, and the
     range costs the same ~1 KB as a single day. */
  time_t   nowT = time(nullptr);
  struct tm a, b;
  time_t   yest = nowT - 86400;
  if (nowT < 8 * 3600 || !gmtime_r(&yest, &a) || !gmtime_r(&nowT, &b)) {
    sportLastErr = "no clock yet";              // dates= needs a real date
    return false;
  }
  char span[20];
  snprintf(span, sizeof(span), "%04d%02d%02d-%04d%02d%02d",
           a.tm_year + 1900, a.tm_mon + 1, a.tm_mday,
           b.tm_year + 1900, b.tm_mon + 1, b.tm_mday);

  String base = String("http://sports.core.api.espn.com/v2/sports/") + mod->espnSport
              + "/leagues/" + fav.league + "/events";
  JsonDocument filter, doc;
  filter["items"][0]["$ref"] = true;
  if (!espnGet(base + "?dates=" + span + "&limit=" + String(ESPN_SCAN_MAX), doc, filter))
    return false;

  /* ids only — the refs are full URLs, and the id is the last path segment */
  char ids[ESPN_SCAN_MAX][16];
  uint8_t nIds = 0;
  for (JsonObject it : doc["items"].as<JsonArray>()) {
    if (nIds >= ESPN_SCAN_MAX) break;
    const char *ref = it["$ref"] | "";
    const char *p = strstr(ref, "/events/");
    if (!p) continue;
    p += 8;
    uint8_t k = 0;
    while (*p && *p != '?' && *p != '/' && k < sizeof(ids[0]) - 1) ids[nIds][k++] = *p++;
    ids[nIds][k] = 0;
    if (k) nIds++;
  }
  if (!nIds) {
    if (w.valid) logLine("ESPN " + fav.name + ": no fixtures");
    w.valid = false; sportLastErr = "no fixtures";
    return true;
  }

  for (uint8_t i = 0; i < nIds; i++) {
    String cbase = String("http://sports.core.api.espn.com/v2/sports/") + mod->espnSport
                 + "/leagues/" + fav.league + "/events/" + ids[i]
                 + "/competitions/" + ids[i];
    JsonDocument sf, sd; espnFilterStatus(sf);
    if (!espnGet(cbase + "/status", sd, sf)) continue;
    uint8_t est = espnState(sd["type"]["state"] | "", sd["type"]["detail"] | "",
                            sd["type"]["description"] | "");
    if (est != MS_LIVE && est != MS_BREAK) continue;   // not under way

    JsonDocument cf, cd;
    cf["items"][0]["id"] = true;
    cf["items"][0]["homeAway"] = true;
    if (!espnGet(cbase + "/competitors", cd, cf)) continue;
    w.homeComp[0] = w.awayComp[0] = 0;
    for (JsonObject c : cd["items"].as<JsonArray>()) {
      bool isHome = !strcmp(c["homeAway"] | "", "home");
      strlcpy(isHome ? w.homeComp : w.awayComp, c["id"] | "",
              isHome ? sizeof(w.homeComp) : sizeof(w.awayComp));
    }
    if (!w.homeComp[0] || !w.awayComp[0]) continue;

    strlcpy(w.eventId, ids[i], sizeof(w.eventId));
    strlcpy(w.league,  fav.league.c_str(), sizeof(w.league));
    w.homeIsFav  = true;            // neither side is "the" favourite
    w.kickoffUtc = 0;               // already in progress, so no wait
    w.valid      = true;
    logLine("ESPN " + fav.name + ": live ev " + w.eventId);
    return true;
  }
  if (w.valid) logLine("ESPN " + fav.name + ": nothing live of " + String(nIds));
  w.valid = false;
  return true;
}

/* ---- cricket: the last six deliveries -------------------------------
   The card's pill strip was drawn by the renderer but written only by the demo
   simulator, so on a real match it was blank. This fills it.

   The source is playbyplay, and its paging is the whole difficulty: `limit`
   windows from the START of the innings, so limit=6 returns the first over of
   the day, not the last. The response does carry `pageCount`, and page=N with
   a small limit works — but the final page is usually PARTIAL (an innings of
   1819 balls leaves one ball on page 304), so no single request returns "the
   last six".

   Hence rebuilding from the tail page. Note what does NOT work: appending
   "any ball newer than the last id seen". Commentary ids restart at the
   INNINGS, not the match — a first innings measured 110 ... 115060 and the
   second opened at 210 — so a high-water mark carried across the break sits
   far above every id of the new innings and the strip freezes on the old one
   while the score above it counts the new one. Ids are monotonic only within
   an innings (test_cricket_ball_ids_reset_at_the_innings_break).

   The strip lives in the watch, so it survives both the score polls in between
   and a page rolling over to a fresh over.

   pageCount is cached and self-correcting: ask for the page we believed was
   last, and the reply tells us what last really is now. An over-range page
   still returns the metadata, so a stale guess costs accuracy for one cycle,
   never an error. */
static char espnBallChar(const char *playTypeId, const char *shortText) {
  /* playType ids, confirmed against live Tests and a T20:
       1 run  2 no run  3 four  4 six  5 no ball  6 wide  7 bye  8 leg bye
       9 out                                                                */
  if (!playTypeId) return 0;
  if (!strcmp(playTypeId, "2")) return '.';
  if (!strcmp(playTypeId, "9")) return 'W';
  if (!strcmp(playTypeId, "3")) return '4';
  if (!strcmp(playTypeId, "4")) return '6';
  /* runs and extras carry their count only in the prose ("…, 1 leg bye"), so
     take the first digit after the last comma; a run with no number is one. */
  if (shortText && *shortText) {
    const char *p = strrchr(shortText, ',');
    if (p) for (const char *q = p; *q; q++)
      if (*q >= '1' && *q <= '9') return *q;
  }
  return '1';
}

static bool espnCricketBalls(EspnWatch &w) {
  JsonDocument filter;
  JsonObject c = filter["commentary"].to<JsonObject>();
  c["pageCount"] = true;
  JsonObject it = c["items"][0].to<JsonObject>();
  it["id"] = true; it["shortText"] = true; it["playType"]["id"] = true;

  auto fetch = [&](uint16_t page, JsonDocument &doc) {
    String url = String("http://site.api.espn.com/apis/site/v2/sports/cricket/")
               + w.league + "/playbyplay?event=" + w.eventId
               + "&limit=6&page=" + String(page);
    return espnGet(url, doc, filter);
  };
  /* Append a page's real deliveries. Every page is padded with a sentinel —
     id 999999999999999, empty text, but a perfectly valid-looking playType of
     "2"/no run — so filter on both empty text and id magnitude (real ids are
     over*100 + ball*10, six or seven digits). */
  auto collect = [&](JsonDocument &doc, char *buf, uint8_t &n, uint8_t cap) {
    for (JsonObject b : doc["commentary"]["items"].as<JsonArray>()) {
      const char *st = b["shortText"] | "";
      if (!*st) continue;
      long id = atol(b["id"] | "0");
      if (id <= 0 || id > 99999999L) continue;
      if (n < cap) buf[n++] = espnBallChar(b["playType"]["id"] | (const char *)nullptr, st);
    }
  };

  /* Rebuild from the last two pages rather than accumulating ball by ball.
     Accumulating looked cheaper — one request, append anything newer than the
     last id seen — but it loses deliveries every time the page rolls over: the
     tail of the page being left behind is never read again, and the strip then
     carries a hole for good. Rebuilding is self-healing instead: it is correct
     after a reboot, after a missed fetch, and after any page roll, at the cost
     of one extra request a minute. The last two pages always contain at least
     six deliveries because a page is six. */
  uint16_t page = w.ballPage ? w.ballPage : 1;
  JsonDocument cur;
  if (!fetch(page, cur)) return false;
  uint16_t pages = (uint16_t)(cur["commentary"]["pageCount"] | 1);
  if (pages && pages != page) {                 // first run, or the page rolled
    page = pages;
    if (!fetch(page, cur)) return false;
  }
  w.ballPage = page;

  /* Collect the tail page first. Only reach back a page when it cannot answer
     on its own: a page is six deliveries, so a FULL tail page already is the
     last six and the second request would be pure waste — and these pages are
     not cheap (16.5 KB each, measured, because every item carries both squads'
     bowling figures). The tail page is usually partial, but skipping the reach
     -back whenever it is full is what keeps a per-ball refresh affordable. */
  char buf[20];
  uint8_t n = 0, tail = 0;
  collect(cur, buf, tail, sizeof(buf));
  if (tail >= 6) {
    n = tail;
  } else if (page > 1) {
    JsonDocument prev;
    if (fetch(page - 1, prev)) collect(prev, buf, n, sizeof(buf));
    collect(cur, buf, n, sizeof(buf));
  } else {
    n = tail;
  }
  if (!n) return true;                          // nothing bowled yet

  uint8_t keep = n > 6 ? 6 : n;                 // the last six, in order
  memcpy(w.strip, buf + (n - keep), keep);
  w.strip[keep] = 0;
  return true;
}

/* The strip refreshes when the SCORE moves, not on a clock of its own.

   It used to be a flat 60 s timer while the score ticked every 20 s, so for up
   to two polls the card showed a wicket in "64/3" with no W anywhere in the
   pills under it — the two halves of the same card disagreeing about what had
   just happened, which is the one thing a six-ball strip exists to prevent.
   Keying off the score is exact rather than merely faster: the pills are
   refetched on precisely the polls where the thing they illustrate changed.

   The signature is what the card itself prints — both sides' runs and wickets
   plus the overs — so "the score changed" and "the strip is stale" are by
   construction the same question. ESPN_BALLS_MS survives as the IDLE refresh,
   covering the deliveries that move no number at all (dots and a maiden over),
   and as the first fetch of a match whose score has not moved since we
   attached to it. */
static void espnCricketStrip(EspnWatch &w, LiveMatch &m) {
  if (m.state == MS_LIVE) {
    char sig[24];
    snprintf(sig, sizeof(sig), "%d/%d %d/%d %s",
             m.home.score, m.home.score2, m.away.score, m.away.score2, m.period);
    bool moved = strcmp(sig, w.ballSig) != 0;
    bool idle  = !w.ballsAt || millis() - w.ballsAt > ESPN_BALLS_MS;
    if (moved || idle) {
      w.ballsAt = millis() | 1;
      strlcpy(w.ballSig, sig, sizeof(w.ballSig));
      espnCricketBalls(w);
    }
  }
  /* Copied on EVERY poll, not just the ones that fetched: LiveMatch is rebuilt
     from scratch each time, so the strip would otherwise blink in and out. */
  strlcpy(m.strip, w.strip, sizeof(m.strip));
}

/* ---- a match the user pinned from the dashboard ----------------------
   The browser reads the event id, league and both team ids straight off
   ESPN's scoreboard (a competitor id IS the team id — verified on soccer and
   NFL) and hands them over, so there is nothing to discover: the pin goes
   directly to the ~830 byte live tick. That is what makes "show this on the
   panel" feel immediate rather than costing a discovery round trip. */
static EspnWatch espnPin;
static uint8_t   espnPinSport = 0;
static bool      espnPinSet   = false;

void espnSetPin(uint8_t sport, const char *league, const char *eventId,
                const char *homeId, const char *awayId) {
  memset(&espnPin, 0, sizeof(espnPin));
  strlcpy(espnPin.eventId,  eventId, sizeof(espnPin.eventId));
  strlcpy(espnPin.league,   league,  sizeof(espnPin.league));
  strlcpy(espnPin.homeComp, homeId,  sizeof(espnPin.homeComp));
  strlcpy(espnPin.awayComp, awayId,  sizeof(espnPin.awayComp));
  espnPin.homeIsFav = true;
  espnPin.valid     = true;
  espnPinSport      = sport;
  espnPinSet        = true;
}
void espnClearPin() { espnPinSet = false; espnPin.valid = false; }

/* ---- how soon the next poll matters --------------------------------- */
uint8_t espnUrgency() {
  if (espnPinSet) return 2;
  time_t nowT = time(nullptr);
  uint32_t nowUtc = (nowT > 8 * 3600) ? (uint32_t)nowT : 0;
  uint8_t best = 0;
  for (int i = 0; i < numFavTeams; i++) {
    const EspnWatch &w = espnWatch[i];
    if (!w.valid) continue;
    /* Suspended is not urgent. Stumps on a Test is fourteen hours of nothing;
       polling it at the sport's live rate would spend thousands of requests
       waiting for morning. Drop to the pregame cadence, which still notices
       the resumption within a minute. */
    if (w.lastState == MS_SUSPENDED) { if (!best) best = 1; continue; }
    if (!w.kickoffUtc || !nowUtc || nowUtc >= w.kickoffUtc) return 2;   // in window
    if (w.kickoffUtc - nowUtc < 3600) best = 1;                         // soon
  }
  return best;
}

/* The tick a live card actually deserves. A basketball score moves every few
   seconds while a Test match does not, so a single ESPN_LIVE_MS either wastes
   requests on cricket or leaves basketball visibly stale. Each SportModule
   states its own; when several are live the fastest wins, because the panel
   rotates between them and the card on screen has to be the current one. */
uint32_t espnLiveIntervalMs() {
  /* An explicit override wins, but not below the floor: sportsTask wakes every
     5 s, so anything faster is unreachable however it is configured. */
  if (sportRefreshSec > 0)
    return (uint32_t)(sportRefreshSec < 5 ? 5 : sportRefreshSec) * 1000UL;
  uint32_t best = 0;
  auto consider = [&](uint8_t sport) {
    uint32_t ms = sportOf(sport)->liveMs;
    if (!ms) ms = ESPN_LIVE_MS;
    if (!best || ms < best) best = ms;
  };
  if (espnPinSet) consider(espnPinSport);
  time_t nowT = time(nullptr);
  uint32_t nowUtc = (nowT > 8 * 3600) ? (uint32_t)nowT : 0;
  for (int i = 0; i < numFavTeams; i++) {
    const EspnWatch &w = espnWatch[i];
    if (!w.valid || w.lastState == MS_SUSPENDED) continue;
    if (!w.kickoffUtc || !nowUtc || nowUtc >= w.kickoffUtc) consider(favTeams[i].sport);
  }
  return best ? best : ESPN_LIVE_MS;
}

/* ---- the poll sportsPoll() calls ------------------------------------ */
bool espnPoll(LiveMatch *out, int maxN, int &n) {
  n = 0;
  if (espnBackoffMs && millis() - espnLastReqAt < espnBackoffMs) {
    sportLastErr = "backing off";
    return false;
  }

  time_t nowT = time(nullptr);
  uint32_t nowUtc = (nowT > 8 * 3600) ? (uint32_t)nowT : 0;
  bool any = false;

  /* The pin goes first so it always survives the MAX_LIVE cap — the user asked
     for this one by hand, and it must not be crowded out by followed teams. */
  if (espnPinSet && n < maxN) {
    LiveMatch m;
    memset(&m, 0, sizeof(m));
    m.home.score2 = m.away.score2 = SCORE2_NONE;
    if (espnLiveW(espnPin, espnPinSport, m)) {
      strlcpy(m.home.id, espnPin.homeComp, sizeof(m.home.id));
      strlcpy(m.away.id, espnPin.awayComp, sizeof(m.away.id));
      if (!espnIdentApply(espnPinSport, m.home)) espnIdentFallback(espnPinSport, m.home);
      if (!espnIdentApply(espnPinSport, m.away)) espnIdentFallback(espnPinSport, m.away);
      /* A pin can be the only reason this sport is on screen, so it has to ask
         for its own catalogue — otherwise a pinned match in a sport with
         nothing followed shows team ids for as long as it runs. */
      if (espnPinSport < NUM_SPORTS_MAX && espnIdentAt[espnPinSport] == 0)
        espnCatWanted = espnPinSport;
      m.home.fav = m.away.fav = true;      // pinned counts as eligible
      /* A pinned cricket match needs its strip too. This branch had none, so
         pinning one from the dashboard gave a card whose score ticked over an
         empty row of pills for the whole match — the same disagreement the
         followed-team path had, in its most complete form. espnPin is a static
         watch, so it carries the page and signature exactly as espnWatch[] does. */
      if (espnPinSport < NUM_SPORTS_MAX &&
          !strcmp(sportOf(espnPinSport)->espnSport, "cricket"))
        espnCricketStrip(espnPin, m);
      m.startedAt = m.changedAt = millis();
      out[n++] = m;
      any = true;
      if (m.state == MS_ENDED) espnPinSet = false;   // released; hold applies
    }
  }

  for (int i = 0; i < numFavTeams && n < maxN; i++) {
    const bool isLeague = followIsLeague(favTeams[i]);
    if (!isLeague && !favTeams[i].espn.length()) continue;   // catalogue-only
    if (!sportIsOn(favTeams[i].sport)) continue;             // sport switched off
    EspnWatch &w = espnWatch[i];

    /* Should this fixture be live-polled right now? Only inside its window:
       from kickoff until it ends. Outside it we do nothing but occasionally
       re-discover, which is what keeps the daily request count tiny. */
    bool inWindow = w.valid && (!w.kickoffUtc || !nowUtc || nowUtc >= w.kickoffUtc);

    if (!w.valid || !inWindow) {
      uint32_t due = isLeague ? ESPN_SCAN_MS
                   : (w.valid && w.kickoffUtc && nowUtc &&
                      w.kickoffUtc - nowUtc < 3600) ? ESPN_PREGAME_MS : ESPN_DISCOVER_MS;
      if (w.checkedAt && millis() - w.checkedAt < due) continue;

      bool okDisc = isLeague ? espnScanLeague(i) : espnDiscover(i);
      if (!okDisc) {
        logLine("ESPN discover " + favTeams[i].abbr + " failed: " + sportLastErr);
        continue;
      }
      /* Fall through to the live fetch IN THIS SAME PASS when what we just
         found is already under way. Returning here instead is what used to
         make following a team mid-match take up to ten minutes to appear:
         discovery was prompt, but the first score waited for the next poll,
         and with nothing yet on screen that poll was ten minutes out. */
      inWindow = w.valid && (!w.kickoffUtc || !nowUtc || nowUtc >= w.kickoffUtc);
      if (!inWindow) continue;
    }

    /* Two followed teams playing EACH OTHER is one match, not two.
       Without this the fixture was fetched and published once per favourite,
       and since the copies were fetched seconds apart they carried different
       scores. fetchScores() diffs by event id and takes the first hit, so both
       copies diffed against the same baseline and whichever landed lower gave
       a negative delta — no event, no animation. It also doubled the requests
       and put the same card twice into the rotation. */
    bool dup = false;
    for (int k = 0; k < n; k++)
      if (!strcmp(out[k].id, w.eventId)) {
        /* the other side is followed too: mark it, so the bright cap on the
           team bars correctly shows neither side is uniquely "yours" */
        if (!isLeague) {
          if (isFavTeam(favTeams[i].sport, out[k].home.id)) out[k].home.fav = true;
          if (isFavTeam(favTeams[i].sport, out[k].away.id)) out[k].away.fav = true;
        }
        dup = true;
        break;
      }
    if (dup) continue;

    LiveMatch m;
    memset(&m, 0, sizeof(m));
    m.home.score2 = m.away.score2 = SCORE2_NONE;
    if (!espnLiveW(w, favTeams[i].sport, m)) continue;

    if (isLeague) {
      /* No favourite side to borrow identity from — name both from the RAM
         table, and mark both as eligible so the card is allowed on the panel. */
      strlcpy(m.home.id, w.homeComp, sizeof(m.home.id));
      strlcpy(m.away.id, w.awayComp, sizeof(m.away.id));
      if (!espnIdentApply(favTeams[i].sport, m.home)) espnIdentFallback(favTeams[i].sport, m.home);
      if (!espnIdentApply(favTeams[i].sport, m.away)) espnIdentFallback(favTeams[i].sport, m.away);
      m.home.fav = m.away.fav = true;
      /* Identity is empty until this sport's catalogue has been fetched once;
         ask for it, and the next poll shows real names instead of OPP. */
      if (espnIdentAt[favTeams[i].sport] == 0) espnCatWanted = favTeams[i].sport;
    } else {
      espnApplyIdentity(i, m);
      m.home.fav = m.home.fav || isFavTeam(favTeams[i].sport, m.home.id);
      m.away.fav = m.away.fav || isFavTeam(favTeams[i].sport, m.away.id);
      if (!m.home.fav && !m.away.fav)
        (w.homeIsFav ? m.home : m.away).fav = true;   // it is why we polled
    }
    /* Cricket's pill strip, kept in step with the score above it. */
    if (!strcmp(sportOf(favTeams[i].sport)->espnSport, "cricket"))
      espnCricketStrip(w, m);

    m.startedAt = millis();
    m.changedAt = millis();

    /* A finished match stops being interesting; re-discover the next fixture
       (sportsTick keeps the final score up for sportHoldMin either way). */
    if (m.state == MS_ENDED) w.checkedAt = 0;
    w.lastState = m.state;      // drives the poll cadence (espnUrgency)

    out[n++] = m;
    any = true;
  }

  if (!any && n == 0 && !sportLastErr.length()) sportLastErr = "no fixtures";
  return true;     // true even with n==0: an empty poll is a valid answer
}

#endif  // ESPN_ENABLE
