/* DigiFrame — sport module: Formula 1.

   The odd one out, and worth saying why before the code.

   A race has twenty competitors and LiveMatch has exactly two Sides. Both
   Sides are still used, because everything upstream depends on them — home is
   the followed driver and away is the leader, which keeps sportsEligible's
   two-term OR, the edge bars, the fav marker and the lead-change effect all
   working untouched. But the CARD is a leaderboard, and that lives in
   sportData as an F1Board: position, code, constructor colour, movement and
   interval for the top six plus the followed driver.

   sportData rides the same memcpy from scoreBack to scoreFront that the rest
   of LiveMatch does, so the N-way board needs no second cross-core path.

     home   the followed driver          score = grid POSITION, 1..20
     away   the race leader — or P2 when the followed driver is leading
     period "34/70" during the race, "FP2" / "QUAL" / "FIN" otherwise
     detail the followed driver's gap, kept for the dashboard and Telegram
     strip  the followed driver's constructor code (not drawn on the card)

   The one thing that could NOT be bent to fit is the direction of the score:
   a position improves by falling, and sportsDiff() only derives an event from
   a delta that grew. Hence `scoreDescends` on SportModule — see sports_core.h.

   Nothing on the card is broadcast furniture; everything came from a measured
   endpoint (see espn_api.h). Tyre compound, sector times, DRS and fastest lap
   are not available at any price and are not implied. */
#pragma once

#define F1_ACCENT RGB565(225, 6, 0)

/* ---- the board ------------------------------------------------------
   Laid over LiveMatch.sportData. Read and written with memcpy, never by
   casting a pointer into the array: sportData's offset inside LiveMatch is
   not guaranteed even and an unaligned 16-bit load faults on Xtensa. */
#define F1_TOP  6          // what one 4.4 KB order call returns
#define F1_ROWS 7          // the top six, plus the followed driver below them

struct F1Row {
  uint8_t  pos;            // grid position, 1..20; 0 = unused slot
  int8_t   moved;          // places gained since the last poll (+ = forward)
  uint16_t color;          // constructor colour, already through teamInk()
  char     abbr[4];        // three-letter driver code
  char     gap[6];         // "LEAD", "+0.7", "+12", "+1L", or empty
};

struct F1Board {
  uint16_t simStep;        // demo feed's edge detector
  /* The flag the provider parsed, as a plain character so espn_api.h — which
     is included long before this file — can write it without a shared enum.

       G green   Y yellow   S safety car   V virtual SC   R red   C chequered

     Only GREEN and CHECKER were ever observed on the live endpoint; the rest
     are inferred from ESPN's vocabulary elsewhere. Anything unrecognised must
     arrive as 0, which tints the divider in the sport accent and fires no
     event — never guess a flag the API did not send. */
  uint8_t  flag;
  uint8_t  n;              // rows in use
  uint8_t  sticky;         // index of the followed driver's row, 0xFF if none
  /* Qualifying only: the session leader's time for the segment being run, as
     ESPN formats it, trimmed to one decimal. It cannot live in a row's gap
     column — "1:17.2" is six characters and that column is four — so the
     bottom line carries it beside the segment: "Q2 1:17.2". */
  char     best[8];
  F1Row    row[F1_ROWS];
};

static inline void f1Load(const LiveMatch &m, F1Board &b) {
  memcpy(&b, m.sportData, sizeof(b));
}
static inline void f1Store(LiveMatch &m, const F1Board &b) {
  memcpy(m.sportData, &b, sizeof(b));
}

static const TeamEntry F1_TEAMS[] = {
  {"5752", "Oscar Piastri",     "PIA", RGB565(255, 135,   0)},   // McLaren
  {"5579", "Lando Norris",      "NOR", RGB565(255, 135,   0)},
  {"5498", "Charles Leclerc",   "LEC", RGB565(220,   0,   0)},   // Ferrari
  {"868",  "Lewis Hamilton",    "HAM", RGB565(220,   0,   0)},
  {"5503", "George Russell",    "RUS", RGB565(  0, 210, 190)},   // Mercedes
  {"5829", "Kimi Antonelli",    "ANT", RGB565(  0, 210, 190)},
  {"4665", "Max Verstappen",    "VER", RGB565(  0,  50, 125)},   // Red Bull
  {"5741", "Liam Lawson",       "LAW", RGB565(  0,  50, 125)},
  {"5501", "Pierre Gasly",      "GAS", RGB565(255, 245,   0)},   // Alpine
  {"5823", "Franco Colapinto",  "COL", RGB565(255, 245,   0)},
  {"5835", "Gabriel Bortoleto", "BOR", RGB565(255,  45,   0)},   // Audi
  {"4396", "Nico Hulkenberg",   "HUL", RGB565(255,  45,   0)},
  {"5652", "Yuki Tsunoda",      "TSU", RGB565(102, 146, 255)},   // Racing Bulls
  {"5855", "Arvid Lindblad",    "LIN", RGB565(102, 146, 255)},
  {"4678", "Esteban Ocon",      "OCO", RGB565(150, 150, 150)},   // Haas
  {"5789", "Oliver Bearman",    "BEA", RGB565(150, 150, 150)},
  {"5592", "Alex Albon",        "ALB", RGB565(255, 255, 255)},   // Williams
  {"4686", "Carlos Sainz",      "SAI", RGB565(255, 255, 255)},
  {"348",  "Fernando Alonso",   "ALO", RGB565(  0, 111,  98)},   // Aston Martin
  {"4775", "Lance Stroll",      "STR", RGB565(  0, 111,  98)},
  {"4520", "Valtteri Bottas",   "BOT", RGB565(162, 170, 173)},   // Cadillac
  {"4472", "Sergio Perez",      "PER", RGB565(162, 170, 173)},
};

/* events[0] is the signature animation /test fires, so the overtake leads.

   The stems are SHORT because a native name carries its payload: the provider
   emits "pass:HAM:VER", and MatchEvent.native is sixteen characters, so
   "overtake:HAM:VER" would not have fitted. sportMapEvent() matches on the
   stem; f1Event() reads the codes back off e.native.

   Note evIsMajor() is a fixed set BY KIND, and most users never leave the
   default sportFx = 1 ("major only"). That is why taking the lead is
   EV_SCORE_BIG rather than EV_LEAD_CHANGE, and why a safety car — which
   genuinely reshuffles a race — is EV_TURNOVER rather than EV_PENALTY.
   Anything left on EV_PENALTY here is deliberate chatter. */
static const EventMap F1_EVENTS[] = {
  {"pass",   EV_SCORE_MAJOR, "PASS", "OVERTAKE"},
  {"lead",   EV_SCORE_BIG,   "LEAD", "TAKES LEAD"},
  {"podium", EV_MILESTONE,   "P3",   "PODIUM"},
  {"sc",     EV_TURNOVER,    "SC",   "SAFETY CAR"},
  {"vsc",    EV_TURNOVER,    "VSC",  "VIRTUAL SC"},
  {"red",    EV_TURNOVER,    "RED",  "RED FLAG"},
  /* The session changing is rare — five times a weekend — and it is the one
     thing that tells you WHAT you are looking at, so it earns a major kind. */
  {"ses",    EV_MILESTONE,   "GO",   "SESSION"},
  {"dnf",    EV_TURNOVER,    "DNF",  "RETIRED"},
  /* EV_TURNOVER, not EV_SCORE_MINOR: a stop hands over track position, and
     more to the point evIsMajor() excludes MINOR, so at the default
     sportFx = 1 a pit stop would never have appeared at all. A driver
     pits two or three times a race — this is not the frequent-event case
     that rule exists to suppress. */
  {"pit",    EV_TURNOVER,    "PIT",  "PIT STOP"},
  /* Deliberately left on EV_PENALTY, which evIsMajor() excludes: a local
     yellow fires several times a race and would hijack a clock. The flag is
     already reported continuously by the divider's colour, which is the right
     weight for it; the animation is there for sportFx = 2 ("all events"). */
  {"yellow", EV_PENALTY,     "YEL",  "YELLOW"},
  {"period", EV_PERIOD,      "END",  "SESSION"},
  {"final",  EV_FINAL,       "FLAG", "CHEQUERED"},
};

/* The divider hairline carries the flag — the single most useful colour on a
   race card, and it costs a row that gfxCard() has already painted once. */
static uint16_t f1FlagInk(uint8_t flag, uint32_t f) {
  switch (flag) {
    case 'G': return gfxDim(RGB565(0, 220, 60), 60);
    case 'Y': return gfxDim(RGB565(255, 210, 0), 75);
    case 'V': return gfxDim(RGB565(255, 180, 0), 65);
    case 'S': return gfxBlend(RGB565(255, 180, 0), 0xFFFF, gfxPulse(f, 16) / 2);
    case 'R': return gfxBlend(RGB565(255, 30, 30), 0xFFFF, gfxPulse(f, 10) / 2);
    case 'C': return ((f / 6) % 2) ? 0xFFFF : gfxDim(0xFFFF, 25);
    default:  return gfxDim(F1_ACCENT, 55);       // exactly what gfxCard drew
  }
}

/* ---- the card -------------------------------------------------------
     20  divider, tinted by the flag        full width: no edge bars
     24  |
     32  |  three board slots, paging 1-2-3 then 4-5-6
     40  |
     48  hairline separator
     50  your driver — or the lap, when they are already on the page above
     58  the match comet

   Four text rows is a ceiling, not a preference: the built-in font needs 8px
   of pitch, row 21 belongs to the chrome and 61+ to the bottom margin. Three
   paged slots plus one pinned row is the most drivers that fits while the
   pinned row stays where the eye expects it.

   Rows start at 23, not 22: two blank rows under the divider give the board
   somewhere to breathe, and the top row's interval used to collide with the
   blinking live dot that sat at x=58 in rows 21-22 (since removed for every
   sport — see score_gfx.h).

   Columns. Measured off a capture, the block sat 7px in from the left and 3px
   from the right, because right-aligning the position put a single digit at
   x=7 while a two-digit one started at x=1. Left-aligning it makes the left
   margin CONSTANT and equal to the right one; the driver-code column is the
   alignment the eye actually follows, and that is fixed either way.

     x   3..13   position, left-aligned      3px margin
     x  17..33   driver code                 3px gutter
     x  38..60   interval, right-aligned 62  3px margin, 4px gutter */
#define F1_SLOTS   3
#define F1_PAGE_F  (RENDER_FPS * 4)          // ~4 s a page, on the frame clock

/* Three columns, each with its own job: movement on the left, identity in the
   middle, interval on the right. The position digits are COLOURED by movement
   rather than given a glyph of their own — there is no room for an arrow, and
   colour reads faster than a 3px triangle at this pitch. */
static void f1DrawRow(const F1Row &r, int y, bool sticky) {
  if (!r.pos) return;
  char p[4];
  snprintf(p, sizeof(p), "%d", r.pos);
  /* The position was the DIMMEST thing on the card at 38% while the interval
     beside it sat at 62% — upside down on a leaderboard, where the number you
     scan for is the position. Measured off a capture, not guessed. */
  uint16_t mc = (r.moved > 0) ? RGB565(60, 220, 90)
              : (r.moved < 0) ? RGB565(235, 70, 55)
                              : gfxDim(0xFFFF, 66);
  gfxText(p, 3, y, mc);
  gfxText(r.abbr, 17, y, sticky ? r.color : gfxDim(r.color, 95));
  /* The interval column is four characters wide and the codes end at x=33, so
     four clear pixels separate them — see espnRacingGap() for why a gap is
     never longer than that. */
  /* "LEAD" and "POLE" are states, not measurements: they get less weight than
     a real interval so the numbers down the column stay comparable. */
  if (r.gap[0]) {
    bool measured = (r.gap[0] == '+');
    gfxTextR(r.gap, 62, y, sticky ? C_BAND
                                  : gfxDim(C_BAND, measured ? 62 : 42));
  }
}

static void f1Body(const LiveMatch &m, uint32_t f) {
  F1Board b;
  f1Load(m, b);

  /* Row 20 rather than 21: the body's remit is 21..54, but the flag belongs
     on the divider and nothing else wants that row. Repainted here because
     gfxCard() runs before drawBody and only knows the static sport accent. */
  dma->drawFastHLine(0, SW_RULE, PANEL_W, f1FlagInk(b.flag, f));

  if (!b.n || b.n > F1_ROWS) {          // no board yet: say so rather than lie
    gfxTextC("GRID", 31, gfxDim(0xFFFF, 45));
    gfxTextC(m.period, 43, gfxDim(0xFFFF, 30));
    return;
  }

  /* The paged list is the running order, INCLUDING the followed driver. An
     earlier version left them out so the pinned row below was never a repeat,
     and the page then read "1 HAM / 2 NOR / 4 RUS" — a missing third place
     looks like a bug in the data, which is worse than showing a driver twice.
     The pinned row is visually separate enough to read as "and yours". */
  /* Exactly the top six, two pages of three. Row six is the last slot the one
     4.4 KB order call returns; anything beyond it exists only as the pinned
     row, and paging over it produced a ragged third page holding one driver. */
  uint8_t nIdx = 0;
  for (uint8_t i = 0; i < b.n && i < F1_TOP; i++) if (b.row[i].pos) nIdx++;

  uint8_t pages = nIdx ? (uint8_t)((nIdx + F1_SLOTS - 1) / F1_SLOTS) : 1;
  uint8_t page  = (uint8_t)((f / F1_PAGE_F) % pages);
  uint8_t first = (uint8_t)(page * F1_SLOTS);
  for (uint8_t s = 0; s < F1_SLOTS; s++) {
    uint8_t k = (uint8_t)(first + s);
    if (k >= nIdx) break;
    f1DrawRow(b.row[k], 24 + s * 8, false);
  }

  /* Row 48, not 46. The third slot's glyphs end on row 46, so a rule on 46
     had ZERO clearance and read as an underline of that row rather than a
     divider above your driver — the same abutting-fields fault gfxBand keeps
     3px for, in the vertical. One clear row either side now. */
  for (int x = 0; x < PANEL_W; x += 3)
    dma->drawPixel(x, 48, gfxDim(C_BAND, 30));

  /* The bottom row carries whatever you most need that is NOT already on
     screen. Your driver, normally — but when this page already shows them,
     repeating the identical line reads as a rendering fault, so the row
     spends itself on the lap instead. That is also where the lap counter
     went when the board took the band's rows. */
  bool onPage = (b.sticky < b.n && b.sticky >= first &&
                 b.sticky < (uint8_t)(first + F1_SLOTS));
  if (b.sticky < b.n && !onPage) {
    f1DrawRow(b.row[b.sticky], 50, true);
  } else if (strchr(m.period, '/')) {
    char line[16];
    snprintf(line, sizeof(line), "LAP %s", m.period);
    gfxTextC(line, 50, gfxDim(C_BAND, 70));
  } else if (b.best[0]) {
    /* Qualifying spent this whole band on two centred characters while the
       segment's benchmark lap — already fetched for the interval column — went
       unshown. Segment left, benchmark right, on the board's own columns. */
    gfxText(m.period, 3, 50, gfxDim(0xFFFF, 74));
    gfxTextR(b.best, 62, 50, C_BAND);
  } else {
    gfxTextC(m.period, 50, gfxDim(C_BAND, 70));
  }
}

/* Laps done over laps scheduled. Practice and qualifying have no lap total,
   so period holds "FP2" instead and this returns "cannot say" — the comet
   then draws nothing rather than inventing a race distance, exactly as a Test
   match does in sport_cricket.h. */
static int f1Progress(const LiveMatch &m) {
  const char *slash = strchr(m.period, '/');
  if (!slash) return -1;
  int done = atoi(m.period), total = atoi(slash + 1);
  return (total > 0) ? done * 100 / total : -1;
}

/* Deltas reach here already normalised to "places gained" by sportsDiff's
   `dir`, so a positive number always means the driver moved forward. This is
   only the FALLBACK: when the provider can name the pass it reports
   "pass:HAM:VER" instead and this never runs. A place LOST never arrives —
   no sport animates a score going backwards. */
static const char *f1Delta(int dScore, int dScore2) {
  (void)dScore2;
  return (dScore >= 1) ? "pass" : nullptr;
}

/* Look a driver's constructor colour up on the board, so an animation can
   paint the two cars in their real liveries rather than guessing. */
static uint16_t f1InkOf(const LiveMatch &m, const char *abbr, uint16_t dflt) {
  if (!abbr || !*abbr) return dflt;
  F1Board b;
  f1Load(m, b);
  if (b.n > F1_ROWS) return dflt;
  for (uint8_t i = 0; i < b.n; i++)
    if (!strcmp(b.row[i].abbr, abbr)) return b.row[i].color;
  return dflt;
}

/* --- bespoke: the overtake, with both drivers named. --- */
static bool f1Overtake(const LiveMatch &m, const MatchEvent &e, uint32_t f) {
  const uint8_t dur = EV_FRAMES[EV_SCORE_MAJOR];
  char who[6], whom[6];
  nativeField(e.native, 0, who,  sizeof(who));
  nativeField(e.native, 1, whom, sizeof(whom));

  uint16_t ci = f1InkOf(m, who,  e.color);
  uint16_t cv = f1InkOf(m, whom, m.away.color);
  fxStage(ci, f, 8);

  /* Row 22, not 36. gfxPunch OWNS and clears rows 28..58 — nearly the whole
     card — so a motif drawn where the word goes is simply not there; that is
     the same rule that stops an event compositing over the body. Above the
     word is the only free strip, and the two then read as one picture: the
     pass at the top, who did it underneath. */
  const int road = 22;
  for (int x = 0; x < PANEL_W; x += 4)              // the track, dashed
    dma->drawFastHLine(x, road + 5, 2, gfxDim(0xFFFF, 18));

  const uint8_t MOVE = 20;
  int t  = (int)f * 100 / MOVE; if (t > 100) t = 100;
  int rx = 6 + t * 30 / 100;                        // the car being passed
  int mx = 2 + t * 52 / 100;                        // the one doing the passing
  dma->fillRect(rx, road, 7, 4, gfxDim(cv, 55));
  dma->fillRect(mx, road, 7, 4, ci);
  dma->drawFastHLine(mx - 4, road + 1, 4, gfxDim(ci, 50));        // slipstream
  dma->drawFastHLine(mx - 6, road + 2, 6, gfxDim(ci, 25));

  if (f > 5) {
    char label[12];
    if (who[0] && whom[0]) snprintf(label, sizeof(label), "PASSES %s", whom);
    else                   strlcpy(label, e.label, sizeof(label));
    gfxPunch(who[0] ? who : e.punch, label, f - 5, dur - 5, ci);
  }
  return true;
}

/* --- bespoke: the chequered flag, waved across the card. --- */
static bool f1Chequered(const LiveMatch &m, const MatchEvent &e, uint32_t f) {
  const uint8_t dur = EV_FRAMES[EV_FINAL];
  char winner[6];
  nativeField(e.native, 0, winner, sizeof(winner));
  uint16_t c = f1InkOf(m, winner, e.color);

  fxStage(c, f, 6);
  int edge = (int)f * (PANEL_W + 10) / (dur * 2 / 3);
  for (int y = SW_TOP + 1; y <= SW_BOT; y += 4)
    for (int x = 0; x < PANEL_W; x += 4) {
      if (x > edge) continue;
      int wave = ((y - SW_TOP) / 4 + (int)f / 3) % 2;   // the ripple
      bool white = (((x / 4) + (y / 4) + wave) % 2) == 0;
      dma->fillRect(x, y, 4, 4, white ? 0xFFFF : RGB565(10, 10, 10));
    }
  if (f > dur / 3)
    gfxPunch(winner[0] ? winner : e.punch, winner[0] ? "WINS" : e.label,
             f - dur / 3, dur, c);
  return true;
}

/* --- bespoke: a marshal's flag, waving.
       `segments` splits the wave into dashes, which is the whole visual
       difference between a full safety car and a virtual one: the VSC is the
       same amber, delivered in pieces. --- */
static bool f1FlagWave(const MatchEvent &e, uint32_t f, uint16_t c,
                       bool segments = false) {
  const uint8_t dur = EV_FRAMES[e.kind];
  dma->fillRect(0, SW_TOP + 1, PANEL_W, SW_H - 1, 0);
  for (int y = SW_TOP + 1; y <= SW_BOT; y++) {
    int wave = gfxPulse((uint32_t)(f * 2 + (y - SW_TOP) * 3), 24);
    uint16_t ink = gfxDim(c, 12 + wave * 25 / 100);
    if (!segments) {
      dma->drawFastHLine(0, y, PANEL_W, ink);
    } else {
      for (int x = ((y + (int)f) % 10); x < PANEL_W; x += 10)
        dma->drawFastHLine(x, y, 6, ink);
    }
  }
  gfxPunch(e.punch, e.label, f, dur, c);
  return true;
}

/* --- bespoke: the safety car itself, lights running along its roof bar,
       crossing the strip above the word. --- */
static bool f1SafetyCar(const MatchEvent &e, uint32_t f) {
  const uint8_t dur = EV_FRAMES[EV_TURNOVER];
  const uint16_t amber = RGB565(255, 180, 0);
  fxStage(amber, f, 8);

  const int road = 22;
  for (int x = 0; x < PANEL_W; x += 4)
    dma->drawFastHLine(x, road + 6, 2, gfxDim(0xFFFF, 18));

  int t  = (int)f * 100 / (dur / 2 ? dur / 2 : 1); if (t > 100) t = 100;
  int cx = -14 + t * (PANEL_W + 14) / 100;
  dma->fillRect(cx, road + 1, 12, 4, gfxDim(0xFFFF, 70));      // body
  dma->fillRect(cx + 3, road - 1, 6, 2, gfxDim(0xFFFF, 45));   // cabin
  /* the light bar: two lamps alternating, which is what makes it read as a
     safety car rather than just another car */
  dma->drawPixel(cx + 3, road - 2, ((f / 3) % 2) ? amber : gfxDim(amber, 20));
  dma->drawPixel(cx + 8, road - 2, ((f / 3) % 2) ? gfxDim(amber, 20) : amber);

  if (f > 6) gfxPunch(e.punch, e.label, f - 6, dur - 6, amber);
  return true;
}

/* --- bespoke: the pit box. The car drops in, the crew's three wheels come
       off, and it leaves — the shape of a stop, not a caption about one. --- */
static bool f1PitStop(const LiveMatch &m, const MatchEvent &e, uint32_t f) {
  char who[6];
  nativeField(e.native, 0, who, sizeof(who));
  const uint8_t dur = EV_FRAMES[e.kind];
  uint16_t c = f1InkOf(m, who, e.color);
  fxStage(c, f, 8);

  const int box = 22;
  dma->drawFastHLine(20, box - 2, 24, gfxDim(0xFFFF, 35));     // the box
  dma->drawFastVLine(20, box - 2, 9, gfxDim(0xFFFF, 35));
  dma->drawFastVLine(43, box - 2, 9, gfxDim(0xFFFF, 35));

  int t = (int)f * 100 / dur;
  int cx = (t < 30) ? (-12 + t * 38 / 30)          // in
       : (t < 70) ? 26                              // stopped
       : (26 + (t - 70) * 50 / 30);                 // away
  dma->fillRect(cx, box + 1, 11, 4, c);
  if (t >= 30 && t < 70) {                          // wheels off, jack up
    int lift = ((f / 2) % 2) ? 1 : 0;
    dma->fillRect(cx, box + 1 - lift, 11, 4, c);
    for (int i = 0; i < 3; i++)
      dma->drawPixel(22 + i * 3, box + 7, gfxDim(0xFFFF, 60));
  }

  if (f > 8) {
    char label[12];
    if (who[0]) snprintf(label, sizeof(label), "%s PITS", who);
    else        strlcpy(label, e.label, sizeof(label));
    gfxPunch(e.punch, label, f - 8, dur - 8, c);
  }
  return true;
}

/* --- bespoke: a retirement. The car stops and smoke rises off it. --- */
static bool f1Retire(const LiveMatch &m, const MatchEvent &e, uint32_t f) {
  char who[6];
  nativeField(e.native, 0, who, sizeof(who));
  const uint8_t dur = EV_FRAMES[e.kind];
  uint16_t c = f1InkOf(m, who, e.color);
  fxStage(RGB565(120, 120, 130), f, 8);

  const int road = 23;
  for (int x = 0; x < PANEL_W; x += 4)
    dma->drawFastHLine(x, road + 5, 2, gfxDim(0xFFFF, 18));
  dma->fillRect(26, road, 11, 4, gfxDim(c, 60));               // stopped car
  for (int i = 0; i < 7; i++) {                                 // smoke
    int age = ((int)f * 2 + i * 5) % 30;
    int sx  = 30 + (i % 3) - 1 + age / 8;
    int sy  = road - 1 - age / 3;
    if (sy < SW_TOP + 1) continue;
    dma->drawPixel(sx, sy, gfxDim(RGB565(190, 190, 200), 70 - age * 2));
  }

  if (f > 8) {
    char label[12];
    if (who[0]) snprintf(label, sizeof(label), "%s OUT", who);
    else        strlcpy(label, e.label, sizeof(label));
    gfxPunch(e.punch, label, f - 8, dur - 8, c);
  }
  return true;
}

/* --- bespoke: a session starting. Five red lights on, then out — the one
       piece of F1 iconography everybody reads instantly. The label says
       which session, because that is the thing the card cannot otherwise
       show while the board is paging. --- */
static bool f1SessionStart(const MatchEvent &e, uint32_t f) {
  const uint8_t dur = EV_FRAMES[EV_MILESTONE];
  const uint16_t red = RGB565(255, 40, 30);
  char label[10];
  nativeField(e.native, 0, label, sizeof(label));
  fxStage(RGB565(40, 120, 60), f, 10);

  int lit = (int)f / 4;                       // one lamp every 4 frames
  bool out = (f >= 24);                       // then all out together
  for (int i = 0; i < 5; i++) {
    int x = 9 + i * 10, y = 23;
    uint16_t c = out ? gfxDim(RGB565(30, 30, 30), 60)
                     : (i < lit ? red : gfxDim(red, 12));
    dma->fillRect(x, y, 7, 5, c);
  }
  if (out) {
    int glow = gfxPulse(f - 24, 16);
    dma->drawFastHLine(4, 30, PANEL_W - 8, gfxDim(RGB565(60, 220, 90), 20 + glow / 3));
  }
  if (f > 22)
    gfxPunch(label[0] ? label : e.punch, label[0] ? "STARTS" : e.label,
             f - 22, dur - 22, out ? RGB565(60, 220, 90) : red);
  return true;
}

static bool f1Event(const LiveMatch &m, const MatchEvent &e, uint32_t f) {
  /* strncmp is a PREFIX test compared at each stem's own length, so a short
     stem that prefixes a longer one would shadow it. None here do — but keep
     new stems distinct at their first two characters, or order them longest
     first, rather than relying on it. */
  if (!strncmp(e.native, "pass",   4)) return f1Overtake(m, e, f);
  if (!strncmp(e.native, "final",  5)) return f1Chequered(m, e, f);
  if (!strncmp(e.native, "ses",    3)) return f1SessionStart(e, f);
  if (!strncmp(e.native, "sc",     2)) return f1SafetyCar(e, f);
  if (!strncmp(e.native, "vsc",    3)) return f1FlagWave(e, f, RGB565(255, 180, 0), true);
  if (!strncmp(e.native, "red",    3)) return f1FlagWave(e, f, RGB565(255, 40, 40));
  if (!strncmp(e.native, "yellow", 6)) return f1FlagWave(e, f, RGB565(255, 210, 0));
  if (!strncmp(e.native, "pit",    3)) return f1PitStop(m, e, f);
  if (!strncmp(e.native, "dnf",    3)) return f1Retire(m, e, f);
  return false;                       // everything else: score_fx.h
}

/* --- demo feed: a 70-lap race replayed from t, one lap every 1.2 s ---
   It must produce the same SHAPE the provider does — a full board, "34/70" in
   period, four-character gaps, the flag character, payload-carrying event
   names — or Preview demonstrates a card that cannot occur. */
static const char *const F1_SIM_CODE[F1_ROWS] =
  {"VER", "NOR", "LEC", "HAM", "RUS", "ANT", "PIA"};
static const uint16_t F1_SIM_INK[F1_ROWS] = {
  RGB565(0, 50, 125), RGB565(255, 135, 0), RGB565(220, 0, 0), RGB565(220, 0, 0),
  RGB565(0, 210, 190), RGB565(0, 210, 190), RGB565(255, 135, 0)
};

static void f1Sim(LiveMatch &m, uint32_t t) {
  const int LAPS = 70;
  int lap = (int)(t / 1200);
  bool over = lap >= LAPS;
  if (over) lap = LAPS;

  /* A deterministic shuffle of the order, replayed from scratch each call so
     the sim never depends on when it was last polled. `prev` is the order one
     lap ago, which is what feeds the movement column and the named pass. */
  uint8_t ord[F1_ROWS], prev[F1_ROWS];
  for (uint8_t i = 0; i < F1_ROWS; i++) ord[i] = i;
  memcpy(prev, ord, sizeof(ord));
  for (int i = 1; i <= lap; i++) {
    memcpy(prev, ord, sizeof(ord));
    uint8_t r = prng8(i * 17 + 3);
    if (r > 200) {                                   // one adjacent swap
      uint8_t k = r % (F1_ROWS - 1);
      uint8_t tmp = ord[k]; ord[k] = ord[k + 1]; ord[k + 1] = tmp;
    }
  }

  F1Board b;
  f1Load(m, b);                       // keep simStep across calls
  memset(b.row, 0, sizeof(b.row));
  b.n = F1_TOP;
  b.sticky = 0xFF;
  b.best[0] = 0;              // the demo is a race; no qualifying benchmark
  uint8_t fr = prng8(lap * 5 + 11);
  b.flag = over ? 'C'
         : (fr < 10) ? 'S' : (fr < 18) ? 'V' : (fr < 30) ? 'Y' : 'G';

  for (uint8_t i = 0; i < F1_TOP; i++) {
    uint8_t d = ord[i], was = i;
    for (uint8_t k = 0; k < F1_ROWS; k++) if (prev[k] == d) { was = k; break; }
    F1Row &r = b.row[i];
    r.pos   = (uint8_t)(i + 1);
    r.moved = (int8_t)((int)was - (int)i);
    r.color = teamInk(F1_SIM_INK[d]);
    strlcpy(r.abbr, F1_SIM_CODE[d], sizeof(r.abbr));
    if (i == 0)     strlcpy(r.gap, "LEAD", sizeof(r.gap));
    else if (i < 4) snprintf(r.gap, sizeof(r.gap), "+%d.%d", i, (fr + i) % 10);
    else            snprintf(r.gap, sizeof(r.gap), "+%d", 10 + i * 3 + fr % 5);
    if (!strcmp(r.abbr, m.home.abbr)) b.sticky = i;
  }
  /* The followed driver is on the card even when nowhere near the front —
     that is the whole point of the pinned row. */
  if (b.sticky == 0xFF) {
    F1Row &r = b.row[F1_ROWS - 1];
    b.n     = F1_ROWS;
    r.pos   = 11;
    r.moved = 0;
    r.color = m.home.color;
    strlcpy(r.abbr, m.home.abbr, sizeof(r.abbr));
    snprintf(r.gap, sizeof(r.gap), "+%d", 28 + fr % 7);
    b.sticky = F1_ROWS - 1;
  }

  m.home.score  = (int16_t)b.row[b.sticky].pos;
  m.away.score  = (int16_t)((m.home.score == 1) ? 2 : 1);
  m.home.score2 = m.away.score2 = SCORE2_NONE;
  m.home.color  = b.row[b.sticky].color;
  m.away.color  = b.row[0].color;
  strlcpy(m.away.abbr, b.row[0].abbr, sizeof(m.away.abbr));
  m.home.active = (m.home.score == 1);
  m.away.active = !m.home.active;
  m.state = over ? MS_ENDED : MS_LIVE;

  if (over) strlcpy(m.period, "FIN", sizeof(m.period));
  else      snprintf(m.period, sizeof(m.period), "%d/%d", lap, LAPS);
  strlcpy(m.detail, b.row[b.sticky].gap, sizeof(m.detail));

  if ((uint16_t)lap != b.simStep) {
    b.simStep = (uint16_t)lap;
    char ev[16];
    if (lap <= 1) {
      /* The provider announces the session once, and so does the sim — the
         demo is always a race, so it always says RACE. */
      simReport(m, "ses:RACE", true);
    } else if (over) {
      snprintf(ev, sizeof(ev), "final:%s", b.row[0].abbr);
      simReport(m, ev, m.home.score == 1);
    } else if (b.flag == 'S') {
      simReport(m, "sc", true);
    } else if (b.flag == 'V') {
      simReport(m, "vsc", true);
    } else if (b.flag == 'Y') {
      simReport(m, "yellow", true);
    } else if (lap == 55) {
      snprintf(ev, sizeof(ev), "dnf:%s", b.row[F1_TOP - 1].abbr);
      simReport(m, ev, false);
    } else if (lap == 18 || lap == 42) {
      snprintf(ev, sizeof(ev), "pit:%s", b.row[b.sticky].abbr);
      strlcpy(b.row[b.sticky].gap, "PIT", sizeof(b.row[b.sticky].gap));
      simReport(m, ev, true);
    } else {
      for (uint8_t i = 0; i < F1_TOP; i++)
        if (b.row[i].moved > 0) {
          const char *victim = (i + 1 < F1_TOP) ? b.row[i + 1].abbr : "";
          snprintf(ev, sizeof(ev), "%s:%s:%s",
                   (i == 0) ? "lead" : "pass", b.row[i].abbr, victim);
          simReport(m, ev, i == b.sticky);
          break;
        }
    }
    snprintf(m.ticker, sizeof(m.ticker), "%s P%d  LAP %d/%d",
             b.row[b.sticky].abbr, m.home.score, lap, LAPS);
  }
  f1Store(m, b);
}

static const SportModule SPORT_F1 = {
  "f1", "Formula 1", F1_ACCENT,
  F1_TEAMS,  (uint8_t)(sizeof(F1_TEAMS)  / sizeof(TeamEntry)),
  F1_EVENTS, (uint8_t)(sizeof(F1_EVENTS) / sizeof(EventMap)),
  f1Body, f1Event, f1Sim,
  /* espnSport is "racing", not "f1": the provider branch keys on the sport
     slug, so a NASCAR or IndyCar module later costs no change in espn_api.h.
     espnLeague is the starting point only — FavTeam.league overrides it. */
  "racing", "f1", f1Delta,
  f1Progress,
  20000UL,      /* gaps move constantly, positions on a ~90 s lap scale */
  true,         /* scoreDescends: P8 -> P5 is progress */
  true          /* fullWidth: no edge bars — twenty cars, not two sides */
};

static_assert(sizeof(F1Board) <= 112, "F1Board must fit LiveMatch.sportData");
