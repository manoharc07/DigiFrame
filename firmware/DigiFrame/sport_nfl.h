/* DigiFrame — sport module: American football.

   Shares the standard body with the other four clock sports; what is its own
   is the possession chevron, fed by the 615-byte /situation endpoint. */
#pragma once

static const TeamEntry NFL_TEAMS[] = {
  {"kc",  "Chiefs",    "KC",  RGB565(227,  24,  55)},
  {"sf",  "49ers",     "SF",  RGB565(170,   0,   0)},
  {"dal", "Cowboys",   "DAL", RGB565(  0,  53, 148)},
  {"gb",  "Packers",   "GB",  RGB565( 24,  48,  40)},
  {"ne",  "Patriots",  "NE",  RGB565(  0,  34,  68)},
  {"phi", "Eagles",    "PHI", RGB565(  0,  76,  84)},
  {"buf", "Bills",     "BUF", RGB565(  0,  51, 141)},
  {"bal", "Ravens",    "BAL", RGB565( 26,  25,  95)},
  {"det", "Lions",     "DET", RGB565(  0, 118, 182)},
  {"mia", "Dolphins",  "MIA", RGB565(  0, 142, 151)},
  {"sea", "Seahawks",  "SEA", RGB565(   0, 34,  68)},
  {"pit", "Steelers",  "PIT", RGB565(255, 182,  18)},
};

static const EventMap NFL_EVENTS[] = {
  {"td",     EV_SCORE_MAJOR, "TD!",  "TOUCHDOWN"},
  {"score",  EV_SCORE_MAJOR, "TD!",  "TOUCHDOWN"},
  {"twopt",  EV_SCORE_BIG,   "2PT",  "TWO POINT"},
  {"fg",     EV_SCORE_MINOR, "FG",   "FIELD GOAL"},
  {"xp",     EV_SCORE_MINOR, "+1",   "EXTRA PT"},
  {"safety", EV_SCORE_MINOR, "SAFE", "SAFETY"},
  {"int",    EV_TURNOVER,    "PICK", "INTERCEPT"},
  {"fumble", EV_TURNOVER,    "FUMB", "FUMBLE"},
  {"sack",   EV_TURNOVER,    "SACK", "SACK"},
  {"flag",   EV_PENALTY,     "FLAG", "PENALTY"},
  {"chal",   EV_REVIEW,      "CHAL", "CHALLENGE"},
  {"lead",   EV_LEAD_CHANGE, "LEAD", "NEW LEAD"},
  {"period", EV_PERIOD,      "END",  "END OF Q"},
  {"final",  EV_FINAL,       "WON",  "FINAL"},
};

static void nflBody(const LiveMatch &m, uint32_t f) {
  gfxBodyStd(m, f);

  /* Possession chevron beside whoever has the ball. This used to be drawn
     unconditionally on the away side, because `active` was only ever set by
     the cricket branch of the provider — a marker that looked like data and
     was not. It is now fed by the 615-byte /situation endpoint. */
  if (m.home.active || m.away.active) {
    int cx = m.home.active ? 5 + gfxTextW(m.home.abbr, 1) + 2
                           : PANEL_W - 7 - gfxTextW(m.away.abbr, 1) - 2;
    for (int i = 0; i < 3; i++)
      dma->drawPixel(cx + i, SW_ABBR + 2 + (i < 2 ? i : 1), RGB565(255, 220, 90));
  }
  /* The field-position bar that lived here read m.sportData[4], which only the
     demo simulator ever wrote — so on every real match its marker sat pinned
     at yard 0. Match progress now occupies the band, in the clock's own idiom
     and fed by data that actually arrives. */
}
/* Four quarters of 15 minutes. */
static int nflProgress(const LiveMatch &m) { return gfxProgPeriods(m, 4, 15); }

/* --- bespoke: the touchdown. The ball spirals through the uprights and the
       field bar floods to the end zone. --- */
static bool nflEvent(const LiveMatch &m, const MatchEvent &e, uint32_t f) {
  (void)m;
  if (strcmp(e.native, "td") && strcmp(e.native, "score")) return false;
  const uint8_t dur = EV_FRAMES[EV_SCORE_MAJOR];
  dma->fillRect(0, SW_TOP + 1, PANEL_W, SW_H - 1, 0);

  int gx = 32, gy = 26;                              // goalposts
  uint16_t post = RGB565(255, 210, 90);
  dma->drawFastHLine(gx - 9, gy + 10, 18, post);
  dma->drawFastVLine(gx - 9, gy, 11, post);
  dma->drawFastVLine(gx + 8, gy, 11, post);
  dma->drawFastVLine(gx, gy + 10, 8, gfxDim(post, 60));

  if (f < 20) {                                      // ball, spiralling
    int bx = 8 + (int)f * 2;
    int by = 48 - (int)(f * 2) + (int)(f * f) / 22;
    int w  = ((f / 2) % 2) ? 4 : 2;
    dma->fillRect(bx - w / 2, by - 1, w, 3, RGB565(150, 80, 40));
    dma->drawPixel(bx, by, 0xFFFF);
  } else {
    dma->fillRect(4, 50, PANEL_W - 8, 5, gfxDim(e.color, 40 + (int)(f - 20) * 3));
  }
  if (f > 10) gfxPunch(e.punch, e.label, f - 10, dur - 10, e.color);
  return true;
}

/* --- demo feed: a play every 1.5 s --- */
static void nflSim(LiveMatch &m, uint32_t t) {
  const int PLAYS = 100;
  int step = (int)(t / 1500);
  bool over = step >= PLAYS;
  if (over) step = PLAYS - 1;

  int h = 0, a = 0;
  for (int i = 0; i <= step; i++) {
    uint8_t r = prng8(i * 17 + 3);
    bool home = ((i / 4) % 2) == 0;
    int pts = (r < 200) ? 0 : (r < 232) ? 3 : 7;
    if (home) h += pts; else a += pts;
  }
  m.home.score = h; m.away.score = a;
  m.home.score2 = m.away.score2 = SCORE2_NONE;
  m.home.active = ((step / 4) % 2) == 0;
  m.away.active = !m.home.active;
  m.state = over ? MS_ENDED : MS_LIVE;
  m.sportData[4] = (uint8_t)(prng8(step * 5 + 1) % 101);

  int q = 1 + step * 4 / PLAYS; if (q > 4) q = 4;
  snprintf(m.period, sizeof(m.period), over ? "FIN" : "Q%d", q);
  /* the game clock, as the provider sends it — down & distance never reaches
     this field on the real path, and nflProgress() reads it as minutes left */
  int left = 15 - (step * 4 % PLAYS) * 15 / (PLAYS / 4);
  if (left < 0) left = 0;
  snprintf(m.detail, sizeof(m.detail), "%d:%02d", left, (int)(prng8(step) % 60));

  uint16_t prev = sdGet16(m, 0);
  if ((uint16_t)step != prev) {
    sdSet16(m, 0, (uint16_t)step);
    uint8_t r = prng8(step * 17 + 3);
    bool home = m.home.active;
    if (over)          simReport(m, "final",  h >= a);
    else if (r >= 232) simReport(m, "td",     home);
    else if (r >= 200) simReport(m, "fg",     home);
    else if (r < 10)   simReport(m, "int",    !home);
    else if (r < 18)   simReport(m, "fumble", !home);
    else if (r < 30)   simReport(m, "sack",   !home);
    else if (r < 40)   simReport(m, "flag",   home);
    else if (r < 45)   simReport(m, "chal",   home);
    snprintf(m.ticker, sizeof(m.ticker), "%s %d  %s %d   %s",
             m.home.abbr, h, m.away.abbr, a, m.detail);
  }
}

/* 7 is the common touchdown-plus-extra-point jump between two polls. */
static const char *nflDelta(int dScore, int dScore2) {
  (void)dScore2;
  if (dScore == 6 || dScore == 7 || dScore == 8) return "td";
  if (dScore == 3) return "fg";
  if (dScore == 2) return "twopt";
  if (dScore == 1) return "xp";
  if (dScore >  0) return "td";
  return nullptr;
}

static const SportModule SPORT_NFL = {
  "nfl", "Am. Football", RGB565(200, 160, 90),
  NFL_TEAMS,  (uint8_t)(sizeof(NFL_TEAMS)  / sizeof(TeamEntry)),
  NFL_EVENTS, (uint8_t)(sizeof(NFL_EVENTS) / sizeof(EventMap)),
  nflBody, nflEvent, nflSim,
  "football", "nfl", nflDelta,
  nflProgress, 15000UL   /* scores arrive in bursts between long stoppages */
};
