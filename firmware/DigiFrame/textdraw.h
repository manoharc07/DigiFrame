/* DigiFrame — UTF-8 text on the panel: accented letters + emoji */
#pragma once

/**********************  2c. UTF-8 TEXT RENDERER  *********************/
/* The only font on the panel is the built-in 5x7 GFX font, which is CP437:
   a byte per glyph, and it already contains Ä Ö Ü ä ö ü ß é ç ñ °... UTF-8
   spells each of those as two bytes, so print() drew two garbage glyphs.
   drawText() decodes UTF-8 and lays each codepoint down as:

     ASCII              -> the font glyph, as before
     Latin letters      -> the CP437 glyph for that letter (UTF8_TO_CP437)
     a known emoji      -> an 8x8 bitmap (EMOJIS), scaled with the text size
     FE0F / ZWJ / skin  -> nothing: modifiers that only refine a neighbour
     anything else      -> '?', so an unsupported character is visibly missing
                           rather than silently swallowed

   Every glyph goes through drawChar() and every emoji pixel through
   fillRect(), so dev/panelshot.py captures both (see capture.h).

   REQUIRES dma->cp437(true), set once in setup(). Without it Adafruit GFX
   shifts every glyph from 176 up by one for backward compatibility, so ß
   (0xE1) and ° (0xF8) came out as the neighbouring glyph.

   Single line only: '\n' is laid out as a space. */

/* ---- accented letters: Unicode codepoint -> CP437 glyph ---------------- */
struct Cp437Map { uint16_t cp; uint8_t glyph; };
static const Cp437Map UTF8_TO_CP437[] = {
  {0x00C4, 0x8E}, {0x00D6, 0x99}, {0x00DC, 0x9A},   // Ä Ö Ü
  {0x00E4, 0x84}, {0x00F6, 0x94}, {0x00FC, 0x81},   // ä ö ü
  {0x00DF, 0xE1},                                   // ß
  {0x00C5, 0x8F}, {0x00E5, 0x86}, {0x00C6, 0x92}, {0x00E6, 0x91},  // Å å Æ æ
  {0x00C7, 0x80}, {0x00E7, 0x87}, {0x00C9, 0x90},   // Ç ç É
  {0x00D1, 0xA5}, {0x00F1, 0xA4},                   // Ñ ñ
  {0x00E0, 0x85}, {0x00E1, 0xA0}, {0x00E2, 0x83},   // à á â
  {0x00E8, 0x8A}, {0x00E9, 0x82}, {0x00EA, 0x88}, {0x00EB, 0x89},  // è é ê ë
  {0x00EC, 0x8D}, {0x00ED, 0xA1}, {0x00EE, 0x8C}, {0x00EF, 0x8B},  // ì í î ï
  {0x00F2, 0x95}, {0x00F3, 0xA2}, {0x00F4, 0x93},   // ò ó ô
  {0x00F9, 0x97}, {0x00FA, 0xA3}, {0x00FB, 0x96},   // ù ú û
  {0x00FF, 0x98},                                   // ÿ
  {0x00A1, 0xAD}, {0x00BF, 0xA8}, {0x00A3, 0x9C},   // ¡ ¿ £
  {0x00B0, 0xF8},                                   // °
};

/* ---- emoji: 8x8, '.' = transparent, '1'-'4' = pal[0..3] ----------------
   Drawn for this panel rather than downsampled: at 8x8 a real emoji is a
   blob, so each one is reduced to the one shape that says what it is.
   Nothing is pure black — a black pixel on an LED matrix is just off. */
struct Emoji8 { uint32_t cp; uint16_t pal[4]; const char *px; };

#define EM_YEL   RGB565(255, 205,  60)
#define EM_ORG   RGB565(255, 140,  40)
#define EM_RED   RGB565(235,  40,  60)
#define EM_PINK  RGB565(255, 140, 175)
#define EM_WHT   RGB565(235, 240, 250)
#define EM_GRY   RGB565(150, 160, 180)
#define EM_BRN   RGB565(170, 100,  60)
#define EM_BLU   RGB565( 90, 175, 255)
#define EM_GRN   RGB565(100, 220, 110)
#define EM_DARK  RGB565( 90,  55,  20)

static const Emoji8 EMOJIS[] = {
  {0x2764, {EM_RED, EM_PINK, 0, 0},                      // ❤ heart
    ".11..11."
    "12111111"
    "11111111"
    "11111111"
    ".111111."
    "..1111.."
    "...11..."
    "........"},
  {0x2665, {EM_RED, EM_PINK, 0, 0},                      // ♥ heart suit
    ".11..11."
    "12111111"
    "11111111"
    "11111111"
    ".111111."
    "..1111.."
    "...11..."
    "........"},
  {0x2600, {EM_YEL, EM_ORG, 0, 0},                       // ☀ sun
    "1..1..1."
    ".1.1.1.."
    "..222..."
    "1122211."
    "..222..."
    ".1.1.1.."
    "1..1..1."
    "........"},
  {0x2601, {EM_WHT, EM_GRY, 0, 0},                       // ☁ cloud
    "........"
    "...11..."
    "..11111."
    ".1111111"
    "11111111"
    ".222222."
    "........"
    "........"},
  {0x2B50, {EM_YEL, 0, 0, 0},                            // ⭐ star
    "...11..."
    "...11..."
    "11111111"
    ".111111."
    "..1111.."
    ".11..11."
    "11....11"
    "........"},
  {0x1F319, {RGB565(240, 230, 150), 0, 0, 0},           // 🌙 crescent moon
    "...111.."
    "..11...."
    ".11....."
    ".11....."
    ".11....."
    "..11...."
    "...111.."
    "........"},
  {0x1F382, {EM_YEL, EM_WHT, EM_PINK, EM_BRN},          // 🎂 birthday cake
    "...1...."
    "...2...."
    "...2...."
    ".333333."
    ".444444."
    ".333333."
    ".444444."
    "........"},
  {0x1F389, {EM_YEL, EM_RED, EM_BLU, EM_GRN},           // 🎉 party popper
    "..2..3.."
    "4.....2."
    "...3...."
    "....11.4"
    "...111.."
    "..111..."
    ".111...."
    "11......"},
  {0x1F600, {EM_YEL, EM_DARK, EM_WHT, 0},               // 😀 grinning face
    "..1111.."
    ".111111."
    "11211211"
    "11111111"
    "12333321"
    ".122221."
    "..1111.."
    "........"},
  {0x1F602, {EM_YEL, EM_DARK, EM_WHT, EM_BLU},          // 😂 tears of joy
    "..1111.."
    ".111111."
    "12211221"
    "41111114"
    "41233214"
    ".122221."
    "..1111.."
    "........"},
  {0x1F44D, {RGB565(255, 200, 120), RGB565(200, 130, 60), 0, 0},  // 👍 thumbs up
    "...11..."
    "...11..."
    "..11...."
    "21111111"
    "21111111"
    "21111111"
    "2111111."
    "........"},
  {0x26A0, {EM_YEL, 0, 0, 0},                            // ⚠ warning
    "...11..."
    "...11..."
    "..1..1.."
    "..1..1.."
    ".111111."
    ".11..11."
    "11111111"
    "........"},
  {0x26BD, {EM_WHT, RGB565(90, 90, 100), 0, 0},         // ⚽ football
    "..1111.."
    ".112211."
    "11122111"
    "12111121"
    "11111111"
    ".211112."
    "..1111.."
    "........"},
  {0x2714, {EM_GRN, 0, 0, 0},                            // ✔ check mark
    ".......1"
    "......11"
    ".....11."
    "1...11.."
    "11.11..."
    ".111...."
    "..1....."
    "........"},
  {0x1F525, {RGB565(255, 90, 30), EM_YEL, 0, 0},        // 🔥 fire
    "...1...."
    "..11..1."
    ".111.11."
    "11121111"
    "11222111"
    "11222211"
    ".122221."
    "........"},
};
static const uint8_t EMOJI_COUNT = sizeof(EMOJIS) / sizeof(EMOJIS[0]);

static const Emoji8 *emojiFor(uint32_t cp) {
  for (uint8_t i = 0; i < EMOJI_COUNT; i++)
    if (EMOJIS[i].cp == cp) return &EMOJIS[i];
  return nullptr;
}

static uint8_t cp437For(uint32_t cp) {
  for (const Cp437Map &m : UTF8_TO_CP437)
    if (m.cp == cp) return m.glyph;
  return 0;
}

/* Modifiers that refine the character before them and draw nothing:
   variation selectors (❤️ is 2764 + FE0F), zero-width joiner, skin tones. */
static bool utf8IsSilent(uint32_t cp) {
  return cp == 0xFE0E || cp == 0xFE0F || cp == 0x200D || (cp >= 0x1F3FB && cp <= 0x1F3FF);
}

/* Decode one codepoint at s[i] and advance i. A malformed sequence yields
   0xFFFD and consumes one byte, so a bad byte can never stall the loop. */
static uint32_t utf8Next(const char *s, size_t len, size_t &i) {
  uint8_t c = (uint8_t)s[i];
  int n; uint32_t cp;
  if      (c < 0x80)           { i++; return c; }
  else if ((c & 0xE0) == 0xC0) { n = 1; cp = c & 0x1F; }
  else if ((c & 0xF0) == 0xE0) { n = 2; cp = c & 0x0F; }
  else if ((c & 0xF8) == 0xF0) { n = 3; cp = c & 0x07; }
  else                         { i++; return 0xFFFD; }
  for (int k = 1; k <= n; k++) {
    if (i + k >= len || ((uint8_t)s[i + k] & 0xC0) != 0x80) { i++; return 0xFFFD; }
    cp = (cp << 6) | ((uint8_t)s[i + k] & 0x3F);
  }
  i += n + 1;
  return cp;
}

/* Pixels one codepoint advances the pen by, at text size `size`. A font
   glyph is a 6px cell (5 + 1 gap); an emoji is 8px + 1 gap. */
static int textAdvance(uint32_t cp, uint8_t size) {
  if (cp == '\n') cp = ' ';
  if (utf8IsSilent(cp)) return 0;
  if (cp >= 0x80 && emojiFor(cp)) return 9 * size;
  return 6 * size;
}

/* Rendered width of `s` in pixels at text size `size`. */
int textWidth(const char *s, uint8_t size) {
  size_t len = strlen(s), i = 0;
  int w = 0;
  while (i < len) w += textAdvance(utf8Next(s, len, i), size);
  return w;
}
int textWidth(const String &s, uint8_t size) { return textWidth(s.c_str(), size); }

static void drawEmoji(int x, int y, const Emoji8 &e, uint8_t size) {
  for (int r = 0; r < 8; r++)
    for (int c = 0; c < 8; c++) {
      char p = e.px[r * 8 + c];
      if (p < '1' || p > '4') continue;
      dma->fillRect(x + c * size, y + r * size, size, size, e.pal[p - '1']);
    }
}

/* Draw `s` with its top-left at (x, y); returns the x after the last glyph.
   Transparent background, like print() with a single-argument text colour. */
int drawText(int x, int y, const char *s, uint8_t size, uint16_t color) {
  size_t len = strlen(s), i = 0;
  while (i < len) {
    uint32_t cp = utf8Next(s, len, i);
    if (cp == '\n') cp = ' ';
    if (utf8IsSilent(cp)) continue;
    if (cp < 0x80) {
      dma->drawChar(x, y, (unsigned char)cp, color, color, size);
      x += 6 * size;
      continue;
    }
    if (const Emoji8 *e = emojiFor(cp)) {
      /* off-screen emoji are skipped whole: a long scrolling line can hold
         dozens, and each is up to 64 fillRects */
      if (x < PANEL_W && x + 8 * size > 0) drawEmoji(x, y, *e, size);
      x += 9 * size;
      continue;
    }
    uint8_t g = cp437For(cp);
    dma->drawChar(x, y, g ? g : '?', color, color, size);
    x += 6 * size;
  }
  return x;
}
int drawText(int x, int y, const String &s, uint8_t size, uint16_t color) {
  return drawText(x, y, s.c_str(), size, color);
}

/* Upper-case for the scroller, which shows everything in capitals. ASCII as
   toupper() does; a lower-case letter becomes its capital ONLY where CP437
   has a glyph for that capital (Ä Ö Ü Å Æ Ç É Ñ) — "è" stays "è" rather than
   turning into an "È" the font cannot draw. ß has no capital and stays. */
String utf8ToUpper(const String &in) {
  static const uint16_t PAIRS[][2] = {
    {0x00E4, 0x00C4}, {0x00F6, 0x00D6}, {0x00FC, 0x00DC}, {0x00E5, 0x00C5},
    {0x00E6, 0x00C6}, {0x00E7, 0x00C7}, {0x00E9, 0x00C9}, {0x00F1, 0x00D1},
  };
  String out;
  out.reserve(in.length());
  const char *s = in.c_str();
  size_t len = in.length(), i = 0;
  while (i < len) {
    size_t start = i;
    uint32_t cp = utf8Next(s, len, i);
    if (cp < 0x80) { out += (char)toupper((int)cp); continue; }
    bool done = false;
    for (auto &p : PAIRS)
      if (cp == p[0]) {                       // all targets are 2-byte U+00xx
        out += (char)(0xC0 | (p[1] >> 6));
        out += (char)(0x80 | (p[1] & 0x3F));
        done = true;
        break;
      }
    if (!done) for (size_t k = start; k < i; k++) out += s[k];  // keep as-is
  }
  return out;
}
