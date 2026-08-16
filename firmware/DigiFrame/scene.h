/* DigiFrame — clock face + ambient scene (sprites, couple, fireworks, renderClock) */
#pragma once

/**********************  8. CLOCK FACE + AMBIENT SCENE  ***************/
/* The middle of the screen (rows 10-20) is a living scene that changes
 * as the day progresses:
 *   06:00-18:30  a sun rises, arcs across the sky, turns orange at dusk
 *   19:00-06:00  a crescent moon drifts by, stars twinkle
 *   20:00-05:00  a sleeping cat appears with rising "Zzz"
 *   live weather clouds drift, rain falls, lightning flickers
 *   a playful sprite/vehicle wanders past now and then; sparkles on the hour */

uint32_t frameNo        = 0;
uint32_t hourBurstUntil = 0;
int      prevHour       = -1;

/* ---- scene-event director: one playful thing happens at a time ---- */
enum SceneEvent { SCN_NONE, SCN_CRITTER, SCN_CHASE, SCN_DUO,
                  SCN_BIKE, SCN_F1, SCN_ROCKET, SCN_HERO };
SceneEvent sceneEvent    = SCN_NONE;
int        sceneX        = -16;
int        sceneX2       = 0;
int        sceneY        = 0;
bool       sceneActing   = false;
uint32_t   sceneActUntil = 0;
uint32_t   nextSceneAt   = 15000;
int        activeSprite  = 0;
int        duoPhase      = 0;   // 0=approach, 1=paused/high-five, 2=retreat

bool       nightFlutterOn     = false;
int        nightFlutterX      = -6;
uint32_t   nextNightFlutterAt = 20000;

uint8_t prng8(uint16_t n) { n ^= n << 7; n ^= n >> 9; n *= 31; return n & 0xFF; }

/* reserved for a future "anniversary" celebration theme — not used by the
   default clock scene (kept small so it's cheap to re-enable later). */
void drawHeart(int x, int y, uint16_t c) {
  dma->drawPixel(x + 1, y, c);     dma->drawPixel(x + 3, y, c);
  dma->drawFastHLine(x, y + 1, 5, c);
  dma->drawFastHLine(x + 1, y + 2, 3, c);
  dma->drawPixel(x + 2, y + 3, c);
}
/* tiny neutral 4-point sparkle — the ambient scene's accent flourish */
void drawSpark(int x, int y, uint16_t c) {
  dma->drawPixel(x, y, c);
  dma->drawPixel(x - 1, y, c);
  dma->drawPixel(x + 1, y, c);
  dma->drawPixel(x, y - 1, c);
  dma->drawPixel(x, y + 1, c);
}
void drawZ(int x, int y, uint16_t c) {          // tiny 3x3 "z"
  dma->drawFastHLine(x, y, 3, c);
  dma->drawPixel(x + 1, y + 1, c);
  dma->drawFastHLine(x, y + 2, 3, c);
}
void drawCat(int x, int y, uint16_t c) {        // sleeping kitty, ~8x5
  dma->fillRect(x, y + 2, 6, 2, c);             // body
  dma->fillRect(x + 4, y, 3, 3, c);             // head
  dma->drawPixel(x + 4, y - 1, c);              // ears
  dma->drawPixel(x + 6, y - 1, c);
  dma->drawPixel(x - 1, y + 1, c);              // tail tip
  dma->drawPixel(x - 1, y + 2, c);
}
void drawCloud(int x, int y, uint16_t c) {
  if (x < -8 || x > PANEL_W) return;
  dma->fillRoundRect(x, y + 1, 7, 3, 1, c);
  dma->fillRect(x + 2, y, 3, 2, c);
}
/* A recognizable little cat, ~11 wide x 9 tall, facing right.
   Pointy ears, round head with eyes, arched back, curved up tail,
   and a 2-frame leg walk cycle. jump>0 lifts + tucks the legs. */
void drawWalkCat(int x, int y, uint16_t c, bool stepA, int jump) {
  uint16_t face = dma->color565(255, 225, 235);   // lighter muzzle
  uint16_t eye  = dma->color565(160, 100, 190);
  uint16_t nose = dma->color565(255, 120, 150);
  y -= jump;
  // curved tail sweeping up behind
  dma->drawPixel(x,     y - 2, c);
  dma->drawPixel(x - 1, y - 3, c);
  dma->drawPixel(x - 1, y - 5, c);
  dma->drawPixel(x,     y - 6, c);
  dma->drawPixel(x + 1, y - 6, c);
  // body (arched back)
  dma->fillRect(x + 1, y - 3, 7, 3, c);
  dma->drawFastHLine(x + 2, y - 4, 5, c);         // hump of the back
  // head
  dma->fillRect(x + 7, y - 5, 4, 4, c);
  dma->drawPixel(x + 10, y - 2, face);            // muzzle
  dma->drawPixel(x + 9,  y - 2, face);
  // pointy ears
  dma->drawPixel(x + 7, y - 6, c);
  dma->drawPixel(x + 10, y - 6, c);
  // eyes + nose
  dma->drawPixel(x + 8,  y - 4, eye);
  dma->drawPixel(x + 10, y - 4, eye);
  dma->drawPixel(x + 11, y - 3, nose);
  // whisker
  dma->drawPixel(x + 12, y - 2, face);
  // legs (walk cycle / tucked when jumping)
  if (jump) {
    dma->drawPixel(x + 2, y, c);
    dma->drawPixel(x + 6, y, c);
  } else if (stepA) {
    dma->drawFastVLine(x + 2, y, 2, c);
    dma->drawFastVLine(x + 6, y, 2, c);
  } else {
    dma->drawFastVLine(x + 3, y, 2, c);
    dma->drawFastVLine(x + 5, y, 2, c);
  }
}

/* ---------- SPRITE MENAGERIE v2 — each has a themed ACTIVITY ----------
   acting=false: normal walk cycle. acting=true: sprite pauses mid-scene
   and performs a small themed idle animation (dig, munch, slide, etc). */
typedef void (*SpriteFn)(int, int, uint32_t, bool);

void sprBunny(int x, int y, uint32_t f, bool acting) {
  uint16_t b = dma->color565(240, 240, 250), p = dma->color565(255, 160, 190),
           carrot = dma->color565(255, 150, 60), leaf = dma->color565(120, 200, 100);
  if (acting) {                                  // munching a carrot
    dma->fillRect(x + 1, y - 4, 5, 4, b);
    dma->drawFastVLine(x + 1, y - 8, 4, b);
    dma->drawFastVLine(x + 4, y - 8, 4, b);
    dma->drawPixel(x + 1, y - 8, p); dma->drawPixel(x + 4, y - 8, p);
    dma->drawPixel(x + 5, y - 3, dma->color565(160, 100, 190));
    int bite = (f / 6) % 3;
    dma->fillRect(x + 6, y - 2, 2, 3 - bite, carrot);
    if (bite < 2) dma->drawPixel(x + 6, y - 3, leaf);
    return;
  }
  int hop = ((f / 4) % 2) ? 2 : 0;  y -= hop;
  dma->fillRect(x + 1, y - 4, 5, 4, b);
  dma->drawFastVLine(x + 1, y - 8, 4, b);
  dma->drawFastVLine(x + 4, y - 8, 4, b);
  dma->drawPixel(x + 1, y - 8, p);
  dma->drawPixel(x + 4, y - 8, p);
  dma->drawPixel(x + 5, y - 3, dma->color565(160, 100, 190));
  dma->fillCircle(x + 6, y, 1, b);
  if (!hop) { dma->drawPixel(x + 1, y, b); dma->drawPixel(x + 5, y, b); }
}

void sprPenguin(int x, int y, uint32_t f, bool acting) {
  uint16_t bk = dma->color565(50, 60, 90), wh = dma->color565(240, 245, 255),
           bl = dma->color565(255, 170, 60), ice = dma->color565(180, 220, 255);
  if (acting) {                                  // belly-slide!
    int slide = (f / 3) % 8;
    dma->drawFastHLine(x - 2, y, 12, ice);        // ice patch
    dma->fillRect(x + 1 + slide / 3, y - 3, 6, 3, bk);  // low, sliding pose
    dma->fillRect(x + 2 + slide / 3, y - 2, 4, 2, wh);
    dma->drawPixel(x + slide, y, wh);             // motion spark
    return;
  }
  bool wobble = (f / 5) % 2;
  dma->fillRect(x + 1, y - 7, 6, 7, bk);
  dma->fillRect(x + 2, y - 4, 4, 4, wh);
  dma->drawPixel(x + 3, y - 6, wh);
  dma->drawPixel(x + 5, y - 6, wh);
  dma->drawPixel(x + 4, y - 5, bl);
  dma->drawPixel(wobble ? x : x + 1, y - 3, bk);
  dma->drawPixel(x + 7, y - 3, bk);
  dma->drawPixel(x + 2, y, bl);
  dma->drawPixel(x + 5, y, bl);
}

void sprFrog(int x, int y, uint32_t f, bool acting) {
  uint16_t g = dma->color565(120, 210, 120), d = dma->color565(70, 160, 80),
           tongue = dma->color565(255, 130, 150);
  if (acting) {                                  // tongue-flick catch
    int flick = (f / 4) % 6;
    int tl = (flick < 3) ? flick * 3 : (6 - flick) * 3;
    dma->fillRect(x + 1, y - 4, 6, 4, g);
    dma->drawPixel(x + 1, y - 5, dma->color565(60, 100, 190));
    dma->drawPixel(x + 5, y - 5, dma->color565(60, 100, 190));
    if (tl > 0) dma->drawFastHLine(x + 6, y - 2, tl, tongue);
    dma->drawPixel(x, y, g); dma->drawPixel(x + 7, y, g);
    return;
  }
  int hop = ((f / 4) % 3 == 0) ? 3 : 0;  y -= hop;
  dma->fillRect(x + 1, y - 4, 6, 4, g);
  dma->drawPixel(x + 1, y - 5, g);
  dma->drawPixel(x + 5, y - 5, g);
  dma->drawPixel(x + 1, y - 5, dma->color565(240, 255, 240));
  dma->drawPixel(x + 5, y - 5, dma->color565(240, 255, 240));
  dma->drawPixel(x + 1, y - 5, dma->color565(60, 100, 190));
  dma->drawPixel(x + 5, y - 5, dma->color565(60, 100, 190));
  dma->drawFastHLine(x + 2, y - 2, 3, d);
  if (hop) { dma->drawPixel(x, y - 1, g); dma->drawPixel(x + 7, y - 1, g); }
  else     { dma->drawPixel(x, y, g);     dma->drawPixel(x + 7, y, g); }
}

void sprGhost(int x, int y, uint32_t f, bool acting) {
  uint16_t g = dma->color565(225, 225, 245), e = dma->color565(160, 165, 220);
  if (acting) {                                  // spin a playful "boo" loop
    int spin = (f / 4) % 4;
    int wob = (spin == 1 || spin == 3) ? 1 : 0;
    dma->fillRoundRect(x + 1 - wob, y - 8, 6, 6, 2, g);
    dma->fillRect(x + 1 - wob, y - 3, 6, 2, g);
    dma->drawPixel(x + 3 - wob, y - 6, e);
    dma->drawPixel(x + 5 - wob, y - 6, e);
    return;
  }
  int bob = ((f / 5) % 2) ? 1 : 0;  y -= bob;
  dma->fillRoundRect(x + 1, y - 7, 6, 6, 2, g);
  dma->fillRect(x + 1, y - 2, 6, 2, g);
  dma->drawPixel(x + 1, y, ((f / 3) % 2) ? g : 0);
  dma->drawPixel(x + 3, y, ((f / 3) % 2) ? 0 : g);
  dma->drawPixel(x + 5, y, ((f / 3) % 2) ? g : 0);
  dma->drawPixel(x + 3, y - 5, e);
  dma->drawPixel(x + 5, y - 5, e);
}

void sprDuck(int x, int y, uint32_t f, bool acting) {
  uint16_t yl = dma->color565(255, 220, 90), bl = dma->color565(255, 150, 40),
           e = dma->color565(140, 85, 45), water = dma->color565(120, 190, 255);
  if (acting) {                                  // paddling in a puddle
    dma->fillRoundRect(x - 3, y, 12, 3, 1, water);
    int bob = (f / 5) % 2;
    dma->fillRect(x + 1, y - 5 + bob, 5, 5, yl);
    dma->fillRect(x + 4, y - 7 + bob, 3, 3, yl);
    dma->drawPixel(x + 6, y - 6 + bob, e);
    dma->drawFastHLine(x + 7, y - 5 + bob, 2, bl);
    dma->drawPixel(x - 1, y + 1, water);          // ripple
    dma->drawPixel(x + 8, y + 1, water);
    return;
  }
  bool step = (f / 5) % 2;
  dma->fillRect(x + 1, y - 5, 5, 5, yl);
  dma->fillRect(x + 4, y - 7, 3, 3, yl);
  dma->drawPixel(x + 6, y - 6, e);
  dma->drawFastHLine(x + 7, y - 5, 2, bl);
  dma->drawPixel(step ? x + 2 : x + 3, y, bl);
  dma->drawPixel(x + 4, y, bl);
}

void sprRobot(int x, int y, uint32_t f, bool acting) {
  uint16_t m = dma->color565(150, 200, 230), d = dma->color565(90, 130, 170),
           e = dma->color565(120, 255, 220);
  if (acting) {                                  // radar scan + beep
    dma->fillRect(x + 1, y - 7, 6, 4, m);
    dma->drawPixel(x + 2, y - 5, e); dma->drawPixel(x + 5, y - 5, e);
    dma->fillRect(x + 2, y - 3, 4, 3, d);
    dma->drawPixel(x + 2, y, m); dma->drawPixel(x + 5, y, m);
    int r = (f / 3) % 5;                          // expanding scan arcs
    if (r > 0) { dma->drawPixel(x + 3 - r, y - 8, e); dma->drawPixel(x + 4 + r, y - 8, e); }
    return;
  }
  bool step = (f / 5) % 2;
  dma->drawFastVLine(x + 3, y - 9, 2, d);
  dma->drawPixel(x + 3, y - 10, e);
  dma->fillRect(x + 1, y - 7, 6, 4, m);
  dma->drawPixel(x + 2, y - 5, e);
  dma->drawPixel(x + 5, y - 5, e);
  dma->fillRect(x + 2, y - 3, 4, 3, d);
  dma->drawPixel(step ? x + 2 : x + 3, y, m);
  dma->drawPixel(step ? x + 5 : x + 4, y, m);
}

void sprBuddy(int x, int y, uint32_t f, bool acting) {
  uint16_t bd = dma->color565(255, 215, 90), gg = dma->color565(200, 210, 225),
           pt = dma->color565(90, 130, 220), e = dma->color565(50, 190, 195),
           spark = dma->color565(255, 240, 180);
  if (acting) {                                   // little happy dance
    int sway = ((f / 4) % 4);
    int lean = (sway == 1) ? 1 : (sway == 3) ? -1 : 0;
    dma->fillRoundRect(x + 1 + lean, y - 8, 6, 8, 2, bd);
    dma->fillRect(x + 1 + lean, y - 3, 6, 3, pt);
    dma->drawFastHLine(x + 1 + lean, y - 6, 6, gg);
    dma->fillCircle(x + 3 + lean, y - 6, 1, e);
    if (sway % 2 == 0) { dma->drawPixel(x - 1, y - 9, spark); dma->drawPixel(x + 8, y - 9, spark); }
    return;
  }
  int hop = (int)(fabs(sinf(f / 6.0f)) * 4);  y -= hop;
  dma->fillRoundRect(x + 1, y - 8, 6, 8, 2, bd);
  dma->fillRect(x + 1, y - 3, 6, 3, pt);
  dma->drawFastHLine(x + 1, y - 6, 6, gg);
  dma->fillCircle(x + 3, y - 6, 1, e);
  if (!hop) { dma->drawPixel(x + 2, y, bd); dma->drawPixel(x + 5, y, bd); }
}

SpriteFn SPRITES[] = { sprBunny, sprPenguin, sprFrog, sprGhost,
                       sprDuck, sprRobot, sprBuddy };
const int NUM_SPRITES = 7;

/* ---------- flutter friend: butterfly (day) / firefly (night) ---------- */
void drawFlutter(int x, int y, uint32_t f, bool night) {
  uint16_t body = dma->color565(70, 60, 60);
  uint16_t wing = night ? dma->color565(200, 255, 150) : dma->color565(255, 170, 210);
  int by = y + (int)(sinf(f / 5.0f) * 2);
  dma->drawPixel(x, by, body);
  bool up = (f / 3) % 2;
  dma->drawPixel(x - 1, by + (up ? -1 : 0), wing);
  dma->drawPixel(x + 1, by + (up ? -1 : 0), wing);
  if (night) dma->drawPixel(x, by, dma->color565(230, 255, 180));  // glow core
}

/* ---------- bicycle: rider pedaling across ---------- */
void drawBike(int x, int y, uint32_t f) {
  uint16_t frame = dma->color565(230, 230, 240), wheel = dma->color565(185, 190, 205),
           rider = dma->color565(255, 150, 170), spoke = dma->color565(170, 180, 200);
  dma->drawCircle(x, y, 3, wheel);
  dma->drawCircle(x + 12, y, 3, wheel);
  int spin = (f / 2) % 4;                          // rotating spoke
  static const int8_t sxo[4] = { 0, 2, 0, -2 }, syo[4] = { -2, 0, 2, 0 };
  dma->drawPixel(x + sxo[spin], y + syo[spin], spoke);
  dma->drawPixel(x + 12 + sxo[(spin + 2) % 4], y + syo[(spin + 2) % 4], spoke);
  dma->drawLine(x, y, x + 6, y - 5, frame);         // frame triangle
  dma->drawLine(x + 6, y - 5, x + 12, y, frame);
  dma->drawLine(x + 3, y - 2, x + 6, y - 5, frame);
  dma->fillRect(x + 4, y - 10, 3, 4, rider);        // rider torso
  dma->fillCircle(x + 5, y - 12, 2, rider);         // head
  int pedal = (f / 4) % 2;                          // pedaling legs
  dma->drawPixel(x + 5 + pedal, y - 4, rider);
  dma->drawPixel(x + 7 - pedal, y - 3, rider);
}

/* ---------- F1 car: quick, low, with a speed-line trail ---------- */
void drawF1Car(int x, int y, uint32_t f) {
  uint16_t body = dma->color565(230, 40, 60), wing = dma->color565(190, 30, 55),
           wheel = dma->color565(150, 155, 170), trail = dma->color565(255, 210, 120);
  dma->fillRect(x, y - 3, 10, 2, body);             // body
  dma->fillRect(x - 2, y - 4, 2, 1, wing);          // front wing
  dma->fillRect(x + 10, y - 5, 2, 2, wing);         // rear wing
  dma->fillCircle(x + 2, y, 1, wheel);
  dma->fillCircle(x + 8, y, 1, wheel);
  dma->drawPixel(x + 5, y - 4, dma->color565(240, 240, 245));  // helmet glint
  for (int i = 1; i <= 3; i++)                      // speed streaks behind
    dma->drawFastHLine(x - 2 - i * 3, y - 3 + (i % 2), 2, trail);
}

/* ---------- rocket: launches straight up through the scene ---------- */
void drawRocket(int x, int y, uint32_t f) {
  uint16_t body = dma->color565(235, 235, 245), nose = dma->color565(255, 120, 140),
           fin = dma->color565(120, 150, 220), win = dma->color565(150, 210, 255),
           flm = dma->color565(255, 190, 80), flm2 = dma->color565(255, 120, 60);
  dma->fillTriangle(x, y - 8, x - 2, y - 4, x + 2, y - 4, nose);   // nose cone
  dma->fillRect(x - 2, y - 4, 5, 6, body);                        // body
  dma->fillCircle(x, y - 2, 1, win);                              // window
  dma->fillTriangle(x - 2, y + 2, x - 4, y + 4, x - 2, y, fin);    // fins
  dma->fillTriangle(x + 2, y + 2, x + 4, y + 4, x + 2, y, fin);
  bool flick = (f / 3) % 2;
  dma->fillTriangle(x - 1, y + 2, x + 1, y + 2, x, y + 5 + (flick?1:0),
                    flick ? flm : flm2);            // flame
  if (f % 6 == 0) dma->drawPixel(x + ((f/6)%2 ? -3 : 3), y + 6, dma->color565(120,120,130)); // smoke puff
}

/* ---------- original caped hero (NOT any licensed character) ---------- */
void drawHero(int x, int y, uint32_t f) {
  uint16_t suit = dma->color565(70, 110, 220), cape = dma->color565(220, 60, 70),
           skin = dma->color565(255, 205, 170), mask = dma->color565(255, 205, 90);
  int ripple = (f / 3) % 3 - 1;                     // cape ripple
  dma->fillTriangle(x - 1, y - 4, x - 5, y - 2 + ripple, x - 5, y + 3 + ripple, cape);
  dma->fillRect(x, y - 6, 4, 5, suit);               // torso
  dma->fillCircle(x + 2, y - 8, 2, skin);            // head
  dma->fillRect(x, y - 9, 4, 1, mask);                // mask band
  dma->drawPixel(x + 5, y - 5, skin);                // forward arm (flying pose)
  dma->drawPixel(x + 1, y, suit); dma->drawPixel(x + 3, y, suit);  // legs trailing
}

/* ---------- persistent potted plant, a different bloom each day ---------- */
uint32_t nextBloomVisitAt = 45000;
bool     bloomVisiting = false;

/* 5 original flower styles, picked deterministically from the date so
   the same flower holds steady all day and changes tomorrow. */
void drawBloom(int cx, int cy, uint32_t f, int variant) {
  switch (variant) {
    case 0: {                                       // daisy / aster
      uint16_t petal = dma->color565(255, 170, 200), core = dma->color565(255, 220, 110);
      dma->fillCircle(cx, cy, 2, petal);
      dma->fillCircle(cx, cy, 1, core);
      break;
    }
    case 1: {                                       // sunflower
      uint16_t yel = dma->color565(255, 205, 60), brn = dma->color565(140, 95, 50);
      static const int8_t ox[8] = { -3, 3, 0, 0, -2, 2, -2, 2 };
      static const int8_t oy[8] = { 0, 0, -3, 3, -2, -2, 2, 2 };
      for (int i = 0; i < 8; i++) dma->drawPixel(cx + ox[i], cy + oy[i], yel);
      dma->fillCircle(cx, cy, 1, brn);
      break;
    }
    case 2: {                                       // tulip
      uint16_t red = dma->color565(230, 60, 90);
      dma->drawPixel(cx, cy - 2, red);
      dma->fillRect(cx - 1, cy - 1, 3, 2, red);
      dma->drawPixel(cx - 2, cy, red);
      dma->drawPixel(cx + 2, cy, red);
      break;
    }
    case 3: {                                       // rose
      uint16_t deep = dma->color565(220, 60, 100), light = dma->color565(255, 150, 180);
      dma->fillCircle(cx, cy, 2, deep);
      dma->fillCircle(cx, cy, 1, light);
      dma->drawPixel(cx - 2, cy + 1, deep);
      dma->drawPixel(cx + 2, cy + 1, deep);
      break;
    }
    default: {                                       // lotus
      uint16_t pale = dma->color565(255, 210, 225), mid = dma->color565(255, 235, 245),
               core = dma->color565(255, 225, 140);
      static const int8_t ox[6] = { -2, 2, -3, 3, 0, 0 };
      static const int8_t oy[6] = { -1, -1, 1, 1, -2, 2 };
      for (int i = 0; i < 6; i++) dma->drawPixel(cx + ox[i], cy + oy[i], (i < 4) ? pale : mid);
      dma->fillCircle(cx, cy, 1, core);
      break;
    }
  }
}

void drawPottedPlant(int x, int y, uint32_t f) {
  uint16_t pot  = dma->color565(190, 110, 70);
  uint16_t stem = dma->color565(90, 170, 90);
  uint16_t leaf = dma->color565(110, 200, 110);
  dma->fillRect(x, y, 14, 4, pot);                   // wider pot, edge to edge
  dma->drawFastHLine(x - 1, y, 16, dma->color565(150, 85, 55));
  int sway = (int)(sinf(f / 25.0f) * 1.4f);          // gentle sway
  dma->drawFastVLine(x + 7 + sway, y - 8, 8, stem);
  dma->fillCircle(x + 3 + sway, y - 5, 1, leaf);      // leaves spread wider
  dma->fillCircle(x + 11 + sway, y - 4, 1, leaf);
  dma->fillCircle(x + 5 + sway, y - 2, 1, leaf);
  int variant = tmNow.tm_yday % 5;                   // a new flower each day
  drawBloom(x + 7 + sway, y - 10, f, variant);
  // an occasional butterfly stops by for a rest
  if (!bloomVisiting && millis() > nextBloomVisitAt) bloomVisiting = true;
  if (bloomVisiting) {
    drawFlutter(x + 7 + sway, y - 13, f, false);
    if ((f / 40) % 3 == 0) { bloomVisiting = false; nextBloomVisitAt = millis() + 20000 + esp_random() % 40000; }
  }
}

/* A small birthday cake for the clock face (~14 wide). Candle flickers. */
void drawMiniCake(int x, int y, uint32_t f) {
  uint16_t PLATE = dma->color565(200, 200, 215);
  uint16_t TIER  = dma->color565(255, 170, 190);
  uint16_t ICE   = dma->color565(255, 240, 245);
  uint16_t CND   = dma->color565(180, 220, 255);
  uint16_t FLM   = dma->color565(255, 200,  80);
  uint16_t FLM2  = dma->color565(255, 140,  60);
  dma->drawFastHLine(x, y + 8, 14, PLATE);      // plate
  dma->fillRect(x + 1, y + 3, 12, 5, TIER);     // body
  dma->drawFastHLine(x + 1, y + 3, 12, ICE);    // icing
  dma->drawPixel(x + 3, y + 5, ICE);            // drips
  dma->drawPixel(x + 7, y + 6, ICE);
  dma->drawPixel(x + 10, y + 5, ICE);
  dma->drawFastVLine(x + 6, y, 3, CND);         // candle
  dma->drawPixel(x + 6, y - 1 - (int)((f / 4) % 2),
                 ((f / 4) % 2) ? FLM : FLM2);   // flickering flame
}

/* A looping firework: a bright streak rises, then bursts into an
 * expanding 8-point star that fades out. 80-frame cycle. */
void drawFirework(int cx, int cy, uint32_t f, uint16_t c1, uint16_t c2) {
  int t = (int)(f % 80);
  if (t < 24) {                                    // rising streak
    int y0 = 45 - ((45 - cy) * t) / 24;
    dma->drawPixel(cx, y0, dma->color565(255, 240, 200));
    if (t > 2) dma->drawPixel(cx, y0 + 2, dma->color565(140, 110, 80));
  } else {                                         // radial burst
    int r = (t - 24) / 6;
    if (r >= 1 && r <= 7) {
      static const int8_t dx[8] = { 1, -1, 0, 0, 1, -1, 1, -1 };
      static const int8_t dy[8] = { 0, 0, 1, -1, 1, 1, -1, -1 };
      for (int i = 0; i < 8; i++)
        if (r < 6 || (i % 2))                      // outer ring thins as it fades
          dma->drawPixel(cx + dx[i] * r, cy + dy[i] * r, (i % 2) ? c1 : c2);
      if (r <= 3) dma->drawPixel(cx, cy, dma->color565(255, 255, 230)); // hot core
    }
  }
}

/* The "birthday" celebration theme: the cat scampers up to the cake and
 * bats at it, sparkles popping above. Used by MODE_CELEBRATE, not the
 * default clock. */
void drawCakeAndCat(uint32_t f) {
  const int BOT = 45, TOP = 19;
  uint16_t CATC = dma->color565(255, 170, 190);

  /* confetti drifting down through the scene band all day */
  uint16_t confs[4] = { dma->color565(255, 120, 160), dma->color565(140, 200, 255),
                        dma->color565(255, 220, 120), dma->color565(170, 255, 170) };
  for (int k = 0; k < 8; k++) {
    int rx = (prng8(k * 29 + 3) + k * 5) % PANEL_W;
    int ry = TOP + (int)((f / 2 + prng8(k * 13)) % (BOT - TOP));
    dma->drawPixel(rx, ry, confs[k % 4]);
  }

  /* twin fireworks bursting on offset cycles, left and right of the cake */
  drawFirework(10, 26, f,      dma->color565(255, 120, 160), dma->color565(255, 220, 120));
  drawFirework(56, 24, f + 40, dma->color565(140, 200, 255), dma->color565(190, 150, 255));

  int cakeX = 38;
  drawMiniCake(cakeX, BOT - 12, f);
  // cat walks in from the left, stops by the cake, paws at it
  int stopX = cakeX - 12;
  int cx = -14 + (int)((f / 2) % 200);          // loop the approach
  if (cx > stopX) cx = stopX;                   // wait at the cake
  bool pawing = (cx == stopX);
  int paw = pawing ? ((f / 5) % 2) : 0;         // bat up and down
  drawWalkCat(cx, BOT, CATC, (f / 6) % 2, paw); // small hop = paw swipe
  if (pawing && (f / 5) % 2)
    drawSpark(cakeX + 3, BOT - 18, C_ACCENT);   // sparkle over cake
}

void drawAmbient() {
  const int TOP = 19, BOT = 45;                 // scene strip rows

  int  mins  = tmNow.tm_hour * 60 + tmNow.tm_min;
  bool day   = (mins >= 360 && mins < 1110);    // 06:00 - 18:30
  bool night = (tmNow.tm_hour >= 20 || tmNow.tm_hour < 5);

  uint16_t SUN   = dma->color565(255, 214,  90);
  uint16_t SUNst = dma->color565(255, 150, 110);   // sunset orange
  uint16_t MOON  = dma->color565(225, 228, 240);
  uint16_t STAR  = dma->color565(190, 200, 230);
  uint16_t CATC  = dma->color565(255, 170, 190);
  uint16_t ZZZ   = dma->color565(150, 160, 200);

  /* --- sun arcs across the day / moon drifts through the night --- */
  int celX;                                     // where the sun/moon is
  if (day) {
    float p  = (mins - 360) / 750.0f;           // 0..1 across daylight
    int   sx = 3 + (int)(p * 57);
    int   sy = BOT - 3 - (int)(sinf(p * PI) * 14);
    celX = sx;
    dma->fillCircle(sx, sy, 3, (mins > 1020) ? SUNst : SUN);
    if ((frameNo / 8) % 2) {                    // gentle ray shimmer
      dma->drawPixel(sx, sy - 5, SUN);
      dma->drawPixel(sx - 5, sy, SUN);
      dma->drawPixel(sx + 5, sy, SUN);
      dma->drawPixel(sx, sy + 5, SUN);
    }
  } else {
    int   nm = (mins >= 1140) ? mins - 1140 : mins + 300;  // 19:00 base
    float p  = nm / 660.0f;                     // 0..1 across the night
    int   mx = 3 + (int)(p * 57);
    int   my = BOT - 3 - (int)(sinf(p * PI) * 14);
    celX = mx;
    dma->fillCircle(mx, my, 3, MOON);
    dma->fillCircle(mx + 2, my - 1, 3, 0);      // bite -> crescent
  }

  /* --- twinkling stars always; sleeping cat OR a couple on a bench --- */
  if (night) {
    static const uint8_t sxs[12] = { 5, 12, 20, 27, 34, 41, 48, 56, 60, 16, 37, 52 };
    static const uint8_t sys[12] = { 23, 28, 22, 32, 25, 36, 24, 30, 38, 40, 41, 35 };
    for (int i = 0; i < 12; i++)
      if (((frameNo / 5) + i * 3) % 7 < 4)
        dma->drawPixel(sxs[i], sys[i], STAR);

    // a sleeping cat curled up under the stars, dreaming "Zzz"
    drawCat(5, BOT - 4, CATC);
    int zp = (frameNo / 12) % 3;
    drawZ(15, BOT - 7 - zp * 2, ZZZ);
    if (zp == 2) drawZ(19, BOT - 13, ZZZ);
  }

  /* --- daytime: a scene-event director picks what happens next ---
   * CRITTER: one of 7 sprites walks across, ~40% pausing mid-scene to
   *          do a themed activity (munch, slide, tongue-flick, etc).
   *          The cat is the frequent star and still hops at the sun.
   * CHASE:   the cat chases a fluttering butterfly clear across.
   * DUO:     buddy & robot approach from both sides and high-five.
   * BIKE / F1 / ROCKET / HERO: standalone cameo vehicles & original hero. */
  if (!night) {
    if (sceneEvent == SCN_NONE && millis() > nextSceneAt) {
      uint32_t r = esp_random() % 100;
      if      (r < 45) sceneEvent = SCN_CRITTER;
      else if (r < 55) sceneEvent = SCN_CHASE;
      else if (r < 65) sceneEvent = SCN_DUO;
      else if (r < 76) sceneEvent = SCN_BIKE;
      else if (r < 87) sceneEvent = SCN_F1;
      else if (r < 94) sceneEvent = SCN_ROCKET;
      else             sceneEvent = SCN_HERO;
      sceneX = -16; sceneX2 = PANEL_W + 16; sceneActing = false; sceneActUntil = 0; duoPhase = 0;
      activeSprite = (esp_random() % 2 == 0) ? -1 : (int)(esp_random() % NUM_SPRITES);
      sceneY = BOT;                                // rocket launch height
    }

    switch (sceneEvent) {
      case SCN_CRITTER: {
        int pausePt = 22 + (esp_random() % 12) - (esp_random() % 12); // vary stop point
        if (!sceneActing && sceneX < pausePt) sceneX++;
        if (!sceneActing && sceneX >= pausePt && (frameNo % 173) < 45)
          { sceneActing = true; sceneActUntil = millis() + 2600; }
        if (activeSprite < 0) {
          int jump = sceneActing ? 0 : 6 - abs((sceneX + 5) - celX);
          if (jump < 0) jump = 0;
          drawWalkCat(sceneX, BOT, CATC, (frameNo / 6) % 2, jump);
          if (!sceneActing && jump >= 5) drawSpark(sceneX + 4, BOT - 16, C_ACCENT);
        } else {
          SPRITES[activeSprite](sceneX, BOT, frameNo, sceneActing);
        }
        if (sceneActing && millis() > sceneActUntil) sceneActing = false;
        else if (!sceneActing && sceneX >= pausePt) sceneX++;
        if (sceneX > PANEL_W + 14) {
          sceneEvent = SCN_NONE;
          nextSceneAt = millis() + (18000UL + esp_random() % 22000UL);
        }
        break;
      }
      case SCN_CHASE: {                             // cat chases a butterfly
        int bx = sceneX + 10 + (int)(sinf(frameNo / 7.0f) * 3);
        drawFlutter(bx, BOT - 10, frameNo, false);
        int jump = ((frameNo / 3) % 4 == 0) ? 3 : 0;
        drawWalkCat(sceneX, BOT, CATC, (frameNo / 5) % 2, jump);
        sceneX += 1;
        if (sceneX > PANEL_W + 10) {
          sceneEvent = SCN_NONE;
          nextSceneAt = millis() + (20000UL + esp_random() % 25000UL);
        }
        break;
      }
      case SCN_DUO: {                                // buddy & robot high-five
        // duoPhase: 0=approaching, 1=paused/high-five, 2=retreating
        if (duoPhase == 0) {
          sceneX++; sceneX2--;
          if (sceneX >= 24 && sceneX2 <= 34) { duoPhase = 1; sceneActUntil = millis() + 1800; }
        } else if (duoPhase == 1) {
          if ((frameNo / 3) % 2)
            drawSpark((sceneX + sceneX2) / 2 + 3, BOT - 14, C_ACCENT);
          if (millis() > sceneActUntil) duoPhase = 2;
        } else {
          sceneX--; sceneX2++;
          if (sceneX < -16) {
            sceneEvent = SCN_NONE; duoPhase = 0;
            nextSceneAt = millis() + (25000UL + esp_random() % 25000UL);
          }
        }
        sprBuddy(sceneX, BOT, frameNo, false);
        sprRobot(sceneX2, BOT, frameNo, false);
        break;
      }
      case SCN_BIKE:
        drawBike(sceneX, BOT - 2, frameNo);
        sceneX += 2;
        if (sceneX > PANEL_W + 16) { sceneEvent = SCN_NONE;
          nextSceneAt = millis() + (20000UL + esp_random() % 25000UL); }
        break;
      case SCN_F1:
        drawF1Car(sceneX, BOT - 2, frameNo);
        sceneX += 4;
        if (sceneX > PANEL_W + 16) { sceneEvent = SCN_NONE;
          nextSceneAt = millis() + (25000UL + esp_random() % 30000UL); }
        break;
      case SCN_ROCKET:
        drawRocket(52, sceneY, frameNo);
        sceneY -= 1;
        if (sceneY < TOP - 4) { sceneEvent = SCN_NONE;
          nextSceneAt = millis() + (35000UL + esp_random() % 30000UL); }
        break;
      case SCN_HERO:
        drawHero(sceneX, TOP + 6, frameNo);
        sceneX += 2;
        if (sceneX > PANEL_W + 10) { sceneEvent = SCN_NONE;
          nextSceneAt = millis() + (30000UL + esp_random() % 30000UL); }
        break;
      default: break;
    }
  } else {
    // occasional firefly drifting by at night, just for polish
    if (!nightFlutterOn && millis() > nextNightFlutterAt) { nightFlutterOn = true; nightFlutterX = -6; }
    if (nightFlutterOn) {
      drawFlutter(nightFlutterX, TOP + 6 + (int)(sinf(frameNo/9.0f)*4), frameNo, true);
      nightFlutterX++;
      if (nightFlutterX > PANEL_W + 6) { nightFlutterOn = false;
        nextNightFlutterAt = millis() + 30000UL + esp_random() % 40000UL; }
    }
  }

  /* (weather effects live in the full-screen background layer) */

  /* --- sparkle burst at the top of every hour (2 s) --- */
  if (tmNow.tm_hour != prevHour) {
    prevHour = tmNow.tm_hour;
    hourBurstUntil = millis() + 2000;
  }
  if (millis() < hourBurstUntil) {
    for (int i = 0; i < 6; i++)
      if (((frameNo / 3) + i) % 2)
        drawSpark(4 + i * 10, TOP + (prng8(i * 7 + frameNo / 6) % 16), C_ACCENT);
  }

  /* --- playful extras: drifting balloon + soft sparkles ---
     The balloon vanishes at the scene's top edge, as if it floated
     off-scene: its 40-row rise used to carry it into rows 0-18 and paint
     it over the AM/PM text — the clock block is inviolate (CLAUDE.md). */
  uint32_t bt = frameNo % 900;
  if (bt < 220) {
    int by = BOT - (int)((bt / 220.0f) * 40);
    int bx = 48 + (int)(sinf(bt / 18.0f) * 2);
    if (by - 3 < 20) { bx = -10; by = -10; }   // above the scene: skip below
    uint16_t balls[3] = { dma->color565(255,120,150),
                          dma->color565(140,200,255),
                          dma->color565(255,210,120) };
    dma->fillCircle(bx, by, 3, balls[(frameNo / 300) % 3]);
    dma->drawPixel(bx, by + 3, dma->color565(200,200,210));
    for (int s = 1; s < 5; s++)
      dma->drawPixel(bx, by + 3 + s, dma->color565(120,120,140));
  }
  for (int s = 0; s < 3; s++) {
    int sx = prng8(s * 29 + frameNo / 20) % PANEL_W;
    int sy = TOP + prng8(s * 13 + frameNo / 25) % 18;
    if (((frameNo / 6) + s) % 5 == 0)
      dma->drawPixel(sx, sy, dma->color565(255,240,200));
  }
}

/* ---- full-screen weather background (deliberately dim so the ----
   ---- clock, scene and text always stay readable on top)      ---- */
void drawWeatherBg() {
  if (wCode < 0) return;
  uint16_t DIMCLOUD = dma->color565(38, 42, 50);
  uint16_t DIMRAIN  = dma->color565(40, 70, 120);
  uint16_t DIMSNOW  = dma->color565(80, 84, 92);
  uint16_t DIMFOG   = dma->color565(30, 33, 38);
  uint16_t DIMSTAR  = dma->color565(50, 55, 75);
  uint16_t GOLD     = dma->color565(70, 60, 25);
  uint16_t FLASH    = dma->color565(150, 145, 100);
  bool night = (tmNow.tm_hour >= 20 || tmNow.tm_hour < 5);

  if (wCode == 0) {                              // ---- clear ----
    if (night) {
      // faint starfield across the whole sky
      for (int i = 0; i < 24; i++) {
        int sx = prng8(i * 11 + 3) % PANEL_W;
        int sy = prng8(i * 7 + 29) % PANEL_H;
        if (((frameNo / 7) + i) % 9 < 5) dma->drawPixel(sx, sy, DIMSTAR);
      }
    } else {
      // sunny day: an occasional wandering golden sparkle
      if ((frameNo / 4) % 3 == 0) {
        int sx = prng8((uint16_t)(frameNo / 12) * 13) % PANEL_W;
        int sy = prng8((uint16_t)(frameNo / 12) * 7 + 5) % PANEL_H;
        dma->drawPixel(sx, sy, GOLD);
      }
    }
  } else if (wCode <= 3) {                       // ---- cloudy ----
    // three cloud shadows drifting at different speeds/heights
    for (int c = 0; c < 3; c++) {
      int cx = PANEL_W + 10 - (int)((frameNo / (10 + c * 4) + c * 17) % 94);
      int cy = 4 + c * 20;
      dma->fillRoundRect(cx, cy, 10, 3, 1, DIMCLOUD);
      dma->fillRect(cx + 3, cy - 1, 4, 2, DIMCLOUD);
    }
  } else if (wCode <= 48) {                      // ---- fog / mist ----
    for (int b = 0; b < 8; b++) {
      int off = ((frameNo / 14 + b * 8) % 80) - 8;
      dma->drawFastHLine(off, 4 + b * 8, 28, DIMFOG);
    }
  } else if (wCode <= 67 || (wCode >= 80 && wCode <= 82)) {  // ---- rain ----
    bool heavy = (wCode >= 63 || wCode >= 81);
    int  drops = heavy ? 20 : 12;
    int  speed = heavy ? 1 : 2;                  // heavy rain falls faster
    for (int k = 0; k < drops; k++) {
      int rx = (prng8(k * 13 + 7) + k * 5) % PANEL_W;
      int ry = (int)((frameNo / speed + prng8(k * 31)) % 66) - 2;
      dma->drawPixel(rx, ry, DIMRAIN);
      if (heavy) dma->drawPixel(rx, ry - 1, DIMRAIN);      // streaks
    }
  } else if (wCode <= 86) {                      // ---- snow ----
    for (int k = 0; k < 14; k++) {
      int ry = (int)((frameNo / 4 + prng8(k * 17)) % PANEL_H);
      int rx = (prng8(k * 23 + 5) + ((frameNo / 10 + k) % 2)) % PANEL_W;
      dma->drawPixel(rx, ry, DIMSNOW);
    }
  } else {                                       // ---- thunderstorm ----
    for (int k = 0; k < 18; k++) {               // driving rain
      int rx = (prng8(k * 13 + 7) + k * 5) % PANEL_W;
      int ry = (int)((frameNo + prng8(k * 31)) % 66) - 2;
      dma->drawPixel(rx, ry, DIMRAIN);
    }
    if (prng8((uint16_t)(frameNo / 5)) % 9 == 0) {  // frequent flashes         // zig-zag bolt
      int bx = 8 + prng8((uint16_t)(frameNo / 5) + 9) % 48;
      for (int y = 0; y < 20; y += 2) {
        dma->drawPixel(bx + ((y / 2) % 2), 6 + y, FLASH);
        dma->drawPixel(bx + ((y / 2) % 2), 7 + y, FLASH);
      }
    }
  }
}

/* Callers must gate this on `frameDue` — the animation step counter
   `frameNo` is advanced centrally by the frame clock in loop(). */
void renderClock() {
  dma->fillScreen(0);      // safe: writing to back buffer, DMA scans front buffer
  drawWeatherBg();
  char buf[20];

  /* --- 12-hour time, visually centered: the block width adapts to a
     1- or 2-digit hour so the rendered content is always centered.
     Colon blinks bright<->dim (never vanishes, so no fake gap). --- */
  int h12 = tmNow.tm_hour % 12;
  if (h12 == 0) h12 = 12;
  bool pm = (tmNow.tm_hour >= 12);
  char hs[4], msb[4];
  snprintf(hs, sizeof(hs), "%d", h12);
  snprintf(msb, sizeof(msb), "%02d", tmNow.tm_min);
  const int COLON_W = 5, MIN_W = 24, AMPM_W = 11;
  int hw   = (h12 >= 10) ? 24 : 12;
  int gap  = (h12 >= 10) ? 0 : 2;                  // 2-digit uses full width
  int xoff = (PANEL_W - (hw + COLON_W + MIN_W + gap + AMPM_W)) / 2;
  if (xoff < 0) xoff = 0;
  dma->setTextWrap(false);
  dma->setTextSize(2);
  dma->setTextColor(C_TIME);
  dma->setCursor(xoff, 2);
  dma->print(hs);
  uint16_t colonC = (tmNow.tm_sec % 2) ? C_TIME : dma->color565(120, 70, 100);
  dma->fillRect(xoff + hw + 1, 5, 2, 2, colonC);
  dma->fillRect(xoff + hw + 1, 12, 2, 2, colonC);
  dma->setCursor(xoff + hw + COLON_W, 2);
  dma->print(msb);
  dma->setTextSize(1);
  dma->setTextColor(C_DATE);
  dma->setCursor(xoff + hw + COLON_W + MIN_W + gap, 10);
  dma->print(pm ? "PM" : "AM");

  /* --- seconds comet: a soft trail grows across beneath the time,
     with a pulsing bright head — a full sweep every minute --- */
  int secs = tmNow.tm_sec;
  if (secs > 0)
    dma->drawFastHLine(2, 17, secs, dma->color565(110, 55, 85));
  dma->drawPixel(2 + secs, 17, ((frameNo / 3) % 2) ? C_ACCENT : C_TIME);

  /* --- rows 19-63: either the living scene + footer, or, while a followed
     match is live, the score card (score_widget.h). The clock block above is
     identical either way — that is the whole point of the sub-mode. --- */
  if (clockSub == SUB_SCORE) {
    drawScoreWidget(frameNo);
  } else {
    if (sceneNeedsReset) {                // came back from the score card:
      sceneNeedsReset = false;            // restart cleanly instead of resuming
      sceneEvent  = SCN_NONE;             // a sprite mid-walk from where it froze
      nextSceneAt = millis() + 3000;
    }
    /* --- living scene (rows 20-44) --- */
    drawAmbient();

    /* --- footer: date/temp block and the potted plant share the same
       top and bottom edges (rows 47-62), so nothing floats in dead space --- */
    static const char *DOW[7] = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
    snprintf(buf, sizeof(buf), "%s %d", DOW[tmNow.tm_wday], tmNow.tm_mday);
    dma->setTextColor(C_DATE);
    dma->setCursor(2, 47);
    dma->print(buf);
    if (!isnan(wTemp)) {
      snprintf(buf, sizeof(buf), "%dC", (int)round(wTemp));
      dma->setTextColor(C_TEMP);
      dma->setCursor(2, 55);
      dma->print(buf);
    }
    drawWeatherIconAnim(24, 54, frameNo); // animated icon, clear of the date row
    drawPottedPlant(41, 59, frameNo);     // wider pot fills the right column edge-to-edge
  }
  panelPresent();                   // present completed back buffer atomically
}

