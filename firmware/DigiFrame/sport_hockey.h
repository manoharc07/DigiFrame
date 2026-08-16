/* DigiFrame — sport module: ice hockey.

   Low-scoring, so the score gets the big type; the interesting state is the
   period clock and the power play, which share the band row. */
#pragma once

static const TeamEntry HOCKEY_TEAMS[] = {
  {"tor", "Maple Leafs", "TOR", RGB565(  0,  32,  91)},
  {"mtl", "Canadiens",   "MTL", RGB565(175,  30,  45)},
  {"bos", "Bruins",      "BOS", RGB565(252, 181,  20)},
  {"nyr", "Rangers",     "NYR", RGB565(  0,  56, 168)},
  {"chi", "Blackhawks",  "CHI", RGB565(207,   0,  40)},
  {"det", "Red Wings",   "DET", RGB565(206,  17,  38)},
  {"col", "Avalanche",   "COL", RGB565(111,  38,  61)},
  {"vgk", "Golden Kn.",  "VGK", RGB565(185, 151,  91)},
  {"edm", "Oilers",      "EDM", RGB565(252,  76,   2)},
  {"tbl", "Lightning",   "TBL", RGB565(  0,  40, 104)},
};

static const EventMap HOCKEY_EVENTS[] = {
  {"goal",    EV_SCORE_MAJOR, "GOAL!", "GOAL"},
  {"score",   EV_SCORE_MAJOR, "GOAL!", "GOAL"},
  {"ppgoal",  EV_SCORE_MAJOR, "PPG!",  "PP GOAL"},
  {"hat",     EV_SCORE_BIG,   "HAT!",  "HAT-TRICK"},
  {"save",    EV_TURNOVER,    "SAVE",  "BIG SAVE"},
  {"penalty", EV_PENALTY,     "BOX",   "PENALTY"},
  {"pp",      EV_PENALTY,     "PP",    "POWER PLAY"},
  {"chal",    EV_REVIEW,      "RVW",   "CHALLENGE"},
  {"lead",    EV_LEAD_CHANGE, "LEAD",  "NEW LEAD"},
  {"period",  EV_PERIOD,      "END",   "END OF P"},
  {"final",   EV_FINAL,       "WON",   "FINAL"},
};

static void hockeyBody(const LiveMatch &m, uint32_t f) {
  gfxBodyStd(m, f);
  /* a power play runs a slow siren chase along the card edges for its duration */
  if (m.detail[0] == 'P' && m.detail[1] == 'P') {
    int p = (int)(f * 2) % (SW_H * 2);
    int y = SW_TOP + (p < SW_H ? p : SW_H * 2 - p);
    dma->drawPixel(3, y, RGB565(255, 90, 40));
    dma->drawPixel(PANEL_W - 4, SW_BOT - (y - SW_TOP), RGB565(255, 90, 40));
  }
}
static int hockeyProgress(const LiveMatch &m) { return gfxProgPeriods(m, 3, 20); }

/* --- bespoke: the goal lamp. A red beam sweeps the card the way the lamp
       behind the net does, with the puck streaking in ahead of it. --- */
static bool hockeyEvent(const LiveMatch &m, const MatchEvent &e, uint32_t f) {
  (void)m;
  if (strcmp(e.native, "goal") && strcmp(e.native, "score") && strcmp(e.native, "ppgoal"))
    return false;
  const uint8_t dur = EV_FRAMES[EV_SCORE_MAJOR];
  dma->fillRect(0, SW_TOP + 1, PANEL_W, SW_H - 1, RGB565(8, 8, 14));

  int lx = e.homeSide ? 8 : PANEL_W - 8, ly = SW_TOP + 6;
  dma->fillCircle(lx, ly, 3, RGB565(255, 40, 40));               // the lamp
  dma->fillCircle(lx, ly, 1, 0xFFFF);

  float ang = f * 0.55f;                                          // rotating beam
  for (int r = 4; r < 46; r++) {
    int bx = lx + (int)(r * cosf(ang));
    int by = ly + (int)(r * sinf(ang) * 0.6f);
    if (bx < 0 || bx >= PANEL_W || by < SW_TOP || by > SW_BOT) continue;
    dma->drawPixel(bx, by, gfxDim(RGB565(255, 60, 50), 90 - r));
    dma->drawPixel(bx, by + 1, gfxDim(RGB565(255, 60, 50), 55 - r));
  }
  if (f < 14) {                                                   // puck streak
    int px = e.homeSide ? 4 + (int)f * 4 : PANEL_W - 4 - (int)f * 4;
    for (int i = 0; i < 6; i++)
      dma->drawPixel(px + (e.homeSide ? -i * 2 : i * 2), 42, gfxDim(0xFFFF, 90 - i * 14));
    dma->fillRect(px - 1, 41, 3, 2, 0xFFFF);
  }
  if (f > 8) gfxPunch(e.punch, e.label, f - 8, dur - 8, e.color);
  return true;
}

/* --- demo feed: a 60-minute game, one minute every 2 s --- */
static void hockeySim(LiveMatch &m, uint32_t t) {
  const int FULL = 60;
  int minute = (int)(t / 2000);
  bool over = minute > FULL;
  if (minute > FULL) minute = FULL;

  int h = 0, a = 0;
  for (int i = 1; i <= minute; i++) {
    uint8_t r = prng8(i * 23 + 7);
    if (r < 18) { if (r & 1) h++; else a++; }
  }
  m.home.score = h; m.away.score = a;
  m.home.score2 = m.away.score2 = SCORE2_NONE;
  m.state = over ? MS_ENDED : ((minute == 20 || minute == 40) ? MS_BREAK : MS_LIVE);
  m.home.active = ((minute / 2) % 2) == 0;
  m.away.active = !m.home.active;

  int period = minute >= 40 ? 3 : (minute >= 20 ? 2 : 1);
  int left   = 20 - (minute % 20);
  if (over) strlcpy(m.period, "FIN", sizeof(m.period));
  else      snprintf(m.period, sizeof(m.period), "P%d %d'", period, left);

  bool pp = (prng8(minute * 3 + 11) < 60);
  if (pp) snprintf(m.detail, sizeof(m.detail), "PP 1:%02d", 59 - (minute % 60));
  else    snprintf(m.detail, sizeof(m.detail), "SOG %d", 10 + minute / 2);

  uint16_t prev = sdGet16(m, 0);
  if ((uint16_t)minute != prev) {
    sdSet16(m, 0, (uint16_t)minute);
    uint8_t r = prng8(minute * 23 + 7);
    if (over)                             simReport(m, "final",   h >= a);
    else if (r < 18)                      simReport(m, pp ? "ppgoal" : "goal", (r & 1));
    else if (minute == 20 || minute == 40) simReport(m, "period",  true);
    else if (r < 30)                      simReport(m, "penalty", (r & 2) != 0);
    else if (r < 40)                      simReport(m, "save",    (r & 2) != 0);
    else if (r < 45)                      simReport(m, "chal",    (r & 2) != 0);
    snprintf(m.ticker, sizeof(m.ticker), "%s %d  %s %d   P%d",
             m.home.abbr, h, m.away.abbr, a, period);
  }
}

static const char *hockeyDelta(int dScore, int dScore2) {
  (void)dScore2;
  return (dScore > 0) ? "goal" : nullptr;
}

static const SportModule SPORT_HOCKEY = {
  "hockey", "Ice Hockey", RGB565(120, 190, 255),
  HOCKEY_TEAMS,  (uint8_t)(sizeof(HOCKEY_TEAMS)  / sizeof(TeamEntry)),
  HOCKEY_EVENTS, (uint8_t)(sizeof(HOCKEY_EVENTS) / sizeof(EventMap)),
  hockeyBody, hockeyEvent, hockeySim,
  "hockey", "nhl", hockeyDelta,
  hockeyProgress, 15000UL   /* goals are sudden; the clock is worth keeping fresh */
};
