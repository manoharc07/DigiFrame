/* DigiFrame — sport module: cricket.

   Cricket carries more state than any other sport here (two innings, wickets,
   overs, run rate, the rhythm of the last six balls), so it gets its own
   two-innings layout rather than sharing the simple score line. */
#pragma once

static const TeamEntry CRICKET_TEAMS[] = {
  {"ind", "India",        "IND", RGB565( 20, 110, 255)},
  {"aus", "Australia",    "AUS", RGB565(255, 210,   0)},
  {"eng", "England",      "ENG", RGB565( 60, 130, 255)},
  {"pak", "Pakistan",     "PAK", RGB565(  0, 160,  90)},
  {"nz",  "New Zealand",  "NZ",  RGB565( 20,  20,  20)},
  {"sa",  "South Africa", "SA",  RGB565(  0, 150, 110)},
  {"sl",  "Sri Lanka",    "SL",  RGB565( 20,  60, 160)},
  {"ban", "Bangladesh",   "BAN", RGB565(  0, 130,  80)},
  {"wi",  "West Indies",  "WI",  RGB565(140,  20,  40)},
  {"afg", "Afghanistan",  "AFG", RGB565( 40, 120, 200)},
  {"csk", "Chennai",      "CSK", RGB565(255, 200,   0)},
  {"mi",  "Mumbai",       "MI",  RGB565( 30,  90, 200)},
  {"rcb", "Bangalore",    "RCB", RGB565(220,  30,  40)},
  {"kkr", "Kolkata",      "KKR", RGB565(130,  70, 190)},
};

static const EventMap CRICKET_EVENTS[] = {
  {"wicket",  EV_TURNOVER,    "OUT!",  "WICKET"},
  {"six",     EV_SCORE_MAJOR, "SIX!",  "SIX"},
  {"four",    EV_SCORE_MAJOR, "FOUR!", "FOUR"},
  {"run",     EV_SCORE_MINOR, "+1",    "RUN"},
  {"score",   EV_SCORE_MINOR, "+1",    "RUN"},
  {"fifty",   EV_MILESTONE,   "50",    "FIFTY"},
  {"hundred", EV_MILESTONE,   "100",   "CENTURY"},
  {"drs",     EV_REVIEW,      "DRS",   "REVIEW"},
  {"noball",  EV_PENALTY,     "NO-B",  "NO BALL"},
  {"lead",    EV_LEAD_CHANGE, "AHEAD", "AHEAD"},
  {"period",  EV_PERIOD,      "INNS",  "INNINGS"},
  {"final",   EV_FINAL,       "WON",   "RESULT"},
};

/* deterministic per-ball outcome for the demo feed */
static char cricketBall(int ball) {
  uint8_t r = prng8(ball * 13 + 29);
  if (r <   9) return 'W';
  if (r <  95) return '.';
  if (r < 150) return '1';
  if (r < 180) return '2';
  if (r < 225) return '4';
  return '6';
}
static int cricketRuns(char c) { return (c == '.' || c == 'W') ? 0 : (c - '0'); }

/* --- layout: one row per innings, then the progress band, the last-six-balls
       strip, and the ticker. Everything is size 1 — 10 chars is exactly what
       "38.2 RR6.4" needs. --- */
/* One innings row: "IND 91/2", "BAN 426" when all out, "AUS  yet" before
   they bat. Shared by both rows so the two never drift apart. */
static void cricketScoreLine(char *out, size_t n, const Side &sd) {
  /* "%-3.3s" pads AND truncates: the precision is what keeps the line inside
     the card if Side.abbr is ever widened again. Longest possible line is
     "IND 288/9" — nine characters, 54px from x=5, clear of the right bar. */
  if (sd.score2 < 0)        snprintf(out, n, "%-3.3s  yet", sd.abbr);
  else if (sd.score2 >= 10) snprintf(out, n, "%-3.3s %d",   sd.abbr, sd.score);
  else                      snprintf(out, n, "%-3.3s %d/%d", sd.abbr, sd.score, sd.score2);
}

static void cricketBody(const LiveMatch &m, uint32_t f) {
  char line[16];
  bool fresh = (millis() - m.changedAt) < 1500;

  /* innings rows — the side that is batting is bright, the other dimmed */
  /* An all-out innings is written as bare runs in cricket ("426", not
     "426/10") — which is both correct notation and the only way it fits:
     "BAN 426/10" is 10 characters and runs a pixel off the 64px panel. */
  /* Cricket is the one sport that cannot use gfxBodyStd: a score is "91/2",
     not a number, and both innings matter at once. It keeps its own two rows
     but now lands on the shared grid, so the card is the same object with the
     same margins as the other five. */
  /* Cricket's own row set, tighter than the shared grid: two 8-row innings
     lines plus a 7-row ball strip do not fit the spacing the other five get,
     because those spend one row set on abbreviations that cricket has already
     folded into its score lines. Rows: 22, 31, pills 40, then SW_BAND. */
  const int R1 = SW_SCORE - 1, R2 = SW_SCORE + 8;
  cricketScoreLine(line, sizeof(line), m.home);
  gfxText(line, 5, R1, m.home.active ? (fresh ? 0xFFFF : m.home.color)
                                     : gfxDim(m.home.color, 45));
  cricketScoreLine(line, sizeof(line), m.away);
  gfxText(line, 5, R2, m.away.active ? (fresh ? 0xFFFF : m.away.color)
                                     : gfxDim(m.away.color, 45));

  /* a small striker dot on the batting side's bar */
  dma->fillRect(0, (m.home.active ? R1 + 1 : R2 + 1), 3, 3, RGB565(255, 255, 200));

  /* The last six balls sit between the innings and the band. They are 7 rows
     tall, so at SW_ABBR they landed on top of the second innings row — the
     one row of the grid cricket cannot use, because its two score rows have
     already spent the space the other sports give to abbreviations. */
  gfxPills(m.strip, SW_BAND - 9, f);
  gfxBand(m.period, m.detail, SW_BAND, C_BAND);
}
/* Overs bowled out of the innings' allotment, which the provider stores in
   `period` as "18.2/20" for a limited-overs game. A Test has no such number —
   it returns -1 and the comet stays away rather than inventing a run rate. */
static int cricketProgress(const LiveMatch &m) {
  const char *slash = strchr(m.period, '/');
  if (!slash) return -1;
  int done = atoi(m.period), total = atoi(slash + 1);
  return (total > 0) ? done * 100 / total : -1;
}

/* --- bespoke: stumps shatter on a wicket. Three stumps stand in the middle
       of the card, the bails fly off on parabolic arcs and splinters scatter
       before the card comes back. --- */
static bool cricketWicketFx(const MatchEvent &e, uint32_t f, uint8_t dur) {
  dma->fillRect(0, SW_TOP + 1, PANEL_W, SW_H - 1, 0);
  uint16_t wood = RGB565(230, 200, 150);
  int base = 50, top = 30;

  for (int s = 0; s < 3; s++) {                 // stumps, leaning as they go
    int x = 28 + s * 4;
    int lean = (f < 6) ? 0 : (int)(f - 6) * (s - 1) / 4;
    for (int y = top; y <= base; y++)
      dma->drawPixel(x + lean * (base - y) / 20, y, gfxDim(wood, 100 - (int)f));
  }
  for (int b = 0; b < 2; b++) {                 // bails on parabolic arcs
    int t  = (int)f;
    int bx = 30 + b * 4 + (b ? t : -t) * 2;
    int by = top - 2 - (t * 3 - t * t / 6);
    if (by > SW_TOP && by < SW_BOT && bx > 0 && bx < PANEL_W)
      dma->fillRect(bx, by, 2, 1, wood);
  }
  gfxShards(f, dur, RGB565(255, 140, 60));
  if (f > 5) gfxPunch(e.punch, e.label, f - 5, dur - 5, RGB565(255, 90, 60));
  return true;
}

/* --- bespoke: a six leaves the card over the boundary rope; a four skims
       along the ground into it. --- */
static bool cricketBoundaryFx(const MatchEvent &e, uint32_t f, uint8_t dur, bool six) {
  dma->fillRect(0, SW_TOP + 1, PANEL_W, SW_H - 1, 0);
  int ropeY = SW_BOT - 6;
  uint16_t rope = RGB565(70, 90, 140);
  dma->drawFastHLine(0, ropeY, PANEL_W, rope);

  int t = (int)f;
  if (six) {
    int bx = 8 + t * 2;                          // arcs up and out
    int by = ropeY - (t * 4 - t * t / 5);
    for (int i = 0; i < 5; i++) {                // trail
      int tx = bx - i * 2, ty = by + i * i / 3;
      if (tx > 0 && tx < PANEL_W && ty > SW_TOP && ty < SW_BOT)
        dma->drawPixel(tx, ty, gfxDim(RGB565(255, 220, 120), 70 - i * 12));
    }
    if (bx < PANEL_W && by > SW_TOP)
      dma->fillCircle(bx, by, 1, RGB565(255, 240, 180));
    else if (t > 10) drawSpark(PANEL_W - 8, SW_TOP + 6, RGB565(255, 230, 150));
  } else {
    int bx = 6 + t * 3;                          // skims the turf
    for (int i = 0; i < 6; i++) {
      int tx = bx - i * 3;
      if (tx > 0 && tx < PANEL_W)
        dma->drawFastHLine(tx, ropeY - 1, 2, gfxDim(RGB565(120, 220, 255), 80 - i * 12));
    }
    if (bx >= PANEL_W - 4)                        // the rope shivers on impact
      for (int x = PANEL_W - 14; x < PANEL_W; x++)
        dma->drawPixel(x, ropeY + ((x + t) % 3 == 0 ? -1 : 0), RGB565(150, 180, 255));
  }
  if (f > 6) gfxPunch(e.punch, e.label, f - 6, dur - 6, e.color);
  return true;
}

/* --- bespoke: bat raised for a fifty or a century --- */
static bool cricketMilestoneFx(const MatchEvent &e, uint32_t f, uint8_t dur) {
  dma->fillRect(0, SW_TOP + 1, PANEL_W, SW_H - 1, 0);
  int t = (int)f * 100 / (dur ? dur : 1);
  int lift = t < 50 ? t / 6 : 8;
  int bx = 14, by = 48 - lift;
  dma->fillRect(bx, by - 10, 3, 10, RGB565(230, 200, 150));   // bat
  dma->fillRect(bx - 1, by, 5, 4, RGB565(180, 140, 90));      // handle
  dma->fillCircle(bx + 6, by + 2, 3, RGB565(120, 200, 255));  // batter
  dma->fillRect(bx + 4, by + 5, 5, 8, RGB565(230, 230, 240));
  for (int i = 0; i < 10; i++) {                              // laurel sparkles
    int a = (i * 36 + (int)f * 5) % 360;
    drawSpark(40 + (int)(14 * cos(a * PI / 180.0)),
              38 + (int)(9 * sin(a * PI / 180.0)), gfxDim(RGB565(255, 220, 120), 90 - t / 2));
  }
  /* Right-align the number against the panel edge and only use size 2 while
     it still clears the batter sprite (x<=22). "50" is 24px wide and fits
     easily; "100" is 36px and used to be drawn from a hard-coded x=40, which
     pushed its last digit off the panel and rendered a century as "10". */
  uint8_t ps = 2;
  int pw = gfxTextW(e.punch, ps);
  if (PANEL_W - 2 - pw < 26) { ps = 1; pw = gfxTextW(e.punch, ps); }
  gfxText(e.punch, PANEL_W - 2 - pw, ps == 2 ? 32 : 36,
          gfxBlend(e.color, 0xFFFF, gfxPulse(f, 14) / 2), ps);
  return true;
}

static bool cricketEvent(const LiveMatch &m, const MatchEvent &e, uint32_t f) {
  (void)m;
  if (!strcmp(e.native, "wicket"))  return cricketWicketFx(e, f, EV_FRAMES[EV_TURNOVER]);
  if (!strcmp(e.native, "six"))     return cricketBoundaryFx(e, f, EV_FRAMES[EV_SCORE_MAJOR], true);
  if (!strcmp(e.native, "four"))    return cricketBoundaryFx(e, f, EV_FRAMES[EV_SCORE_MAJOR], false);
  if (!strcmp(e.native, "fifty") || !strcmp(e.native, "hundred"))
    return cricketMilestoneFx(e, f, EV_FRAMES[EV_MILESTONE]);
  return false;                                   // everything else: generic
}

/* --- demo feed: a T20, one ball per second --- */
static void cricketSim(LiveMatch &m, uint32_t t) {
  const int PER_INNINGS = 120;
  int ball = (int)(t / 1000);
  bool over = ball >= PER_INNINGS * 2;
  if (over) ball = PER_INNINGS * 2 - 1;

  int inn = ball < PER_INNINGS ? 1 : 2;
  int r1 = 0, w1 = 0, r2 = 0, w2 = 0;
  for (int i = 0; i < PER_INNINGS && i <= ball; i++) {
    char c = cricketBall(i);
    if (c == 'W') w1++; else r1 += cricketRuns(c);
    if (w1 >= 10) break;
  }
  if (inn == 2)
    for (int i = PER_INNINGS; i <= ball; i++) {
      char c = cricketBall(i);
      if (c == 'W') w2++; else r2 += cricketRuns(c);
      if (w2 >= 10) break;
    }

  m.home.score = r1; m.home.score2 = w1;
  m.away.score = r2; m.away.score2 = (inn == 2) ? w2 : SCORE2_NONE;
  m.home.active = (inn == 1);
  m.away.active = (inn == 2);
  m.state = over ? MS_ENDED : (ball == PER_INNINGS ? MS_BREAK : MS_LIVE);

  int inBall = ball % PER_INNINGS;
  snprintf(m.period, sizeof(m.period), "%d.%d", inBall / 6, inBall % 6);
  int runs = (inn == 1) ? r1 : r2;
  int rr10 = inBall ? (runs * 60) / inBall : 0;             // run rate x10
  snprintf(m.detail, sizeof(m.detail), "RR%d.%d", rr10 / 10, rr10 % 10);

  uint16_t prevBall = sdGet16(m, 0);
  if ((uint16_t)ball != prevBall) {
    sdSet16(m, 0, (uint16_t)ball);
    char c = cricketBall(ball);
    m.strip[0] = 0;
    for (int i = (ball > 5 ? ball - 5 : 0); i <= ball; i++) stripPush(m, cricketBall(i));

    bool homeBatting = (inn == 1);
    if (over)                      simReport(m, "final",  r1 > r2);
    else if (ball == PER_INNINGS)  simReport(m, "period", false);
    else if (c == 'W')             simReport(m, "wicket", homeBatting);
    else if (c == '6')             simReport(m, "six",    homeBatting);
    else if (c == '4')             simReport(m, "four",   homeBatting);
    else if (runs == 50 || runs == 100)
      simReport(m, runs == 50 ? "fifty" : "hundred", homeBatting);
    else if (prng8(ball * 3 + 7) < 6) simReport(m, "drs",    !homeBatting);
    else if (prng8(ball * 5 + 2) < 5) simReport(m, "noball", !homeBatting);
    else                           simReport(m, "run",    homeBatting);

    if (over)
      snprintf(m.ticker, sizeof(m.ticker), "%s won  %d/%d v %d/%d",
               r1 > r2 ? m.home.abbr : m.away.abbr, r1, w1, r2, w2);
    else if (inn == 2)
      snprintf(m.ticker, sizeof(m.ticker), "%s need %d in %d balls",
               m.away.abbr, r1 - r2 + 1, PER_INNINGS * 2 - ball);
    else
      snprintf(m.ticker, sizeof(m.ticker), "%s %d/%d after %d.%d ov",
               m.home.abbr, r1, w1, inBall / 6, inBall % 6);
  }
}

/* Runs and wickets both move; a wicket outranks the boundary that cannot
   have happened on the same ball anyway. Overs/run-rate changes alone are
   not events. */
static const char *cricketDelta(int dScore, int dScore2) {
  if (dScore2 > 0) return "wicket";        // score2 = wickets
  if (dScore >= 6) return "six";
  if (dScore == 4) return "four";
  if (dScore >  0) return "run";
  return nullptr;
}

static const SportModule SPORT_CRICKET = {
  "cricket", "Cricket", RGB565(120, 220, 140),
  CRICKET_TEAMS,  (uint8_t)(sizeof(CRICKET_TEAMS)  / sizeof(TeamEntry)),
  CRICKET_EVENTS, (uint8_t)(sizeof(CRICKET_EVENTS) / sizeof(EventMap)),
  cricketBody, cricketEvent, cricketSim,
  "cricket", "24231", cricketDelta,
  cricketProgress, 20000UL   /* a ball every ~40 s; the series board is 6.7 KB */
};
