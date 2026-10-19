/* DigiFrame — live scores: shared drawing toolkit.

   The primitives every sport module composes its layout from. Nothing here
   knows about a specific sport; nothing here decides *what* to draw, only how
   to draw it well inside the 45 rows the widget owns (19..63) while the clock
   keeps rows 0..18.

   Included after scene.h so it can reuse that file's sprite helpers
   (prng8, drawSpark, drawFirework). */
#pragma once

#define SW_TOP 19       // first row the score widget owns
#define SW_BOT 63       // last row
#define SW_H   (SW_BOT - SW_TOP + 1)

/* ---- the layout grid ----
   Every sport body lands on these rows. They used to be hardcoded per file,
   which is how the card ended up welded to the clock (one blank row above the
   divider and one below — invisible at this pitch) while eight rows at the
   bottom held nothing but the edge bars. One origin, six bodies.

     19  blank        the gap. The clock's own descenders end at 17.
     20  SW_RULE      divider hairline, in the sport's accent
     21  blank
     23  SW_SCORE     score pair, size 2 (16 rows)
     39  SW_ABBR      team abbreviations, size 1
     47  SW_UNDER     each team's colour, under its own name
     49  SW_BAND      period | detail, size 1
     58  SW_METER     match-progress comet
     60+ blank        bottom margin                                           */
#define SW_RULE  20
#define SW_SCORE 23
#define SW_ABBR  39
#define SW_UNDER 47
#define SW_BAND  49
#define SW_METER 58

/* ---- palette ----
   Borrowed from the clock (globals.h) rather than invented, so the two halves
   of the panel read as one object. C_BAND replaced a pale yellow that belonged
   to no other element on the display. */
#define C_BAND   C_DATE                          // lavender: secondary text
#define C_TRAIL  dma->color565(110, 55, 85)      // the seconds comet's trail


/* ---- colour helpers (565 in, 565 out) ---- */
static inline uint8_t c5r(uint16_t c) { return (c >> 11) & 0x1F; }
static inline uint8_t c5g(uint16_t c) { return (c >> 5)  & 0x3F; }
static inline uint8_t c5b(uint16_t c) { return  c        & 0x1F; }
static inline uint16_t c5make(int r, int g, int b) {
  if (r < 0) r = 0; if (r > 31) r = 31;
  if (g < 0) g = 0; if (g > 63) g = 63;
  if (b < 0) b = 0; if (b > 31) b = 31;
  return (uint16_t)((r << 11) | (g << 5) | b);
}
/* scale brightness, pct 0..100 */
static uint16_t gfxDim(uint16_t c, int pct) {
  return c5make(c5r(c) * pct / 100, c5g(c) * pct / 100, c5b(c) * pct / 100);
}
/* linear blend a->b, t 0..100 */
static uint16_t gfxBlend(uint16_t a, uint16_t b, int t) {
  return c5make((c5r(a) * (100 - t) + c5r(b) * t) / 100,
                (c5g(a) * (100 - t) + c5g(b) * t) / 100,
                (c5b(a) * (100 - t) + c5b(b) * t) / 100);
}
/* 0..100..0 triangle over a period, for breathing/pulsing */
static int gfxPulse(uint32_t f, uint8_t period) {
  int p = f % period;
  return (p < period / 2) ? p * 200 / period : (period - p) * 200 / period;
}

/* ---- text ---- */
static int gfxTextW(const char *s, uint8_t size) { return (int)strlen(s) * 6 * size; }

static void gfxText(const char *s, int x, int y, uint16_t c, uint8_t size = 1) {
  dma->setTextWrap(false);
  dma->setTextSize(size);
  dma->setTextColor(c);
  dma->setCursor(x, y);
  dma->print(s);
  dma->setTextSize(1);
}
static void gfxTextC(const char *s, int y, uint16_t c, uint8_t size = 1) {
  gfxText(s, (PANEL_W - gfxTextW(s, size)) / 2, y, c, size);
}
static void gfxTextR(const char *s, int right, int y, uint16_t c, uint8_t size = 1) {
  gfxText(s, right - gfxTextW(s, size), y, c, size);
}

/* ---- card chrome ---- */
/* Opaque band + accent divider. The opaque fill matters: drawWeatherBg()
   paints dim weather pixels across all 64 rows before we are called. */
static void gfxCard(uint16_t accent) {
  dma->fillRect(0, SW_TOP, PANEL_W, SW_H, 0);
  dma->drawFastHLine(0, SW_RULE, PANEL_W, gfxDim(accent, 55));
}

/* ---- the match comet ----
   The clock draws a hairline across row 17 that grows with the seconds and
   carries a pulsing head — the most characteristic thing on this display. The
   card gets its twin: same trail colour, same blinking head, tracking the
   match instead of the minute. Two comets, one idiom, and the bottom band
   finally says something.

   It replaced three unrelated gadgets — cricket's blob strip, NFL's field
   line, basketball's bar — of which two were fed by data the provider never
   sends, and three of the six sports had nothing down here at all.

   pct < 0 means "this sport cannot say" and draws nothing rather than lying. */
static void gfxMeter(int pct, uint32_t f) {
  if (pct < 0) return;
  if (pct > 100) pct = 100;
  const int x0 = 4, w = PANEL_W - 8;
  int len = w * pct / 100;
  dma->drawFastHLine(x0, SW_METER, w, gfxDim(C_TRAIL, 35));   // the run still to go
  if (len > 0) dma->drawFastHLine(x0, SW_METER, len, C_TRAIL);
  dma->drawPixel(x0 + len, SW_METER, ((f / 8) % 2) ? C_ACCENT : C_TIME);
}

/* 3px colour bars down both edges; the side that is attacking / batting /
   serving breathes so the panel shows possession without spending a row. */
static void gfxTeamBars(const LiveMatch &m, uint32_t f) {
  int breathe = 55 + gfxPulse(f, 30) * 45 / 100;
  uint16_t lc = m.home.active ? gfxDim(m.home.color, breathe) : gfxDim(m.home.color, 45);
  uint16_t rc = m.away.active ? gfxDim(m.away.color, breathe) : gfxDim(m.away.color, 45);
  dma->fillRect(0, SW_TOP + 2, 3, SW_H - 2, lc);
  dma->fillRect(PANEL_W - 3, SW_TOP + 2, 3, SW_H - 2, rc);
  /* A bright cap answers "which of these is mine?" — so it only earns its
     pixels when the answer is one of them. A league follow and a pinned match
     mark BOTH sides fav to make the match eligible at all, which lit both caps
     and made the marker mean nothing exactly when it was least obvious why the
     card was up. Two favourites is the same as none, here. */
  if (m.home.fav != m.away.fav) {
    if (m.home.fav) dma->drawFastHLine(0, SW_TOP + 2, 3, m.home.color);
    else            dma->drawFastHLine(PANEL_W - 3, SW_TOP + 2, 3, m.away.color);
  }
}

/* The blinking live dot that used to sit at PANEL_W-6, SW_TOP+2 is gone.
   It answered "is this live?" — a question the card barely raises, since it
   only holds the panel while a match is live or just finished — and it cost
   two pixels inside F1's interval column, where the top row's gap and the dot
   drew over each other. Removed for every sport rather than suppressed for
   one: an indicator worth two pixels on six cards was not worth the collision
   on the seventh. */

/* ---- progress helpers for SportModule.progress() ----
   Both read what the live tick already stores, so neither costs a request. */

/* first integer in a string, or -1 ("Q3" -> 3, "22'" -> 22, "8:48" -> 8) */
static int gfxFirstInt(const char *s) {
  if (!s) return -1;
  while (*s && (*s < '0' || *s > '9')) s++;
  return *s ? atoi(s) : -1;
}
/* Clock sports (NFL, NBA, NHL): the clock counts DOWN inside a numbered
   period, so elapsed is the part already burnt off. */
static int gfxProgPeriods(const LiveMatch &m, int periods, int minsEach) {
  int p = gfxFirstInt(m.period);
  if (p < 1) return -1;
  if (p > periods) p = periods;                    // overtime pins the head
  int left = gfxFirstInt(m.detail);                // whole minutes remaining
  if (left < 0 || left > minsEach) left = 0;       // no clock: assume period end
  int done = (p - 1) * minsEach + (minsEach - left);
  return done * 100 / (periods * minsEach);
}
/* Running-clock sports (football, rugby): the minute counts up and is the
   whole answer. */
static int gfxProgMinutes(const LiveMatch &m, int fullTime) {
  int mins = gfxFirstInt(m.detail);
  if (mins < 0) mins = gfxFirstInt(m.period);
  if (mins < 0) return -1;
  return mins * 100 / fullTime;
}

/* ---- score line ----
   The auto-size rule that lets basketball's 112-108 share a renderer with
   football's 2-1: size 2 (12x16, five chars max) when it fits, else size 1
   with the two numbers pushed to either side of a centred dash. */
static void gfxScorePair(int a, int b, uint16_t ca, uint16_t cb, int y, uint32_t f,
                         bool glowA = false, bool glowB = false) {
  char sa[6], sb[6], both[14];
  snprintf(sa, sizeof(sa), "%d", a);
  snprintf(sb, sizeof(sb), "%d", b);
  snprintf(both, sizeof(both), "%s-%s", sa, sb);

  int glow = gfxPulse(f, 20) / 2;                    // settle-glow after a change
  uint16_t ga = glowA ? gfxBlend(ca, 0xFFFF, glow) : ca;
  uint16_t gb = glowB ? gfxBlend(cb, 0xFFFF, glow) : cb;

  if (strlen(both) <= 5) {                            // size 2 fits
    int w = gfxTextW(both, 2);
    int x = (PANEL_W - w) / 2;
    gfxText(sa, x, y, ga, 2);
    gfxText("-", x + gfxTextW(sa, 2), y, gfxDim(0xFFFF, 40), 2);
    gfxText(sb, x + gfxTextW(sa, 2) + 12, y, gb, 2);
  } else {                                            // three digits a side
    gfxText("-", (PANEL_W - 6) / 2, y + 4, gfxDim(0xFFFF, 40), 1);
    gfxTextR(sa, (PANEL_W - 6) / 2 - 2, y + 4, ga, 1);
    gfxText(sb, (PANEL_W + 6) / 2 + 2, y + 4, gb, 1);
  }
}

/* ---- period / detail band ----
   The right field YIELDS when the left one reaches it. The two strings are
   independent and both variable-width, and overlapped 5x7 glyphs read as
   garbage: hockey's "P2 18'" over "SOG 21" rendered as "$8G" — the panel
   showed hybrid glyphs no string in the firmware contained (proven by a
   pixel-exact match of the union of both strings against a capture).
   Dropping the lesser field entirely beats truncating it: a partial stat
   misleads, a missing one is just quiet. */
/* ---- the band, as one centred unit ----
   gfxBand splits its two fields to the outer margins, which is right when they
   are wide and wrong when they are not: "H1" and "22'" ended up either side of
   twenty dead pixels, and the eye read them as two unrelated things. When the
   pair fits with a separator between them, set it as ONE object in the middle
   of the card; when it does not, fall back to the split, which is what the
   yield rule exists for. The separator is a drawn 2px block, not a glyph —
   the built-in font's middle dot lives at CP437 0xFA and only renders when
   cp437(true) has been set, which is not a dependency worth taking. */
static void gfxBand(const char *left, const char *right, int y, uint16_t c);

static void gfxBandC(const char *left, const char *right, int y, uint16_t c) {
  int lw = (left  && *left)  ? gfxTextW(left, 1)  : 0;
  int rw = (right && *right) ? gfxTextW(right, 1) : 0;
  /* Hockey and rugby pack both facts into `period` and leave `detail` empty.
     Centre the lone field rather than dropping to gfxBand's left margin, or
     five cards centre their band and two do not. */
  if (!lw && !rw) return;
  if (!lw || !rw) { gfxTextC(lw ? left : right, y, c); return; }
  int total = lw + 6 + rw;
  if (total > PANEL_W - 6) {
    /* Too wide to pair. Split it the way gfxBand would — unless gfxBand's
       clearance rule would DROP the right field, in which case centre what is
       left rather than leaving one lonely string hugging a margin that no
       longer has an edge bar beside it. Hockey ("P2 18'" + "SOG 21") and rugby
       ("H1 22'" + "TRY") are both this case. */
    int rx = PANEL_W - 5 - rw;
    if (rx >= 5 + lw + 3) gfxBand(left, right, y, c);
    else                  gfxTextC(left, y, c);
    return;
  }
  int x = (PANEL_W - total) / 2;
  gfxText(left, x, y, c);
  dma->fillRect(x + lw + 2, y + 3, 2, 2, gfxDim(c, 55));
  gfxText(right, x + lw + 6, y, gfxDim(c, 70));
}

static void gfxBand(const char *left, const char *right, int y, uint16_t c) {
  int lw = (left && *left) ? gfxTextW(left, 1) : 0;
  if (lw) gfxText(left, 5, y, c);
  if (right && *right) {
    int rx = PANEL_W - 5 - gfxTextW(right, 1);
    /* Three pixels of clearance, not zero. Abutting 5x7 glyphs read as one
       word — "H1 22'" beside "TRY" rendered as "22'TRY", and cricket's overs
       beside its run rate as "7.3RR12.1". The fields cleared the old test by
       exactly one pixel, which is why it looked like a font bug rather than a
       layout one. Dropping the right field still beats truncating it. */
    if (rx >= 5 + lw + 3) gfxTextR(right, PANEL_W - 5, y, gfxDim(c, 70));
  }
}

/* ---- the standard body ----
   Five of the six sports drew exactly this and differed only in a pale band
   tint they each invented — football 220,220,150; basketball 230,200,150;
   hockey 200,225,255; rugby 200,235,200 — four near-identical colours that
   read as one at this pitch. Now one layout, one token, and a sport file
   keeps only what is genuinely its own. */
static void gfxBodyStd(const LiveMatch &m, uint32_t f) {
  bool fresh = (millis() - m.changedAt) < 1500;
  gfxScorePair(m.home.score, m.away.score, m.home.color, m.away.color,
               SW_SCORE, f, fresh, fresh);
  /* x=3, not 5: the 3px edge bars are gone (SportModule.fullWidth), so the
     names get the columns back. */
  gfxText (m.home.abbr, 3,            SW_ABBR, gfxDim(m.home.color, 95));
  gfxTextR(m.away.abbr, PANEL_W - 3,  SW_ABBR, gfxDim(m.away.color, 95));

  /* The team colour now sits UNDER THE NAME IT IDENTIFIES rather than down the
     edge of the card, where it repeated what the score digits and the code
     already said and cost six of sixty-four columns. The favourite's rule is
     2px — the marker the bright bar cap used to carry. Two favourites is the
     same as none, exactly as it was on the bars. */
  int hw = gfxTextW(m.home.abbr, 1) - 1;
  int aw = gfxTextW(m.away.abbr, 1) - 1;
  bool one = (m.home.fav != m.away.fav);
  int hh = (one && m.home.fav) ? 2 : 1;
  int ah = (one && m.away.fav) ? 2 : 1;
  if (hw > 0) dma->fillRect(3, SW_UNDER + 1 - hh, hw, hh, m.home.color);
  if (aw > 0) dma->fillRect(PANEL_W - 3 - aw, SW_UNDER + 1 - ah, aw, ah, m.away.color);

  gfxBandC(m.period, m.detail, SW_BAND, C_BAND);
}


/* ---- ticker: scrolls only when the text is wider than the panel ---- */
static void gfxTicker(const char *s, int y, uint32_t f, uint16_t c) {
  if (!s || !*s) return;
  int w = gfxTextW(s, 1);
  if (w <= PANEL_W - 8) { gfxTextC(s, y, c); return; }
  int span = w + PANEL_W;
  int x = PANEL_W - (int)((f / 2) % (uint32_t)span);
  gfxText(s, x, y, c);
  dma->fillRect(0, y, 3, 8, 0);                       // keep clear of the bars
  dma->fillRect(PANEL_W - 3, y, 3, 8, 0);
}

/* ---- recent-events strip: one pill per character of m.strip ----
   The characters are sport-defined ('.'/'1'/'4'/'6'/'W' for cricket,
   '2'/'3'/'F' for basketball); the colour map is shared. */
static uint16_t gfxPillColor(char ch) {
  switch (ch) {
    case 'W': case 'X': return RGB565(255,  70,  70);   // wicket / turnover
    case '6': case '3': return RGB565(255, 190,  60);   // biggest score
    case '4': case '2': return RGB565( 90, 210, 255);   // boundary / two
    case '.':           return RGB565( 70,  70,  90);   // dot ball
    case 'F':           return RGB565(220, 130, 255);   // free throw / foul
    default:            return RGB565(150, 210, 150);   // ordinary run
  }
}
/* Six filled chips carrying a BLACK glyph is what this used to be, and on a
   64px panel it read as mud: a mid-tone 8x7 block with a dark 5x7 letter
   punched out of it resolves to a smudge, and a dot ball got a full chip of
   its own for saying nothing happened. Inverted now — the ground is a dark
   tint and the glyph carries the colour, so a W is a W from across the room —
   and a dot is a quiet 2px mark rather than a chip. Only what HAPPENED gets
   furniture. */
static void gfxPills(const char *strip, int y, uint32_t f) {
  int n = (int)strlen(strip);
  if (n <= 0) return;
  if (n > 6) { strip += n - 6; n = 6; }
  int pw = 8, gap = 1, total = n * pw + (n - 1) * gap;
  int x0 = (PANEL_W - total) / 2;
  for (int i = 0; i < n; i++) {
    int x = x0 + i * (pw + gap);
    bool newest = (i == n - 1);          // the newest delivery reads brightest
    char ch = strip[i];
    uint16_t c = gfxPillColor(ch);
    if (ch == '.') {
      dma->fillRect(x + 3, y + 3, 2, 2, gfxDim(c, newest ? 130 : 80));
      continue;
    }
    dma->fillRoundRect(x, y, pw, 7, 2, gfxDim(c, newest ? 30 : 18));
    char t[2] = { ch, 0 };
    gfxText(t, x + 2, y, gfxDim(c, newest ? 100 : 72));
  }
  (void)f;
}

/* ---- progress / countdown bar (shot clock, power play, field position) ---- */
static void gfxBar(int x, int y, int w, int h, int pct, uint16_t c) {
  if (pct < 0) pct = 0; if (pct > 100) pct = 100;
  dma->drawRect(x, y, w, h, gfxDim(c, 30));
  int fill = (w - 2) * pct / 100;
  if (fill > 0) dma->fillRect(x + 1, y + 1, fill, h - 2, c);
}

/* ---- odometer digit roll, used whenever a score changes ----
   Draws the outgoing digit sliding up out of an 8px cell and the incoming one
   following it, then blacks out the rows either side to clip the overflow. */
static void gfxOdometer(int oldV, int newV, int x, int y, uint32_t f, uint8_t dur, uint16_t c) {
  char a[6], b[6];
  snprintf(a, sizeof(a), "%d", oldV);
  snprintf(b, sizeof(b), "%d", newV);
  int t = (dur ? (int)f * 100 / dur : 100);
  if (t > 100) t = 100;
  int off = 8 * t / 100;
  gfxText(a, x, y - off, c);
  gfxText(b, x, y - off + 8, c);
  dma->fillRect(x, y - 8, gfxTextW(b, 1) + 2, 8, 0);   // clip above
  dma->fillRect(x, y + 8, gfxTextW(b, 1) + 2, 8, 0);   // clip below
}

/* ---- animation building blocks the sport modules assemble effects from ---- */

/* whole-card colour wash that fades out over `dur` frames */
static void gfxFlash(uint16_t c, uint32_t f, uint8_t dur) {
  if (f >= dur) return;
  int t = 100 - (int)f * 100 / dur;
  dma->fillRect(0, SW_TOP, PANEL_W, SW_H, gfxDim(c, t));
}
/* horizontal wipe; returns the leading edge x so a caller can ride it */
static int gfxWipe(uint16_t c, uint32_t f, uint8_t dur) {
  int x = (int)f * (PANEL_W + 8) / (dur ? dur : 1);
  dma->fillRect(x - 8, SW_TOP + 1, 8, SW_H - 1, gfxDim(c, 70));
  dma->drawFastVLine(x, SW_TOP + 1, SW_H - 1, c);
  return x;
}
/* expanding ring centred anywhere in the card */
static void gfxRing(int cx, int cy, uint32_t f, uint8_t dur, uint16_t c) {
  int r = 2 + (int)f * 34 / (dur ? dur : 1);
  int fade = 100 - (int)f * 100 / (dur ? dur : 1);
  dma->drawCircle(cx, cy, r, gfxDim(c, fade));
  if (r > 3) dma->drawCircle(cx, cy, r - 3, gfxDim(c, fade * 6 / 10));
}
/* pixel shards falling away from a diagonal fracture, then settling back */
static void gfxShards(uint32_t f, uint8_t dur, uint16_t c) {
  for (int i = 0; i < 46; i++) {
    int sx = prng8(i * 7 + 1) % PANEL_W;
    int sy = SW_TOP + 4 + prng8(i * 13 + 5) % (SW_H - 8);
    int vy = 1 + prng8(i * 3 + 2) % 3;
    int t  = (int)f;
    int y  = sy + (t * vy) / 3 - (t * t) / 90;        // fall with a little lift
    int x  = sx + (sx > 32 ? t / 4 : -t / 4);
    if (y < SW_TOP || y > SW_BOT || x < 0 || x >= PANEL_W) continue;
    int fade = 100 - (int)f * 100 / (dur ? dur : 1);
    dma->drawPixel(x, y, gfxDim(c, fade));
  }
}
/* bright line sweeping up and down over a dimmed card */
static void gfxScanline(uint32_t f, uint8_t dur, uint16_t c) {
  dma->fillRect(0, SW_TOP + 1, PANEL_W, SW_H - 1, RGB565(6, 6, 10));
  int span = SW_H - 2, p = (int)(f * 2) % (span * 2);
  int y = SW_TOP + 1 + (p < span ? p : span * 2 - p);
  dma->drawFastHLine(0, y, PANEL_W, c);
  dma->drawFastHLine(0, y - 1, PANEL_W, gfxDim(c, 40));
  dma->drawFastHLine(0, y + 1, PANEL_W, gfxDim(c, 40));
  (void)dur;
}
/* sparkle rain falling through the card */
static void gfxSparkRain(uint32_t f, uint8_t dur, uint16_t c) {
  for (int i = 0; i < 18; i++) {
    int x = prng8(i * 11 + 3) % PANEL_W;
    int y = SW_TOP + ((prng8(i * 5 + 9) + (int)f * 2) % SW_H);
    int fade = 100 - (int)f * 100 / (dur ? dur : 1);
    dma->drawPixel(x, y, gfxDim(c, fade));
    if (i % 4 == 0) drawSpark(x - 1, y - 1, gfxDim(c, fade * 7 / 10));
  }
}
/* The punch word, arriving with a small overshoot and settle.

   Two rules make this legible, and both were learned the hard way:

   1. It CLEARS THE BAND IT OCCUPIES. Drawing the word straight over the
      still-rendered body was the single worst legibility problem on the
      card — "NEW LEAD" landing on top of "22' 43%", "WON" through the
      score digits. The word owns its rows outright; the body shows either
      side of it.
   2. It AUTO-SIZES. Size 2 is 12px per character, so only five fit across
      the panel. A longer word used to run off the right edge and lose its
      tail — cricket's "100" rendered as "10". Anything that does not fit
      drops to size 1 rather than being silently truncated. */
static void gfxPunch(const char *punch, const char *label, uint32_t f, uint8_t dur, uint16_t c) {
  if (!punch || !*punch) return;
  int t = (dur ? (int)f * 100 / dur : 100);

  uint8_t size = (gfxTextW(punch, 2) <= PANEL_W - 2) ? 2 : 1;
  int wh = (size == 2) ? 16 : 8;
  int y  = 30 + (t < 25 ? (25 - t) / 3 : (t < 40 ? (t - 25) / 8 : 0));

  bool showLabel = label && *label && t > 30;
  int top = y - 2;
  int h   = wh + 4 + (showLabel ? 10 : 0);
  if (top < SW_TOP + 1) { h -= (SW_TOP + 1 - top); top = SW_TOP + 1; }
  if (top + h > SW_BOT + 1) h = SW_BOT + 1 - top;
  if (h > 0) dma->fillRect(0, top, PANEL_W, h, 0);

  gfxTextC(punch, y, gfxBlend(c, 0xFFFF, gfxPulse(f, 12) / 3), size);
  if (showLabel) gfxTextC(label, y + wh + 2, gfxDim(0xFFFF, 55));
}
