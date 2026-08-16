/* DigiFrame — live scores: data model, favourites store, poll task, event ring.

   This is the bottom layer of the live-score feature and it draws nothing.
   It knows about matches, favourite teams and events; it does NOT know about
   pixels or about any individual sport. Sports plug in above it as modules
   (sport_*.h) that are gathered by sports_registry.h.

   Layering (dependencies only ever point down):

       sport_cricket.h  sport_football.h  sport_basketball.h  ...
                          |
                 score_gfx.h + score_fx.h        (shared drawing toolkit)
                          |
                    sports_core.h                (this file)

   Cross-core contract: sportsTask (core 0) polls the provider, fills
   scoreBack[] under sportsMutex and pushes events; sportsTick() (core 1)
   copies into scoreFront[] and drives the clock sub-mode. The renderer only
   ever reads scoreFront[], unlocked. Every string here is a fixed char[],
   never a String, for the same reason cfgLat/cfgLon are (see globals.h). */
#pragma once

/**********************  7a. MODEL  ***********************************/
#define MAX_LIVE        4       // live matches tracked at once
#define MAX_FOLLOWS     12      // teams + leagues the user can follow
#define MAX_FAV_TEAMS   MAX_FOLLOWS   // old name, still used by espnWatch[]
#define SCORE2_NONE     (-1)    // "this sport has no secondary score"

/* 565 packing as a compile-time constant — dma->color565() is a method and
   cannot be used in the static tables the sport modules declare. */
#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

/* MS_BREAK is a pause you wait through with the score still up — half time,
   an innings break, lunch. MS_SUSPENDED is one you do not: stumps on a Test is
   fourteen hours of no play, and rain or bad light is open-ended. The card
   releases after the usual sportHoldMin, exactly as at full time, but the
   fixture is NOT finished so the watch keeps it rather than re-discovering. */
enum MatchState : uint8_t { MS_UPCOMING, MS_LIVE, MS_BREAK, MS_ENDED, MS_SUSPENDED };

struct Side {
  char     id[12];     // provider/catalogue id, used to match favourites
  char     abbr[5];    // what the panel shows — 4 chars max
  char     name[20];   // dashboard only
  uint16_t color;
  int16_t  score;      // goals / points / runs
  int16_t  score2;     // wickets / sets / shootout — SCORE2_NONE when unused
  bool     active;     // possession / batting / serving — drives the edge glow
  bool     fav;
};

struct LiveMatch {
  char     id[24];
  uint8_t  sport;            // index into SPORTS[]
  uint8_t  state;            // MatchState
  Side     home, away;
  char     period[8];        // "67'", "Q3", "38.2", "P2"
  char     detail[16];       // "RR 6.4", "2nd&7", "PP 1:20"
  char     strip[8];         // recent-events strip, interpreted by the sport
  char     ticker[64];
  char     lastEvent[16];    // native event name, e.g. "goal" / "wicket"
  uint16_t eventSeq;         // bumped by the provider each time lastEvent is new
  uint32_t startedAt;        // millis the match went live (simulator clock)
  uint32_t changedAt;        // millis of the last visible change
  uint8_t  sportData[16];    // opaque scratch a sport module may use freely
};

/* sportData accessors — a sport module's private scratch inside the match */
static inline uint16_t sdGet16(const LiveMatch &m, int i) {
  return (uint16_t)(m.sportData[i] | (m.sportData[i + 1] << 8));
}
static inline void sdSet16(LiveMatch &m, int i, uint16_t v) {
  m.sportData[i] = v & 0xFF; m.sportData[i + 1] = v >> 8;
}
/* push a character onto the 6-slot recent-events strip */
static void stripPush(LiveMatch &m, char ch) {
  int n = (int)strlen(m.strip);
  if (n >= 6) { memmove(m.strip, m.strip + 1, n); n--; }
  m.strip[n] = ch; m.strip[n + 1] = 0;
}
/* a simulated match reports an event exactly the way a real feed would */
static void simReport(LiveMatch &m, const char *ev, bool homeSide) {
  strlcpy(m.lastEvent, ev, sizeof(m.lastEvent));
  m.eventSeq++;
  m.home.active = homeSide;
  m.away.active = !homeSide;
}

/**********************  7b. EVENTS  **********************************/
/* Generic kinds every sport folds its native events onto, so a new sport
   inherits working animations and overrides only what is worth the effort. */
enum EventKind : uint8_t {
  EV_SCORE_MINOR, EV_SCORE_MAJOR, EV_SCORE_BIG, EV_TURNOVER, EV_MILESTONE,
  EV_PENALTY, EV_REVIEW, EV_LEAD_CHANGE, EV_PERIOD, EV_FINAL, EV_KIND_COUNT
};

/* native provider name -> kind + labels. `punch` is the size-2 banner word and
   is hard-capped at 5 characters (12x16 glyphs, 64px panel); `label` is the
   size-1 line beneath it, so "TD!" punches while "TOUCHDOWN" explains. */
struct EventMap {
  const char *native;
  uint8_t     kind;
  const char *punch;      // <= 5 chars
  const char *label;      // <= 10 chars looks best
};

struct MatchEvent {
  uint8_t  kind;
  uint8_t  sport;
  bool     homeSide;      // which side the event belongs to
  uint16_t color;         // that side's colour, pre-resolved for the renderer
  char     native[16];    // lets a sport module key its bespoke animation
  char     punch[6];
  char     label[12];
  uint32_t at;            // millis queued
};

/* how long each generic animation runs, in 15fps frames (see score_fx.h) */
static const uint8_t EV_FRAMES[EV_KIND_COUNT] = {
  12,  // EV_SCORE_MINOR — deliberately quiet
  38,  // EV_SCORE_MAJOR
  45,  // EV_SCORE_BIG
  38,  // EV_TURNOVER
  45,  // EV_MILESTONE
  30,  // EV_PENALTY
  50,  // EV_REVIEW
  25,  // EV_LEAD_CHANGE
  30,  // EV_PERIOD
  60   // EV_FINAL
};

/* higher wins and preempts: FINAL > BIG > MAJOR > TURNOVER > ... > MINOR */
/* Team colours come from data we don't fully control — and the All Blacks'
   is literally RGB565(20,20,20). On a black card that makes a side's score,
   abbreviation and every event tinted by it effectively invisible. Floor
   very dark colours toward silver, scaled by the shortfall, so identity
   colour never means invisible on an emissive panel. Applied at the two
   points match data enters LiveMatch (fetchScores, sportsDemoForce), so
   renderers and effects never need to think about it. */
/* rough 565-weighted luminance, 0..187 */
static inline int inkLum(uint16_t c) {
  return 2 * ((c >> 11) & 0x1F) + ((c >> 5) & 0x3F) + 2 * (c & 0x1F);
}
#define INK_FLOOR 48
static uint16_t teamInk(uint16_t c) {
  int r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
  int lum = 2 * r + g + 2 * b;              // rough 565-weighted, 0..250
  if (lum >= INK_FLOOR) return c;
  int add = (INK_FLOOR - lum) / 5;
  r += add; g += add * 2; b += add;
  if (r > 31) r = 31; if (g > 63) g = 63; if (b > 31) b = 31;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

/* Pick between a team's two published colours.

   teamInk() is a legibility floor, not a palette: it lifts a near-black to
   *just* legible and no further, which is right for a side whose only colour
   is dark but wrong when the team publishes a bright second one. Cleveland's
   primary is 472a08 — a brown that teamInk raises barely over its floor and
   which reads as a smudge on the card, while their alternate is ff3c00. So
   prefer the alternate whenever the primary falls under the floor and the
   alternate clears it. Nothing is invented: both colours are the team's own. */
static uint16_t teamInkPair(uint16_t primary, uint16_t alt) {
  if (inkLum(primary) < INK_FLOOR && alt && inkLum(alt) >= INK_FLOOR) return alt;
  return teamInk(primary);
}

/* Does this event earn the panel? See the note in scoreEventSchedule() —
   deliberately not derived from evPriority(), which answers a different
   question (which event wins when two land at once). */
static bool evIsMajor(uint8_t kind) {
  switch (kind) {
    case EV_SCORE_MAJOR:            // goal, try, touchdown, six, basket
    case EV_SCORE_BIG:              // hat-trick, three, drop goal, two-point
    case EV_TURNOVER:               // wicket, interception, fumble, own goal
    case EV_MILESTONE:              // fifty, century, double-double
    case EV_FINAL:      return true;
    default:            return false;  // minor score, penalty, review, lead, period
  }
}

static uint8_t evPriority(uint8_t kind) {
  switch (kind) {
    case EV_FINAL:       return 10;
    case EV_SCORE_BIG:   return 9;
    case EV_SCORE_MAJOR: return 8;
    case EV_TURNOVER:    return 7;
    case EV_MILESTONE:   return 6;
    case EV_PENALTY:     return 5;
    case EV_REVIEW:      return 4;
    case EV_LEAD_CHANGE: return 3;
    case EV_PERIOD:      return 2;
    default:             return 1;   // EV_SCORE_MINOR
  }
}

/**********************  7c. SPORT MODULE INTERFACE  ******************/
struct TeamEntry { const char *id; const char *name; const char *abbr; uint16_t color; };

struct SportModule {
  const char      *key;        // "cricket" — wire + config value
  const char      *label;      // "Cricket" — dashboard
  uint16_t         accent;
  const TeamEntry *catalogue;  uint8_t numTeams;
  const EventMap  *events;     uint8_t numEvents;

  /* rows 21-54: this sport's own layout */
  void (*drawBody)(const LiveMatch &m, uint32_t f);
  /* bespoke animation; return false to fall through to the generic one */
  bool (*drawEvent)(const LiveMatch &m, const MatchEvent &e, uint32_t f);
  /* demo feed: advance this match by wall-clock ms since it went live */
  void (*simulate)(LiveMatch &m, uint32_t t);

  /* ---- ESPN live provider (espn_api.h) ----
     `espnSport` is ESPN's sport slug, `espnLeague` the default league or
     series id (numeric for rugby and cricket, a slug elsewhere); the
     league is overridden at runtime by whatever a followed team's
     nextEvent reports, so this is only the starting point.

     The cheap live endpoints return scores, not a play feed, so events are
     DERIVED from score deltas: eventForDelta maps a change onto one of this
     sport's own native event names, keeping the whole animation set working
     without play-by-play. Return nullptr for a change not worth animating. */
  const char *espnSport;
  const char *espnLeague;
  const char *(*eventForDelta)(int dScore, int dScore2);

  /* How far through the match, 0..100, or <0 for "cannot say" — which draws
     nothing rather than a lie. Feeds the match comet (gfxMeter), the one
     bottom-band device all six sports now share. Each sport reads its own
     period/clock: quarters for NFL, minutes for football, overs for cricket. */
  int (*progress)(const LiveMatch &m);

  /* Live poll interval in ms. A basketball score moves every few seconds; a
     Test match does not. Polling them alike either wastes requests or shows a
     stale score, so the sport says which it is. 0 = use ESPN_LIVE_MS. */
  uint32_t liveMs;
};

/* Defined by sports_registry.h, which is included after the modules. Declared
   here so the plumbing below can resolve match.sport without depending on any
   individual sport. */
extern const SportModule *const *const SPORTS;
extern const uint8_t NUM_SPORTS;

static const SportModule *sportOf(uint8_t idx) {
  return (idx < NUM_SPORTS) ? SPORTS[idx] : SPORTS[0];
}
static int sportIndexByKey(const char *key) {
  for (uint8_t i = 0; i < NUM_SPORTS; i++)
    if (!strcmp(SPORTS[i]->key, key)) return i;
  return -1;
}
/* resolve a native event name through a sport's map; falls back to a
   generic major-score event so an unknown name still animates. */
static void sportMapEvent(uint8_t sport, const char *native, MatchEvent &e) {
  const SportModule *s = sportOf(sport);
  for (uint8_t i = 0; i < s->numEvents; i++) {
    if (!strcmp(s->events[i].native, native)) {
      e.kind = s->events[i].kind;
      strlcpy(e.punch, s->events[i].punch, sizeof(e.punch));
      strlcpy(e.label, s->events[i].label, sizeof(e.label));
      return;
    }
  }
  e.kind = EV_SCORE_MAJOR;
  strlcpy(e.punch, "+1", sizeof(e.punch));
  strlcpy(e.label, native, sizeof(e.label));
}

/**********************  7d. FOLLOWS (/teams.json)  *******************/
/* A follow is either a TEAM or a whole LEAGUE. Both live in one array because
   they are the same thing to everything downstream: a source of live matches,
   with one EspnWatch slot tracking whatever it is currently watching.

   Sports are deliberately NOT a third kind. There are exactly NUM_SPORTS of
   them, so "follow a sport" is a bitmask in /config.json (sportOn) rather than
   a variable-length record — and per the design it adds no polling of its own,
   it enables/disables the leagues and teams already followed under it.

   `id` is the catalogue id ("ind", "ars"); `espn` is ESPN's numeric team id,
   learned when the team is added and used to build every provider URL. An
   empty espn means "catalogue-only" — the demo feed still works, the live
   provider skips it.

   `league` is ESPN's league slug or series id for THIS team ("esp.1", not the
   sport's default "eng.1"). It matters: the team endpoint answers 200 for a
   team looked up under the wrong league but returns an empty nextEvent[], so
   discovery fails silently and the card simply never appears. Empty means
   "use the sport module's default", which is right for the demo feed and for
   favourites added before this field existed. */
enum FollowKind : uint8_t { FOLLOW_TEAM = 0, FOLLOW_LEAGUE = 1 };

/* For a LEAGUE follow: `id` and `league` both hold ESPN's league slug or
   series id ("eng.1", "270559"), `name` its display name, `espn` is empty. */
struct FavTeam {
  String id; String name; String abbr; uint8_t sport; String espn; String league;
  uint8_t kind;
};
FavTeam favTeams[MAX_FOLLOWS];
int     numFavTeams = 0;

static inline bool followIsLeague(const FavTeam &f) { return f.kind == FOLLOW_LEAGUE; }

void saveTeams() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < numFavTeams; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["i"] = favTeams[i].id;
    o["n"] = favTeams[i].name;
    o["a"] = favTeams[i].abbr;
    o["s"] = favTeams[i].sport;
    o["e"] = favTeams[i].espn;
    o["l"] = favTeams[i].league;
    o["k"] = favTeams[i].kind;
  }
  File f = LittleFS.open("/teams.json", "w");
  serializeJson(doc, f);
  f.close();
}
void loadTeams() {
  numFavTeams = 0;
  if (!LittleFS.exists("/teams.json")) return;   // no defaults — the user picks
  File f = LittleFS.open("/teams.json", "r");
  JsonDocument doc;
  if (deserializeJson(doc, f) == DeserializationError::Ok) {
    for (JsonObject o : doc.as<JsonArray>()) {
      if (numFavTeams >= MAX_FOLLOWS) break;
      favTeams[numFavTeams++] = {
        o["i"].as<String>(),
        o["n"].is<const char*>() ? o["n"].as<String>() : String(""),
        o["a"].is<const char*>() ? o["a"].as<String>() : String("?"),
        (uint8_t)(o["s"].is<int>() ? o["s"].as<int>() : 0),
        o["e"].is<const char*>() ? o["e"].as<String>() : String(""),
        o["l"].is<const char*>() ? o["l"].as<String>() : String(""),
        /* absent in files written before leagues existed -> a team */
        (uint8_t)(o["k"].is<int>() ? o["k"].as<int>() : FOLLOW_TEAM)
      };
    }
  }
  f.close();
}
/* Matches either identifier: the demo feed labels sides with the catalogue id
   ("ind"), while the ESPN provider labels them with ESPN's numeric team id.
   Both must resolve to the same favourite or the card never auto-switches. */
static bool isFavTeam(uint8_t sport, const char *id) {
  if (!id || !*id) return false;
  for (int i = 0; i < numFavTeams; i++) {
    if (favTeams[i].sport != sport || followIsLeague(favTeams[i])) continue;
    if (favTeams[i].id == id) return true;
    if (favTeams[i].espn.length() && favTeams[i].espn == id) return true;
  }
  return false;
}
/* first favourite TEAM for a sport, or -1 — the demo feed builds matches
   around it, and a league follow gives it nothing to build from. */
static int firstFavOf(uint8_t sport) {
  for (int i = 0; i < numFavTeams; i++)
    if (favTeams[i].sport == sport && !followIsLeague(favTeams[i])) return i;
  return -1;
}
/* Is this sport switched on? sportOn is a bitmask over SPORTS[], default all
   ones, so an existing /config.json without the key behaves as before. */
static inline bool sportIsOn(uint8_t sport) {
  return (sportOnMask >> sport) & 1;
}

/**********************  7e. SHARED STATE  ****************************/
LiveMatch scoreBack[MAX_LIVE];    // core 0 writes
LiveMatch scorePrev[MAX_LIVE];    // core 0 only: previous poll, for diffing
LiveMatch scoreFront[MAX_LIVE];   // core 1 reads (renderer)
uint8_t   numBack = 0, numFront = 0;
int       activeMatch = -1;       // index into scoreFront, or -1
bool      scorePreview = false;   // dashboard "preview on panel"
/* Event id the user pinned from the dashboard's "what's live" list, or "".
   Kept as an id rather than an index because a poll can reorder scoreFront. */
char      scorePinned[24] = "";
uint32_t  lastScoreAt = 0;        // millis of the last successful poll
String    sportLastErr = "";

#define EV_RING 4
/* Last event pushed to the ring, kept after the animation is long gone. The
   only way to answer "did that four actually fire?" without racing a 2.5 s
   effect with a screenshot. */
char     evLastNative[16] = "";
uint32_t evLastAt = 0;
uint16_t evLastCount = 0;
MatchEvent evRing[EV_RING];
uint8_t    evCount = 0;           // guarded by sportsMutex

/* push an event (core 0, or core 1 for the dashboard's test button).
   Dedupes an identical event within 10 s and, when full, evicts the
   lowest-priority entry rather than dropping the newcomer blindly. */
static void evPushLocked(const MatchEvent &e) {
  for (uint8_t i = 0; i < evCount; i++)
    if (evRing[i].kind == e.kind && evRing[i].homeSide == e.homeSide &&
        !strcmp(evRing[i].native, e.native) && e.at - evRing[i].at < 10000) return;
  strlcpy(evLastNative, e.native, sizeof(evLastNative));
  evLastAt = millis();
  evLastCount++;
  if (evCount < EV_RING) { evRing[evCount++] = e; return; }
  uint8_t worst = 0;
  for (uint8_t i = 1; i < evCount; i++)
    if (evPriority(evRing[i].kind) < evPriority(evRing[worst].kind)) worst = i;
  if (evPriority(e.kind) > evPriority(evRing[worst].kind)) evRing[worst] = e;
}
void evPush(const MatchEvent &e) {
  if (!sportsMutex) return;
  xSemaphoreTake(sportsMutex, portMAX_DELAY);
  evPushLocked(e);
  xSemaphoreGive(sportsMutex);
}
/* highest priority waiting, or 0 if the ring is empty (core 1) */
uint8_t evPeekPriority() {
  if (!sportsMutex || !evCount) return 0;
  uint8_t best = 0;
  if (xSemaphoreTake(sportsMutex, 0) != pdTRUE) return 0;
  for (uint8_t i = 0; i < evCount; i++) {
    uint8_t p = evPriority(evRing[i].kind);
    if (p > best) best = p;
  }
  xSemaphoreGive(sportsMutex);
  return best;
}
/* pop the highest-priority event (core 1) */
bool evPop(MatchEvent &out) {
  if (!sportsMutex || !evCount) return false;
  if (xSemaphoreTake(sportsMutex, 0) != pdTRUE) return false;
  bool got = false;
  if (evCount) {
    uint8_t best = 0;
    for (uint8_t i = 1; i < evCount; i++)
      if (evPriority(evRing[i].kind) > evPriority(evRing[best].kind)) best = i;
    out = evRing[best];
    for (uint8_t i = best; i + 1 < evCount; i++) evRing[i] = evRing[i + 1];
    evCount--;
    got = true;
  }
  xSemaphoreGive(sportsMutex);
  return got;
}

/**********************  7f. EVENT DETECTION  *************************/
/* Builds the MatchEvent for a side and queues it. */
static void emitEvent(const LiveMatch &m, bool homeSide, const char *native) {
  MatchEvent e;
  memset(&e, 0, sizeof(e));
  e.sport    = m.sport;
  e.homeSide = homeSide;
  e.color    = homeSide ? m.home.color : m.away.color;
  e.at       = millis();
  strlcpy(e.native, native, sizeof(e.native));
  sportMapEvent(m.sport, native, e);
  evPushLocked(e);
}

/* Diff one match against its previous poll. Providers that report events do so
   via lastEvent/eventSeq; those that don't still get score-delta, lead-change
   and state-transition events derived here, so every source animates. */
static void sportsDiff(const LiveMatch &prev, const LiveMatch &cur, bool isNew) {
  if (isNew) return;                       // first sighting: nothing to compare

  bool reported = (cur.eventSeq != prev.eventSeq && cur.lastEvent[0]);
  if (reported) {
    // the reporting side is the one whose score moved, else the active side
    bool homeSide = (cur.home.score != prev.home.score ||
                     cur.home.score2 != prev.home.score2) ? true
                  : (cur.away.score != prev.away.score ||
                     cur.away.score2 != prev.away.score2) ? false
                  : cur.home.active;
    emitEvent(cur, homeSide, cur.lastEvent);
  } else {
    /* No event was reported, so one has to be inferred from the score. This
       is the normal path for the ESPN provider: its cheap endpoints carry
       scores, not plays. The sport module knows what a delta means (+3 is a
       three-pointer, +6 a touchdown, a wicket moves score2 while score may
       not), and "score" is the fallback every sport maps. */
    const SportModule *s = sportOf(cur.sport);
    int dh  = cur.home.score - prev.home.score;
    int da  = cur.away.score - prev.away.score;
    int dh2 = (cur.home.score2 != SCORE2_NONE && prev.home.score2 != SCORE2_NONE)
                ? cur.home.score2 - prev.home.score2 : 0;
    int da2 = (cur.away.score2 != SCORE2_NONE && prev.away.score2 != SCORE2_NONE)
                ? cur.away.score2 - prev.away.score2 : 0;
    if (dh > 0 || dh2 > 0) {
      const char *ev = s->eventForDelta ? s->eventForDelta(dh, dh2) : nullptr;
      emitEvent(cur, true, ev ? ev : "score");
    }
    if (da > 0 || da2 > 0) {
      const char *ev = s->eventForDelta ? s->eventForDelta(da, da2) : nullptr;
      emitEvent(cur, false, ev ? ev : "score");
    }
  }

  /* lead change — only once the scores are actually different */
  int prevLead = prev.home.score - prev.away.score;
  int curLead  = cur.home.score  - cur.away.score;
  if (curLead != 0 && ((prevLead <= 0 && curLead > 0) || (prevLead >= 0 && curLead < 0)))
    emitEvent(cur, curLead > 0, "lead");

  if (cur.state != prev.state) {
    if (cur.state == MS_ENDED)      emitEvent(cur, curLead >= 0, "final");
    else if (cur.state == MS_BREAK) emitEvent(cur, true, "period");
    // MS_SUSPENDED deliberately fires nothing: stumps is not a moment to animate
  }
}

/**********************  7g. PROVIDER SEAM  ***************************/
/* The one function a real data source replaces. Everything above and below is
   source-agnostic: fill `out` with up to maxN matches and set n.

   No real provider is wired yet — every free cricket/football tier allows
   ~100 requests/day while a live poll needs ~2,900, so that choice is
   deliberately deferred. The demo source below drives each sport module's own
   simulate() so all layouts and animations are reachable with no network. */

static LiveMatch demoPool[MAX_LIVE];
static uint8_t   demoCount = 0;
static bool      demoBuilt = false;

/* Build one demo match per sport the user follows, using their favourite as
   the home side. With no favourites at all we still make one match so the
   dashboard's preview button has something to show. */
static void demoBuild() {
  demoCount  = 0;
  demoBuilt  = true;
  uint32_t now = millis();
  for (uint8_t s = 0; s < NUM_SPORTS && demoCount < MAX_LIVE; s++) {
    int fav = firstFavOf(s);
    if (fav < 0) continue;
    const SportModule *mod = SPORTS[s];
    LiveMatch &m = demoPool[demoCount++];
    memset(&m, 0, sizeof(m));
    m.sport = s;
    m.state = MS_LIVE;
    snprintf(m.id, sizeof(m.id), "demo-%s", mod->key);
    strlcpy(m.home.id,   favTeams[fav].id.c_str(),   sizeof(m.home.id));
    strlcpy(m.home.abbr, favTeams[fav].abbr.c_str(), sizeof(m.home.abbr));
    strlcpy(m.home.name, favTeams[fav].name.c_str(), sizeof(m.home.name));
    m.home.color = mod->accent;                   // fallback if the id is stale
    for (uint8_t t = 0; t < mod->numTeams; t++)
      if (!strcmp(mod->catalogue[t].id, m.home.id)) { m.home.color = mod->catalogue[t].color; break; }
    for (uint8_t t = 0; t < mod->numTeams; t++)   // pick any other side
      if (strcmp(mod->catalogue[t].id, m.home.id)) {
        strlcpy(m.away.id,   mod->catalogue[t].id,   sizeof(m.away.id));
        strlcpy(m.away.abbr, mod->catalogue[t].abbr, sizeof(m.away.abbr));
        strlcpy(m.away.name, mod->catalogue[t].name, sizeof(m.away.name));
        m.away.color = mod->catalogue[t].color;
        break;
      }
    m.home.score2  = SCORE2_NONE;
    m.away.score2  = SCORE2_NONE;
    m.startedAt    = now;
  }
  if (!demoCount) {                      // no favourites yet — preview fallback
    const SportModule *mod = SPORTS[0];
    LiveMatch &m = demoPool[demoCount++];
    memset(&m, 0, sizeof(m));
    m.sport = 0;
    m.state = MS_LIVE;
    strlcpy(m.id, "demo-preview", sizeof(m.id));
    strlcpy(m.home.id,   mod->catalogue[0].id,   sizeof(m.home.id));
    strlcpy(m.home.abbr, mod->catalogue[0].abbr, sizeof(m.home.abbr));
    strlcpy(m.home.name, mod->catalogue[0].name, sizeof(m.home.name));
    m.home.color = mod->catalogue[0].color;
    strlcpy(m.away.id,   mod->catalogue[1].id,   sizeof(m.away.id));
    strlcpy(m.away.abbr, mod->catalogue[1].abbr, sizeof(m.away.abbr));
    strlcpy(m.away.name, mod->catalogue[1].name, sizeof(m.away.name));
    m.away.color  = mod->catalogue[1].color;
    m.home.score2 = m.away.score2 = SCORE2_NONE;
    m.startedAt   = now;
  }
}

static bool sportsPollDemo(LiveMatch *out, int maxN, int &n) {
  if (!demoBuilt) demoBuild();
  n = 0;
  for (uint8_t i = 0; i < demoCount && n < maxN; i++) {
    LiveMatch &m = demoPool[i];
    SPORTS[m.sport]->simulate(m, millis() - m.startedAt);
    out[n++] = m;
  }
  return true;
}

/* Live provider: the ESPN public API. Implemented in espn_api.h, which is
   included after this file because it needs LiveMatch — the same forward-decl
   pattern globals.h uses for handleTelegram()/fetchWeather(). Kept out of this
   file so sports_core.h stays free of HTTP and stays testable against demo. */
bool espnPoll(LiveMatch *out, int maxN, int &n);
bool espnFetchCatalogue(uint8_t sportIdx);      // called by sportsTask below
/* How soon the next poll matters: 2 = something is in its match window,
   1 = a kickoff is within the hour, 0 = nothing pending. Drives the poll
   cadence in sportsTask, which is what makes a newly followed team appear in
   seconds rather than at the next idle poll. */
uint8_t espnUrgency();
uint32_t espnLiveIntervalMs();   // the per-sport live tick (espn_api.h)

static bool sportsPollHttp(LiveMatch *out, int maxN, int &n) {
#if ESPN_ENABLE
  return espnPoll(out, maxN, n);
#else
  (void)out; (void)maxN;
  n = 0;
  sportLastErr = "built without ESPN_ENABLE";
  return false;
#endif
}

bool sportsPoll(LiveMatch *out, int maxN, int &n) {
  if (scorePreview || sportSrc == "demo") return sportsPollDemo(out, maxN, n);
  return sportsPollHttp(out, maxN, n);
}

/**********************  7h. POLL + TASK  *****************************/
void fetchScores() {
  LiveMatch fresh[MAX_LIVE];
  int n = 0;
  if (!sportsPoll(fresh, MAX_LIVE, n)) return;

  for (int i = 0; i < n; i++) {
    /* OR, never assign: a league follow and a pinned match have no favourite
       side for isFavTeam() to recognise, so the provider marks them eligible
       itself. Overwriting here made those matches invisible — polled, parsed
       and then silently dropped by sportsEligible(). The demo feed sets
       neither flag and still relies on isFavTeam() entirely. */
    fresh[i].home.fav = fresh[i].home.fav || isFavTeam(fresh[i].sport, fresh[i].home.id);
    fresh[i].away.fav = fresh[i].away.fav || isFavTeam(fresh[i].sport, fresh[i].away.id);
    fresh[i].home.color = teamInk(fresh[i].home.color);   // legibility floor
    fresh[i].away.color = teamInk(fresh[i].away.color);
  }

  if (!sportsMutex) return;
  xSemaphoreTake(sportsMutex, portMAX_DELAY);
  for (int i = 0; i < n; i++) {
    int prevIdx = -1;
    for (uint8_t j = 0; j < numBack; j++)
      if (!strcmp(scorePrev[j].id, fresh[i].id)) { prevIdx = j; break; }
    if (prevIdx >= 0) {
      // only animate for matches the user actually follows
      if (fresh[i].home.fav || fresh[i].away.fav)
        sportsDiff(scorePrev[prevIdx], fresh[i], false);
      fresh[i].changedAt = (fresh[i].eventSeq != scorePrev[prevIdx].eventSeq)
                             ? millis() : scorePrev[prevIdx].changedAt;
    } else {
      fresh[i].changedAt = millis();
    }
    scoreBack[i] = fresh[i];
    scorePrev[i] = fresh[i];
  }
  numBack     = n;
  lastScoreAt = millis();
  sportLastErr = "";
  xSemaphoreGive(sportsMutex);
}

/* core 0: same shape as weatherTask, with a fast cadence while a followed
   match is live and a lazy one otherwise. */
void sportsTask(void *pv) {
  vTaskDelay(pdMS_TO_TICKS(2500));         // let setup() finish first
  uint32_t lastFetch = 0;
  for (;;) {
    /* Cadence follows what is actually being WATCHED, not just what is on
       screen. The old rule keyed on activeMatch alone, so a match that had
       kicked off but had not yet produced a card was still polled at the idle
       10-minute rate — which is exactly why following a team mid-match could
       take ten minutes to appear. espnWatchLive() reports whether any watch is
       inside its match window; ESPN_LIVE_MS is the documented 15 s tick and
       until now was defined but never actually applied. */
    uint32_t every = 10UL * 60000UL;
#if ESPN_ENABLE
    uint8_t urgency = (sportSrc == "http") ? espnUrgency() : 0;
    if (activeMatch >= 0 || urgency == 2) every = espnLiveIntervalMs();
    else if (urgency == 1)                every = ESPN_PREGAME_MS;
#else
    if (activeMatch >= 0) every = ESPN_LIVE_MS;
#endif
    if (sportSrc == "demo" || scorePreview)
      every = (activeMatch >= 0) ? 30UL * 1000UL : 10UL * 60000UL;
    bool wantNet = (sportSrc != "demo" && !scorePreview);
    // While a card is frozen (/api/dev, /test) the poll must pause entirely:
    // sportsFreeze already stops sportsTick publishing, but fetchScores()
    // would still push the demo sim's own events into the 4-slot ring, where
    // they race with — and can evict — the event being tested.
    /* A catalogue refresh the dashboard asked for. Done here because this is
       the only task allowed to make these calls, and it hands the result back
       to core 1 to write (espn_api.h). */
#if ESPN_ENABLE
    if (espnCatWanted >= 0) {
      int want = espnCatWanted;
      espnCatWanted = -1;
      espnFetchCatalogue((uint8_t)want);
    }
#endif

    if (!sportsFreeze &&
        (sportEnable || scorePreview) &&
        (!wantNet || WiFi.status() == WL_CONNECTED) &&
        (sportsNow || !lastFetch || millis() - lastFetch > every)) {
      /* Stamp BEFORE the fetch, so `every` is a period rather than a gap.
         Stamping after added the fetch's own duration to it: a poll of two
         live NFL matches is 8 requests held apart by ESPN_MIN_GAP_MS, which
         takes ~15 s, so a configured 15 s tick was measured arriving every
         30-35 s. If a fetch overruns `every` the next one simply starts
         immediately, which is the right way for this to degrade. */
      lastFetch = millis();
      sportsNow = false;
      uint32_t t0 = millis();
      fetchScores();
      sportPollMs = millis() - t0;      // what the floor actually is, measured
    }
    vTaskDelay(pdMS_TO_TICKS(5000));       // short tick so sportsNow applies fast
  }
}

/**********************  7i. SUB-MODE STATE MACHINE  ******************/
/* Is this match allowed on the panel at all? */
static bool sportsEligible(const LiveMatch &m) {
  if (m.state != MS_LIVE && m.state != MS_BREAK) return false;
  if (!sportIsOn(m.sport)) return false;          // sport switched off
  if (scorePreview) return true;
  return m.home.fav || m.away.fav;                // set by the provider
}

/* A pinned match beats everything: the user asked for this one by hand. Held
   by event id rather than index because the poll can reorder scoreFront. */
static int sportsPinnedIdx() {
  if (!scorePinned[0]) return -1;
  for (uint8_t i = 0; i < numFront; i++)
    if (!strcmp(scoreFront[i].id, scorePinned)) {
      /* released the moment it stops being live — sportHoldMin then applies
         exactly as it does to any other finished match */
      if (scoreFront[i].state == MS_LIVE || scoreFront[i].state == MS_BREAK) return i;
      scorePinned[0] = 0;
      logLine("pin released (match ended)");
      return -1;
    }
  return -1;
}

/* Rotate: every eligible match gets a spell on the panel in turn.
   `most recently changed wins` was the old rule and it had a real failure —
   with two matches live the card could flip on every tick as their changedAt
   stamps raced. Rotation replaces that with a fixed dwell, and sportRotSec = 0
   restores the old behaviour for anyone who preferred it. */
static int sportsPickActive() {
  int pin = sportsPinnedIdx();
  if (pin >= 0) return pin;

  int      order[MAX_LIVE];
  uint8_t  n = 0;
  int      best = -1;
  uint32_t bestAt = 0;
  for (uint8_t i = 0; i < numFront && n < MAX_LIVE; i++) {
    if (!sportsEligible(scoreFront[i])) continue;
    order[n++] = i;
    if (best < 0 || scoreFront[i].changedAt > bestAt) {
      best = i; bestAt = scoreFront[i].changedAt;
    }
  }
  if (n <= 1 || sportRotSec <= 0) return best;    // nothing to rotate between

  static uint32_t turnStartedAt = 0;
  static char     turnId[24]    = "";
  /* find the current holder in this poll's list; if it is gone, start over */
  int cur = -1;
  for (uint8_t k = 0; k < n; k++)
    if (!strcmp(scoreFront[order[k]].id, turnId)) { cur = k; break; }
  if (cur < 0) {
    strlcpy(turnId, scoreFront[order[0]].id, sizeof(turnId));
    turnStartedAt = millis();
    return order[0];
  }
  /* Never cut an animation: the card is mid-effect and swapping matches now
     would show the tail of one match's goal on another match's scoreline. */
  if (millis() - turnStartedAt >= (uint32_t)sportRotSec * 1000UL && !scoreFxBusy) {
    cur = (cur + 1) % n;
    strlcpy(turnId, scoreFront[order[cur]].id, sizeof(turnId));
    turnStartedAt = millis();
  }
  return order[cur];
}

/* core 1, once per second from loop(): publish the latest poll and decide
   whether the clock face shows its ambient scene or the score card. */
void sportsTick() {
  static uint32_t holdUntil = 0;
  // /test and /api/dev?sport= both drive scoreFront themselves; publishing a
  // poll over the top would wipe the forced card within a second
  if (mode == MODE_TEST || sportsFreeze) return;

  if (sportsMutex && xSemaphoreTake(sportsMutex, 0) == pdTRUE) {
    if (numBack) memcpy(scoreFront, scoreBack, sizeof(LiveMatch) * numBack);
    numFront = numBack;                 // publish an empty poll too, or a
    xSemaphoreGive(sportsMutex);        // finished match would linger forever
  }

  uint8_t was = clockSub;
  if (!sportEnable && !scorePreview) {
    clockSub = SUB_AMBIENT; activeMatch = -1; holdUntil = 0;
  } else {
    int idx = sportsPickActive();
    if (idx >= 0) {
      activeMatch = idx; holdUntil = 0; clockSub = SUB_SCORE;
    } else if (activeMatch >= 0 && activeMatch < numFront &&
               (scoreFront[activeMatch].state == MS_ENDED ||
                scoreFront[activeMatch].state == MS_SUSPENDED)) {
      if (!holdUntil) holdUntil = millis() + (uint32_t)sportHoldMin * 60000UL;
      if ((int32_t)(millis() - holdUntil) < 0) clockSub = SUB_SCORE;
      else { clockSub = SUB_AMBIENT; activeMatch = -1; holdUntil = 0; }
    } else {
      clockSub = SUB_AMBIENT; activeMatch = -1; holdUntil = 0;
    }
  }
  // leaving the card: let the ambient scene restart cleanly instead of
  // resuming a sprite mid-walk from wherever it froze.
  if (was == SUB_SCORE && clockSub == SUB_AMBIENT) sceneNeedsReset = true;
}

/* Force a demo match of one sport onto the panel, ignoring favourites. Used by
   /test to walk every layout, and harmless outside it — ctlStop() and the next
   real poll both clear it. Core 1 only. */
void sportsDemoForce(uint8_t sport) {
  if (sport >= NUM_SPORTS) return;
  const SportModule *mod = SPORTS[sport];
  LiveMatch &m = scoreFront[0];
  memset(&m, 0, sizeof(m));
  m.sport = sport;
  m.state = MS_LIVE;
  snprintf(m.id, sizeof(m.id), "test-%s", mod->key);
  strlcpy(m.home.id,   mod->catalogue[0].id,   sizeof(m.home.id));
  strlcpy(m.home.abbr, mod->catalogue[0].abbr, sizeof(m.home.abbr));
  strlcpy(m.home.name, mod->catalogue[0].name, sizeof(m.home.name));
  m.home.color = teamInk(mod->catalogue[0].color);
  strlcpy(m.away.id,   mod->catalogue[1].id,   sizeof(m.away.id));
  strlcpy(m.away.abbr, mod->catalogue[1].abbr, sizeof(m.away.abbr));
  strlcpy(m.away.name, mod->catalogue[1].name, sizeof(m.away.name));
  m.away.color   = teamInk(mod->catalogue[1].color);
  m.home.score2  = m.away.score2 = SCORE2_NONE;
  m.home.fav     = true;
  m.startedAt    = millis();
  m.changedAt    = millis();
  mod->simulate(m, 45000);            // 45 s in: a match already under way
  numFront    = 1;
  activeMatch = 0;
  clockSub    = SUB_SCORE;
}

/* the match the renderer should draw, or nullptr */
const LiveMatch *activeMatchPtr() {
  if (activeMatch < 0 || activeMatch >= numFront) return nullptr;
  return &scoreFront[activeMatch];
}
