/* DigiFrame — sport module: rugby.

   Scores move in 3s, 5s and 7s, so the band row carries the type of the last
   score alongside the clock — that is what tells you how the game is going. */
#pragma once

static const TeamEntry RUGBY_TEAMS[] = {
  {"nzl", "All Blacks", "NZL", RGB565( 20,  20,  20)},
  {"rsa", "Springboks", "RSA", RGB565(  0, 110,  70)},
  {"irl", "Ireland",    "IRL", RGB565(  0, 130,  80)},
  {"fra", "France",     "FRA", RGB565( 20,  60, 160)},
  {"eng", "England",    "ENG", RGB565(230, 230, 240)},
  {"wal", "Wales",      "WAL", RGB565(200,  20,  40)},
  {"sco", "Scotland",   "SCO", RGB565( 20,  60, 130)},
  {"aus", "Wallabies",  "AUS", RGB565(255, 200,   0)},
  {"arg", "Argentina",  "ARG", RGB565(110, 180, 230)},
  {"ita", "Italy",      "ITA", RGB565( 20,  80, 190)},
};

static const EventMap RUGBY_EVENTS[] = {
  {"try",     EV_SCORE_MAJOR, "TRY!", "TRY"},
  {"score",   EV_SCORE_MAJOR, "TRY!", "TRY"},
  {"con",     EV_SCORE_MINOR, "CON",  "CONVERT"},
  {"pen",     EV_SCORE_MINOR, "PEN",  "PENALTY"},
  {"drop",    EV_SCORE_BIG,   "DROP", "DROP GOAL"},
  {"turn",    EV_TURNOVER,    "TURN", "TURNOVER"},
  {"yellow",  EV_PENALTY,     "BIN",  "SIN BIN"},
  {"red",     EV_PENALTY,     "RED!", "RED CARD"},
  {"tmo",     EV_REVIEW,      "TMO",  "TMO CHECK"},
  {"lead",    EV_LEAD_CHANGE, "LEAD", "NEW LEAD"},
  {"period",  EV_PERIOD,      "HT",   "HALF TIME"},
  {"final",   EV_FINAL,       "WON",  "FULL TIME"},
};

static void rugbyBody(const LiveMatch &m, uint32_t f) { gfxBodyStd(m, f); }
static int rugbyProgress(const LiveMatch &m) { return gfxProgMinutes(m, 80); }

/* --- bespoke: the try. The ball is grounded under the posts and a puff of
       dust lifts off the turf. --- */
static bool rugbyEvent(const LiveMatch &m, const MatchEvent &e, uint32_t f) {
  (void)m;
  if (strcmp(e.native, "try") && strcmp(e.native, "score")) return false;
  const uint8_t dur = EV_FRAMES[EV_SCORE_MAJOR];
  dma->fillRect(0, SW_TOP + 1, PANEL_W, SW_H - 1, RGB565(6, 20, 10));

  uint16_t post = gfxDim(0xFFFF, 65);                 // posts
  dma->drawFastVLine(24, SW_TOP + 4, 22, post);
  dma->drawFastVLine(40, SW_TOP + 4, 22, post);
  dma->drawFastHLine(24, SW_TOP + 12, 17, post);
  dma->drawFastHLine(2, 52, PANEL_W - 4, RGB565(40, 90, 50));   // try line

  int t = (int)f;
  int bx = 10 + (t < 14 ? t * 2 : 28);
  int by = 44 + (t < 14 ? 0 : 6);
  dma->fillRect(bx - 2, by - 1, 5, 3, RGB565(160, 110, 60));     // the ball
  if (t >= 14) {                                                 // dust puff
    for (int i = 0; i < 14; i++) {
      int a  = (i * 26) % 360;
      int rr = (t - 14) * 2;
      int dx = bx + (int)(rr * cos(a * PI / 180.0));
      int dy = by + (int)(rr * sin(a * PI / 180.0) / 2);
      if (dx > 0 && dx < PANEL_W && dy > SW_TOP && dy < SW_BOT)
        dma->drawPixel(dx, dy, gfxDim(RGB565(200, 190, 150), 90 - rr * 3));
    }
  }
  if (f > 12) gfxPunch(e.punch, e.label, f - 12, dur - 12, e.color);
  return true;
}

/* --- demo feed: 80 minutes, one minute every 2 s --- */
static void rugbySim(LiveMatch &m, uint32_t t) {
  const int FULL = 80;
  int minute = (int)(t / 2000);
  bool over = minute > FULL;
  if (minute > FULL) minute = FULL;

  int h = 0, a = 0;
  const char *lastType = "";
  for (int i = 1; i <= minute; i++) {
    uint8_t r = prng8(i * 19 + 13);
    int pts = (r < 14) ? 5 : (r < 26) ? 3 : 0;
    if (!pts) continue;
    if (r & 1) h += pts; else a += pts;
    lastType = (pts == 5) ? "TRY" : "PEN";
  }
  m.home.score = h; m.away.score = a;
  m.home.score2 = m.away.score2 = SCORE2_NONE;
  m.state = over ? MS_ENDED : (minute == 40 ? MS_BREAK : MS_LIVE);
  m.home.active = ((minute / 4) % 2) == 0;
  m.away.active = !m.home.active;

  if (over)              strlcpy(m.period, "FT", sizeof(m.period));
  else if (minute == 40) strlcpy(m.period, "HT", sizeof(m.period));
  else                   snprintf(m.period, sizeof(m.period), "H%d %d'",
                                  minute > 40 ? 2 : 1, minute);
  strlcpy(m.detail, lastType, sizeof(m.detail));

  uint16_t prev = sdGet16(m, 0);
  if ((uint16_t)minute != prev) {
    sdSet16(m, 0, (uint16_t)minute);
    uint8_t r = prng8(minute * 19 + 13);
    if (over)              simReport(m, "final",  h >= a);
    else if (r < 14)       simReport(m, "try",    (r & 1));
    else if (r < 26)       simReport(m, "pen",    (r & 1));
    else if (minute == 40) simReport(m, "period", true);
    else if (r < 34)       simReport(m, "turn",   (r & 2) != 0);
    else if (r < 40)       simReport(m, "tmo",    (r & 2) != 0);
    else if (r < 45)       simReport(m, "yellow", (r & 2) != 0);
    else if (r < 48)       simReport(m, "drop",   (r & 1));
    snprintf(m.ticker, sizeof(m.ticker), "%s %d  %s %d   %d'",
             m.home.abbr, h, m.away.abbr, a, minute);
  }
}

/* 7 = converted try; 5 = unconverted; 3 = penalty or drop goal; 2 = the
   conversion arriving in its own poll after the try. */
static const char *rugbyDelta(int dScore, int dScore2) {
  (void)dScore2;
  if (dScore >= 5) return "try";
  if (dScore == 3) return "pen";
  if (dScore == 2) return "con";
  return nullptr;
}

static const SportModule SPORT_RUGBY = {
  "rugby", "Rugby", RGB565(140, 220, 160),
  RUGBY_TEAMS,  (uint8_t)(sizeof(RUGBY_TEAMS)  / sizeof(TeamEntry)),
  RUGBY_EVENTS, (uint8_t)(sizeof(RUGBY_EVENTS) / sizeof(EventMap)),
  rugbyBody, rugbyEvent, rugbySim,
  "rugby", "270559", rugbyDelta,
  rugbyProgress, 20000UL   /* phases are long and scores infrequent */
};
