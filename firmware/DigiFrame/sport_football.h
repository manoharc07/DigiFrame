/* DigiFrame — sport module: football (soccer).

   Self-contained: catalogue, event map, its own body layout, one bespoke
   animation, and a demo feed. Plugged in by sports_registry.h. */
#pragma once

static const TeamEntry FOOTBALL_TEAMS[] = {
  {"ars", "Arsenal",     "ARS", RGB565(239,   1,   7)},
  {"che", "Chelsea",     "CHE", RGB565(  3,  70, 148)},
  {"liv", "Liverpool",   "LIV", RGB565(200,  16,  46)},
  {"mci", "Man City",    "MCI", RGB565(108, 171, 221)},
  {"mun", "Man United",  "MUN", RGB565(218,  41,  28)},
  {"tot", "Tottenham",   "TOT", RGB565(230, 230, 240)},
  {"bar", "Barcelona",   "BAR", RGB565(165,   0,  68)},
  {"rma", "Real Madrid", "RMA", RGB565(254, 186,   0)},
  {"juv", "Juventus",    "JUV", RGB565(200, 200, 200)},
  {"bay", "Bayern",      "BAY", RGB565(220,   5,  45)},
  {"psg", "Paris SG",    "PSG", RGB565(  0,  65, 148)},
  {"int", "Inter",       "INT", RGB565(  0, 102, 204)},
};

static const EventMap FOOTBALL_EVENTS[] = {
  {"goal",     EV_SCORE_MAJOR, "GOAL!", "GOAL"},
  {"score",    EV_SCORE_MAJOR, "GOAL!", "GOAL"},
  {"pengoal",  EV_SCORE_MAJOR, "PEN!",  "PENALTY"},
  {"owngoal",  EV_TURNOVER,    "O.G.",  "OWN GOAL"},
  {"hattrick", EV_SCORE_BIG,   "HAT!",  "HAT-TRICK"},
  {"yellow",   EV_PENALTY,     "CARD",  "YELLOW"},
  {"red",      EV_PENALTY,     "RED!",  "RED CARD"},
  {"var",      EV_REVIEW,      "VAR",   "VAR CHECK"},
  {"lead",     EV_LEAD_CHANGE, "LEAD",  "NEW LEAD"},
  {"period",   EV_PERIOD,      "HT",    "HALF TIME"},
  {"final",    EV_FINAL,       "FT",    "FULL TIME"},
};

/* --- layout: the score is the story, so it gets the big type --- */
static void footballBody(const LiveMatch &m, uint32_t f) { gfxBodyStd(m, f); }
/* 90 minutes, clock running up; stoppage time pins the comet at full. */
static int footballProgress(const LiveMatch &m) { return gfxProgMinutes(m, 90); }

/* --- bespoke: the net ripple. A goal deserves more than a colour flash, so
       we draw the netting and flex it outward from the point of impact, then
       sweep a crowd-wave sparkle across before the word lands. --- */
static bool footballEvent(const LiveMatch &m, const MatchEvent &e, uint32_t f) {
  if (strcmp(e.native, "goal") && strcmp(e.native, "score") && strcmp(e.native, "pengoal"))
    return false;                                 // everything else: generic

  const uint8_t dur = EV_FRAMES[EV_SCORE_MAJOR];
  dma->fillRect(0, SW_TOP + 1, PANEL_W, SW_H - 1, 0);
  float decay = 1.0f - (float)f / (float)dur;
  if (decay < 0) decay = 0;
  int ix = e.homeSide ? 18 : PANEL_W - 18, iy = 36;

  for (int gx = 2; gx < PANEL_W - 2; gx += 6) {         // vertical strands
    for (int gy = SW_TOP + 3; gy <= SW_BOT - 8; gy++) {
      float dx = gx - ix, dy = gy - iy;
      float d  = sqrtf(dx * dx + dy * dy);
      float amp = 5.0f * expf(-d / 24.0f) * decay;
      int   x   = gx + (int)(amp * sinf(d * 0.45f - f * 0.9f));
      if (x >= 0 && x < PANEL_W)
        dma->drawPixel(x, gy, gfxDim(0xFFFF, 18 + (int)(amp * 9)));
    }
  }
  for (int gy = SW_TOP + 4; gy <= SW_BOT - 8; gy += 6) { // horizontal strands
    for (int gx = 1; gx < PANEL_W - 1; gx++) {
      float dx = gx - ix, dy = gy - iy;
      float d  = sqrtf(dx * dx + dy * dy);
      float amp = 4.0f * expf(-d / 24.0f) * decay;
      int   y   = gy + (int)(amp * sinf(d * 0.45f - f * 0.9f));
      if (y >= SW_TOP && y <= SW_BOT)
        dma->drawPixel(gx, y, gfxDim(e.color, 22 + (int)(amp * 10)));
    }
  }
  if (f < 12) dma->fillCircle(ix, iy, 3 - f / 5, 0xFFFF);   // the ball, still in the net

  if (f > 8) {                                    // crowd wave across the band
    int wx = (int)(f - 8) * (PANEL_W + 12) / (dur - 8);
    for (int i = 0; i < 5; i++)
      drawSpark(wx - i * 3, SW_TOP + 4 + (int)(prng8(i * 13 + (f & 7)) % 6), gfxDim(0xFFFF, 90 - i * 15));
  }
  if (f > 6) gfxPunch(e.punch, e.label, f - 6, dur - 6, e.color);
  return true;
}

/* --- demo feed: a 90-minute match compressed into three real minutes --- */
static void footballSim(LiveMatch &m, uint32_t t) {
  const int FULL = 90;
  int minute = (int)(t / 2000);
  bool over = minute > FULL;
  if (minute > FULL) minute = FULL;

  int h = 0, a = 0;
  for (int i = 1; i <= minute; i++) {
    uint8_t r = prng8(i * 7 + 11);
    if (r < 14) { if (r & 1) h++; else a++; }
  }
  m.home.score = h; m.away.score = a;
  m.home.score2 = m.away.score2 = SCORE2_NONE;
  m.state = over ? MS_ENDED : (minute == 45 ? MS_BREAK : MS_LIVE);
  /* Shaped like the real provider: half in `period`, running clock in
     `detail`. It used to put the minute in `period` and possession % in
     `detail` — furniture the card's own design rules reject, and a shape the
     ESPN path never produces, so Preview demonstrated a card that could not
     occur. It also fed footballProgress() the possession figure as a minute. */
  if (over)              { strlcpy(m.period, "FT", sizeof(m.period)); m.detail[0] = 0; }
  else if (minute == 45) { strlcpy(m.period, "HT", sizeof(m.period)); m.detail[0] = 0; }
  else {
    snprintf(m.period, sizeof(m.period), "H%d", minute > 45 ? 2 : 1);
    snprintf(m.detail, sizeof(m.detail), "%d'", minute);
  }
  m.home.active = ((minute / 3) % 2) == 0;
  m.away.active = !m.home.active;

  uint16_t prevMin = sdGet16(m, 0);
  if ((uint16_t)minute != prevMin) {
    sdSet16(m, 0, (uint16_t)minute);
    uint8_t r = prng8(minute * 7 + 11);
    if (over)                simReport(m, "final",  h >= a);
    else if (r < 14)         simReport(m, "goal",   (r & 1));
    else if (minute == 45)   simReport(m, "period", true);
    else if (r < 22)         simReport(m, "yellow", (r & 2) != 0);
    else if (r < 28)         simReport(m, "var",    (r & 2) != 0);
    else if (r < 32)         simReport(m, "red",    (r & 2) != 0);

    if (!strcmp(m.lastEvent, "goal"))
      snprintf(m.ticker, sizeof(m.ticker), "GOAL %s %d'  %d-%d",
               (r & 1) ? m.home.abbr : m.away.abbr, minute, h, a);
    else if (!strcmp(m.lastEvent, "final"))
      snprintf(m.ticker, sizeof(m.ticker), "FULL TIME  %s %d-%d %s",
               m.home.abbr, h, a, m.away.abbr);
    else if (m.lastEvent[0])
      snprintf(m.ticker, sizeof(m.ticker), "%s %d'  %s %d-%d %s",
               m.lastEvent, minute, m.home.abbr, h, a, m.away.abbr);
  }
  if (!m.ticker[0])
    snprintf(m.ticker, sizeof(m.ticker), "%s v %s", m.home.name, m.away.name);
}

/* One goal at a time; a 2+ jump means we missed a poll, so still one goal. */
static const char *footballDelta(int dScore, int dScore2) {
  (void)dScore2;
  return (dScore > 0) ? "goal" : nullptr;
}

static const SportModule SPORT_FOOTBALL = {
  "football", "Football", RGB565(60, 200, 110),
  FOOTBALL_TEAMS,  (uint8_t)(sizeof(FOOTBALL_TEAMS)  / sizeof(TeamEntry)),
  FOOTBALL_EVENTS, (uint8_t)(sizeof(FOOTBALL_EVENTS) / sizeof(EventMap)),
  footballBody, footballEvent, footballSim,
  "soccer", "eng.1", footballDelta,
  footballProgress, 20000UL   /* goals are rare and the clock is coarse */
};
