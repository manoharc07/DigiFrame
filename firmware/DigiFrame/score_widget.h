/* DigiFrame — live scores: the dispatcher renderClock() calls.

   Owns rows 19..63 while the clock keeps 0..18. Draws the shared chrome, hands
   the body to the sport module, and runs at most one event animation on top —
   the sport's own if it has one for that event, otherwise the generic effect
   from score_fx.h. */
#pragma once

static MatchEvent evPlaying;
static bool       evActive  = false;
static uint32_t   evStartF  = 0;

/* Pull the next event to play, honouring priority and the user's intensity
   setting. A higher-priority event preempts one already running, so a goal
   never waits behind a possession change. */
static void scoreEventSchedule(uint32_t f) {
  if (sportFx == 0) {                       // animations off: drain the ring
    evActive = false;
    MatchEvent drop;
    while (evPop(drop)) {}
    return;
  }
  if (evActive && (f - evStartF) >= EV_FRAMES[evPlaying.kind]) evActive = false;

  uint8_t waiting = evPeekPriority();
  if (!waiting) return;
  if (evActive && waiting <= evPriority(evPlaying.kind)) return;

  MatchEvent next;
  if (!evPop(next)) return;
  // Major-only (the default) keeps the moments worth looking up for and drops
  // the rest. This is an explicit set rather than a priority threshold: the
  // priority order ranks how events preempt one another, which is a different
  // question from whether an event deserves the panel at all. A cricket wicket
  // is EV_TURNOVER and a century is EV_MILESTONE — both are headline moments —
  // while a VAR check, a yellow card, a lead change and the end of a quarter
  // are broadcast furniture on a clock you glance at from the sofa.
  if (sportFx == 1 && !evIsMajor(next.kind)) return;
  evPlaying = next;
  evActive  = true;
  evStartF  = f;
}

/* keep the rotation's view of "mid-animation" in step with the renderer's */
static inline void scoreFxPublish() { scoreFxBusy = evActive; }

void drawScoreWidget(uint32_t f) {
  const LiveMatch *mp = activeMatchPtr();
  if (!mp) {                                // preview with nothing live yet
    evActive = false; scoreFxPublish();
    gfxCard(RGB565(90, 90, 110));
    gfxTextC("NO MATCH", 34, gfxDim(0xFFFF, 45));
    gfxTextC("LIVE", 46, gfxDim(0xFFFF, 30));
    return;
  }
  const LiveMatch  &m = *mp;
  const SportModule *s = sportOf(m.sport);

  scoreEventSchedule(f);
  scoreFxPublish();

  gfxCard(s->accent);
  gfxTeamBars(m, f);
  gfxLiveDot(f, m.state);
  s->drawBody(m, f);
  /* Drawn here, not in each body, so every sport gets the same device in the
     same rows and a new sport_*.h inherits it by supplying one function. */
  if (s->progress) gfxMeter(s->progress(m), f);

  if (evActive) {
    uint32_t ef = f - evStartF;
    if (!(s->drawEvent && s->drawEvent(m, evPlaying, ef)))
      fxGeneric(m, evPlaying, ef);
  }
}

/* Fire an event by hand — the dashboard's animation tester. Runs on core 1;
   builds the event against the active match so the colours are right. */
void scoreTestEvent(const char *native) {
  const LiveMatch *mp = activeMatchPtr();
  MatchEvent e;
  memset(&e, 0, sizeof(e));
  e.at    = millis();
  e.sport = mp ? mp->sport : 0;
  strlcpy(e.native, native, sizeof(e.native));
  sportMapEvent(e.sport, native, e);
  e.homeSide = true;
  e.color    = mp ? mp->home.color : sportOf(e.sport)->accent;
  evPush(e);
  logLine("score fx test: " + String(native));
}
