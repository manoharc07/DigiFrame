/* DigiFrame — live scores: the generic animation set.

   One effect per EventKind. Every sport gets these for free; a sport module
   overrides only the events where a bespoke visual is worth the code (see the
   drawEvent hook in each sport_*.h). All of them own rows 19..63 and are
   tinted by the colour of the side the event belongs to. `f` is frames since
   the event started, at the render loop's ~15 fps. */
#pragma once

/* Every takeover effect opens with this.

   It CLEARS the card to a dark tint of the event colour rather than washing
   over it. Two problems went away with that one change:

   - gfxFlash() used to paint the whole card at full brightness for several
     frames. On an emissive panel across a room that reads as a strobe, and
     it hid the score at exactly the moment you looked up at it.
   - Everything drawn afterwards — punch word, label, sport motif — now
     lands on empty pixels instead of compositing over the still-rendered
     body, which is what made "NEW LEAD" land on top of "22' 43%" and "WON"
     print straight through the score digits.

   The stage opens at roughly a quarter brightness and settles almost to
   black, so the event still announces itself with a colour swell without
   ever becoming a flash. */
static void fxStage(uint16_t c, uint32_t f, uint8_t rampDur) {
  int t = (f < rampDur) ? (100 - (int)f * 100 / (rampDur ? rampDur : 1)) : 0;
  dma->fillRect(0, SW_TOP + 1, PANEL_W, SW_H - 1, gfxDim(c, 4 + t * 22 / 100));
}

/* --- EV_SCORE_MINOR: no takeover. The card stays readable; only the scoring
       side's bar pulses twice. Frequent events (free throws, singles) must not
       hijack the panel or the alerts stop meaning anything. --- */
static void fxScoreMinor(const MatchEvent &e, uint32_t f, uint8_t dur) {
  int x = e.homeSide ? 0 : PANEL_W - 3;
  int t = gfxPulse(f * 2, 12);
  dma->fillRect(x, SW_TOP + 2, 3, SW_H - 2, gfxBlend(e.color, 0xFFFF, t));
  (void)dur;
}

/* --- EV_SCORE_MAJOR: burst. Colour wash fading out, a firework from the
       scoring side, then the punch word. --- */
static void fxScoreMajor(const MatchEvent &e, uint32_t f, uint8_t dur) {
  fxStage(e.color, f, 8);
  int cx = e.homeSide ? 16 : PANEL_W - 16;
  drawFirework(cx, 38, f * 2, e.color, gfxBlend(e.color, 0xFFFF, 60));
  if (f > 4) gfxPunch(e.punch, e.label, f - 4, dur - 4, e.color);
}

/* --- EV_SCORE_BIG: shockwave. Ring out of the scoring side's bar with
       sparkle rain falling through it. --- */
static void fxScoreBig(const MatchEvent &e, uint32_t f, uint8_t dur) {
  fxStage(e.color, f, 8);
  int cx = e.homeSide ? 2 : PANEL_W - 2;
  gfxRing(cx, 40, f, dur * 2 / 3, e.color);
  gfxRing(cx, 40, f > 6 ? f - 6 : 0, dur * 2 / 3, gfxDim(e.color, 60));
  gfxSparkRain(f, dur, gfxBlend(e.color, 0xFFFF, 40));
  gfxPunch(e.punch, e.label, f, dur, e.color);
}

/* --- EV_TURNOVER: the card fractures and reassembles. Amber-red rather than
       the team colour, tinted toward whoever benefits. --- */
static void fxTurnover(const MatchEvent &e, uint32_t f, uint8_t dur) {
  uint16_t c = gfxBlend(RGB565(255, 120, 40), e.color, 25);
  fxStage(c, f, 6);
  gfxShards(f, dur, c);
  if (f > 6) gfxPunch(e.punch, e.label, f - 6, dur - 6, c);
}

/* --- EV_MILESTONE: rising numerals. The number lifts out of the score line,
       grows, and arcs of sparks sweep beneath it. --- */
static void fxMilestone(const MatchEvent &e, uint32_t f, uint8_t dur) {
  fxStage(e.color, f, 8);
  int t = (int)f * 100 / (dur ? dur : 1);
  int y = 44 - t * 16 / 100;
  for (int i = 0; i < 14; i++) {
    int a  = (i * 26 + (int)f * 6) % 360;
    int rx = 32 + (int)((22 * cos(a * PI / 180.0)));
    int ry = y + 14 + (int)((7 * sin(a * PI / 180.0)));
    if (ry >= SW_TOP && ry <= SW_BOT)
      dma->drawPixel(rx, ry, gfxDim(e.color, 100 - t / 2));
  }
  gfxTextC(e.punch, y, gfxBlend(e.color, 0xFFFF, gfxPulse(f, 14) / 2), t > 20 ? 2 : 1);
  if (t > 40) gfxTextC(e.label, y + 20, gfxDim(0xFFFF, 55));
}

/* --- EV_PENALTY: a card rotates in on its vertical axis and lands askew. --- */
static void fxPenalty(const MatchEvent &e, uint32_t f, uint8_t dur) {
  fxStage(RGB565(255, 190, 40), f, 6);
  bool red = (strstr(e.native, "red") != nullptr);
  uint16_t c = red ? RGB565(255, 50, 50) : RGB565(255, 210, 40);
  int t = (int)f * 100 / (dur ? dur : 1);
  int w = (t < 55) ? 1 + (int)(15 * fabs(sin(t * PI / 55.0))) : 15;
  int skew = (t < 70) ? 0 : 1;
  dma->fillRect(32 - w / 2, 26 + skew, w, 20, c);
  dma->drawRect(32 - w / 2, 26 + skew, w, 20, gfxDim(c, 45));
  if (t > 55) gfxTextC(e.label, 50, gfxDim(0xFFFF, 60));
}

/* --- EV_REVIEW: the card dims and a line scans it, then resolves to a
       green tick or a red cross. --- */
static void fxReview(const MatchEvent &e, uint32_t f, uint8_t dur) {
  int t = (int)f * 100 / (dur ? dur : 1);
  if (t < 65) {
    gfxScanline(f, dur, RGB565(120, 200, 255));
    gfxTextC("REVIEW", 36, gfxDim(0xFFFF, 70));
  } else {
    bool upheld = (strstr(e.native, "out") == nullptr);
    uint16_t c = upheld ? RGB565(80, 230, 110) : RGB565(255, 70, 70);
    dma->fillRect(0, SW_TOP + 1, PANEL_W, SW_H - 1, RGB565(6, 6, 10));
    if (upheld) {                                  // tick
      for (int i = 0; i < 6; i++)  dma->drawPixel(26 + i, 40 + i, c);
      for (int i = 0; i < 10; i++) dma->drawPixel(32 + i, 45 - i, c);
    } else {                                       // cross
      for (int i = 0; i < 12; i++) { dma->drawPixel(26 + i, 34 + i, c);
                                     dma->drawPixel(37 - i, 34 + i, c); }
    }
    gfxTextC(e.label, 52, gfxDim(c, 80));
  }
}

/* --- EV_LEAD_CHANGE: the two colour bars slide past each other and a
       chevron settles on the new leader. --- */
static void fxLeadChange(const LiveMatch &m, const MatchEvent &e, uint32_t f, uint8_t dur) {
  fxStage(e.color, f, 6);
  int t = (int)f * 100 / (dur ? dur : 1);
  int slide = SW_H * (t < 50 ? t : 100 - t) / 100;
  dma->fillRect(0, SW_TOP + 2 + slide, 3, SW_H - 2 - slide, m.home.color);
  dma->fillRect(PANEL_W - 3, SW_TOP + 2, SW_H - 2 - slide > 0 ? 3 : 0,
                SW_H - 2 - slide, m.away.color);
  int cx = e.homeSide ? 10 : PANEL_W - 10;
  for (int i = 0; i < 4; i++) {                    // chevron pointing at them
    dma->drawPixel(cx - 3 + i, 34 + i, e.color);
    dma->drawPixel(cx + 3 - i, 34 + i, e.color);
  }
  if (t > 40) gfxTextC(e.label, 44, gfxDim(e.color, 85));
}

/* --- EV_PERIOD: a low-key wipe carrying the period label across. --- */
static void fxPeriod(const MatchEvent &e, uint32_t f, uint8_t dur) {
  fxStage(e.color, f, 6);
  gfxWipe(gfxDim(e.color, 60), f, dur);
  // The label used to ride the wipe's leading edge, which walked it straight
  // off the right of the panel and left "END OF Q" rendering as "ND OF Q".
  // Let the wipe pass, then set the label still and centred.
  int t = (dur ? (int)f * 100 / dur : 100);
  if (t > 45) gfxPunch(e.label, nullptr, f - dur * 45 / 100, dur - dur * 45 / 100,
                       gfxDim(0xFFFF, 80));
}

/* --- EV_FINAL: a slow sparkle sweep converging on the winner's bar. --- */
static void fxFinal(const MatchEvent &e, uint32_t f, uint8_t dur) {
  fxStage(e.color, f, 10);
  int t = (int)f * 100 / (dur ? dur : 1);
  int tx = e.homeSide ? 2 : PANEL_W - 2;
  for (int i = 0; i < 16; i++) {
    int sx = prng8(i * 9 + 4) % PANEL_W;
    int sy = SW_TOP + 3 + prng8(i * 17 + 8) % (SW_H - 6);
    int x  = sx + (tx - sx) * t / 100;
    int y  = sy + (40 - sy) * t / 100;
    drawSpark(x, y, gfxDim(e.color, 100 - t / 2));
  }
  gfxTextC(e.punch, 30, gfxBlend(e.color, 0xFFFF, 40), 2);
  if (t > 35) gfxTextC(e.label, 48, gfxDim(0xFFFF, 60));
}

/* Dispatch. Sport modules are consulted first (see score_widget.h); this is
   the fallback that guarantees every event animates. */
static void fxGeneric(const LiveMatch &m, const MatchEvent &e, uint32_t f) {
  uint8_t dur = EV_FRAMES[e.kind < EV_KIND_COUNT ? e.kind : EV_SCORE_MAJOR];
  switch (e.kind) {
    case EV_SCORE_MINOR: fxScoreMinor(e, f, dur);      break;
    case EV_SCORE_MAJOR: fxScoreMajor(e, f, dur);      break;
    case EV_SCORE_BIG:   fxScoreBig(e, f, dur);        break;
    case EV_TURNOVER:    fxTurnover(e, f, dur);        break;
    case EV_MILESTONE:   fxMilestone(e, f, dur);       break;
    case EV_PENALTY:     fxPenalty(e, f, dur);         break;
    case EV_REVIEW:      fxReview(e, f, dur);          break;
    case EV_LEAD_CHANGE: fxLeadChange(m, e, f, dur);   break;
    case EV_PERIOD:      fxPeriod(e, f, dur);          break;
    case EV_FINAL:       fxFinal(e, f, dur);           break;
    default:             fxScoreMajor(e, f, dur);      break;
  }
}
