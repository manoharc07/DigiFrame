/* DigiFrame — sport module: basketball.

   High-scoring and fast, so the layout leans on the auto-sizing score line
   (three digits a side) and gives a row to the shot clock rather than to
   decoration. */
#pragma once

static const TeamEntry BASKETBALL_TEAMS[] = {
  {"bos", "Celtics",   "BOS", RGB565(  0, 122,  51)},
  {"lal", "Lakers",    "LAL", RGB565(253, 185,  39)},
  {"gsw", "Warriors",  "GSW", RGB565( 29,  66, 138)},
  {"mia", "Heat",      "MIA", RGB565(152,   0,  46)},
  {"chi", "Bulls",     "CHI", RGB565(206,  17,  65)},
  {"nyk", "Knicks",    "NYK", RGB565(245, 132,  38)},
  {"den", "Nuggets",   "DEN", RGB565( 13,  34,  64)},
  {"mil", "Bucks",     "MIL", RGB565(  0,  71,  27)},
  {"phx", "Suns",      "PHX", RGB565(229,  95,  32)},
  {"dal", "Mavericks", "DAL", RGB565(  0,  83, 188)},
  {"phi", "76ers",     "PHI", RGB565(  0, 107, 182)},
  {"okc", "Thunder",   "OKC", RGB565(  0, 125, 195)},
};

static const EventMap BASKETBALL_EVENTS[] = {
  {"three",  EV_SCORE_BIG,   "3PTS", "THREE"},
  {"buzzer", EV_SCORE_BIG,   "BUZZ", "BUZZER"},
  {"dunk",   EV_SCORE_MAJOR, "DUNK", "DUNK"},
  {"score",  EV_SCORE_MAJOR, "+2",   "BASKET"},
  {"ft",     EV_SCORE_MINOR, "+1",   "FREE TH"},
  {"steal",  EV_TURNOVER,    "STEAL","TURNOVER"},
  {"block",  EV_TURNOVER,    "BLOCK","BLOCK"},
  {"dd",     EV_MILESTONE,   "DBL",  "DOUBLE"},
  {"foul",   EV_PENALTY,     "FOUL", "TECH FOUL"},
  {"review", EV_REVIEW,      "RVW",  "REVIEW"},
  {"lead",   EV_LEAD_CHANGE, "LEAD", "NEW LEAD"},
  {"period", EV_PERIOD,      "END",  "END OF Q"},
  {"final",  EV_FINAL,       "WON",  "FINAL"},
};

static void basketballBody(const LiveMatch &m, uint32_t f) {
  gfxBodyStd(m, f);
}
/* Four quarters of 12 minutes; overtime pins the comet at full rather than
   overflowing it. */
static int basketballProgress(const LiveMatch &m) { return gfxProgPeriods(m, 4, 12); }

/* --- bespoke: the swish. Ball arcs into a hoop drawn at the far corner and
       the net flicks as it drops through. --- */
static bool basketballEvent(const LiveMatch &m, const MatchEvent &e, uint32_t f) {
  (void)m;
  if (strcmp(e.native, "three") && strcmp(e.native, "buzzer")) return false;
  const uint8_t dur = EV_FRAMES[EV_SCORE_BIG];
  dma->fillRect(0, SW_TOP + 1, PANEL_W, SW_H - 1, 0);

  int hx = e.homeSide ? PANEL_W - 12 : 12, hy = 30;
  dma->drawFastHLine(hx - 5, hy, 11, RGB565(255, 120, 40));     // rim
  for (int i = 0; i < 5; i++) {                                  // net
    int flick = (f > 16 && f < 26) ? ((i + (int)f) % 2 ? 1 : -1) : 0;
    dma->drawFastVLine(hx - 4 + i * 2 + flick, hy + 1, 4, gfxDim(0xFFFF, 55));
  }
  if (f < 18) {                                                  // ball on its arc
    int sx = e.homeSide ? 6 : PANEL_W - 6;
    int bx = sx + (hx - sx) * (int)f / 18;
    int by = 50 - (int)(f * 3) + (int)(f * f) / 12;
    dma->fillCircle(bx, by, 2, RGB565(255, 140, 50));
    dma->drawPixel(bx, by - 2, RGB565(120, 60, 20));
  }
  if (f > 18) gfxRing(hx, hy + 4, f - 18, dur - 18, e.color);
  if (f > 14) gfxPunch(e.punch, e.label, f - 14, dur - 14, e.color);
  return true;
}

/* --- demo feed: possessions every 700 ms --- */
static void basketballSim(LiveMatch &m, uint32_t t) {
  const int POSS = 120;
  int step = (int)(t / 700);
  bool over = step >= POSS;
  if (over) step = POSS - 1;

  int h = 0, a = 0;
  for (int i = 0; i <= step; i++) {
    uint8_t r = prng8(i * 11 + 5);
    bool home = (i % 2) == 0;
    int pts = (r < 90) ? 0 : (r < 190) ? 2 : (r < 235) ? 3 : 1;
    if (home) h += pts; else a += pts;
  }
  m.home.score = h; m.away.score = a;
  m.home.score2 = m.away.score2 = SCORE2_NONE;
  m.home.active = (step % 2) == 0;
  m.away.active = !m.home.active;
  m.state = over ? MS_ENDED : MS_LIVE;

  int q = 1 + step * 4 / POSS; if (q > 4) q = 4;
  int secs = 720 - (step * 4 % (POSS / 4 + 1)) * 24;
  if (secs < 0) secs = 0;
  snprintf(m.period, sizeof(m.period), over ? "FIN" : "Q%d", q);
  snprintf(m.detail, sizeof(m.detail), "%d:%02d", secs / 60, secs % 60);

  uint16_t prev = sdGet16(m, 0);
  if ((uint16_t)step != prev) {
    sdSet16(m, 0, (uint16_t)step);
    uint8_t r = prng8(step * 11 + 5);
    bool home = (step % 2) == 0;
    if (over)            simReport(m, "final",  h >= a);
    else if (r >= 235)   simReport(m, "ft",     home);
    else if (r >= 190)   simReport(m, "three",  home);
    else if (r >= 90)    simReport(m, (r % 7 == 0) ? "dunk" : "score", home);
    else if (r < 12)     simReport(m, "steal",  !home);
    else if (r < 20)     simReport(m, "foul",   !home);
    else if (r < 26)     simReport(m, "block",  !home);
    else if (r < 30)     simReport(m, "review", home);

    char ch = (r >= 235) ? 'F' : (r >= 190) ? '3' : (r >= 90) ? '2' : 'X';
    stripPush(m, ch);
    snprintf(m.ticker, sizeof(m.ticker), "%s %d  %s %d   Q%d",
             m.home.abbr, h, m.away.abbr, a, q);
  }
}

static const char *basketballDelta(int dScore, int dScore2) {
  (void)dScore2;
  if (dScore >= 3) return "three";
  if (dScore == 2) return "score";
  if (dScore == 1) return "ft";
  return nullptr;
}

static const SportModule SPORT_BASKETBALL = {
  "basketball", "Basketball", RGB565(255, 150, 60),
  BASKETBALL_TEAMS,  (uint8_t)(sizeof(BASKETBALL_TEAMS)  / sizeof(TeamEntry)),
  BASKETBALL_EVENTS, (uint8_t)(sizeof(BASKETBALL_EVENTS) / sizeof(EventMap)),
  basketballBody, basketballEvent, basketballSim,
  "basketball", "nba", basketballDelta,
  basketballProgress, 10000UL   /* the score moves every few seconds */
};
