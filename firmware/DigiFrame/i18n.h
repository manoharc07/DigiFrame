/* DigiFrame — UI strings by key (English, German, ...) */
#pragma once
#include "lang_data.h"

/**********************  2b. LOCALISATION  ****************************/
/* Every user-visible string — panel, Telegram, dashboard — is looked up by a
   dotted key: tr("panel.dow.1"). The tables are firmware/lang/<code>.json,
   embedded by firmware/tools/make_lang.ps1 into lang_data.h.

   ALL languages are parsed ONCE at boot into PSRAM and never freed, so a
   table is immutable after setup() and switching language is a one-byte write
   to cfgLang. That is what makes tr() lock-free on both cores: tgTask (core 0)
   formats Telegram replies while the renderer (core 1) draws the date, and
   neither can catch a table half-built. Loading only the active language
   would save a few KB of PSRAM and buy a load/swap race in exchange.

   Lookup falls back active language -> English -> the key itself, so a
   missing key is visible on screen rather than blank.

   Placeholders: tg.* and panel.* values are printf formats (trf()); web.*
   values use {0}, {1} and are filled by the dashboard's own F(). Translations
   must keep the same placeholders in the same order — tests/test_lang.py. */

volatile uint8_t cfgLang = (DEFAULT_LANG < LANG_COUNT) ? DEFAULT_LANG : 0;  // index into LANG_CODES

struct TrEntry { const char *k, *v; };
static TrEntry  *trTab[LANG_COUNT];
static uint16_t  trN[LANG_COUNT];

/* Boot-time parse into PSRAM: internal DRAM is the binding resource on this
   board, and the parse would otherwise briefly need ~2x the table in it. */
struct I18nPsram : ArduinoJson::Allocator {
  void *allocate(size_t n) override { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
  void  deallocate(void *p) override { heap_caps_free(p); }
  void *reallocate(void *p, size_t n) override {
    return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
};

static int trCmp(const void *a, const void *b) {
  return strcmp(((const TrEntry *)a)->k, ((const TrEntry *)b)->k);
}

/* Called first thing in setup(), before anything draws. Uses Serial, not
   logLine(): the log mutex does not exist yet. */
void i18nInit() {
  static I18nPsram alloc;
  for (uint8_t l = 0; l < LANG_COUNT; l++) {
    JsonDocument d(&alloc);
    DeserializationError e = deserializeJson(d, LANG_JSON[l]);
    if (e) { Serial.printf("[i18n] %s: %s\n", LANG_CODES[l], e.c_str()); continue; }
    JsonObject o = d.as<JsonObject>();
    /* one pool for every string of this language: ~220 tiny allocations
       would each carry heap overhead for no benefit */
    size_t n = 0, bytes = 0;
    for (JsonPair kv : o) {
      if (!kv.value().is<const char *>()) continue;
      n++;
      bytes += strlen(kv.key().c_str()) + strlen(kv.value().as<const char *>()) + 2;
    }
    TrEntry *tab  = (TrEntry *)heap_caps_malloc(n * sizeof(TrEntry), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    char    *pool = (char *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!tab || !pool) { heap_caps_free(tab); heap_caps_free(pool); Serial.println("[i18n] no PSRAM"); continue; }
    size_t i = 0;
    for (JsonPair kv : o) {
      if (!kv.value().is<const char *>()) continue;
      const char *k = kv.key().c_str(), *v = kv.value().as<const char *>();
      size_t kl = strlen(k) + 1, vl = strlen(v) + 1;
      memcpy(pool, k, kl); tab[i].k = pool; pool += kl;
      memcpy(pool, v, vl); tab[i].v = pool; pool += vl;
      i++;
    }
    qsort(tab, n, sizeof(TrEntry), trCmp);
    trTab[l] = tab;
    trN[l]   = (uint16_t)n;
  }
}

/* The value of `key` in one specific language, or nullptr. */
const char *trIn(uint8_t lang, const char *key) {
  if (lang >= LANG_COUNT || !trTab[lang]) return nullptr;
  TrEntry probe = { key, nullptr };
  const TrEntry *hit = (const TrEntry *)bsearch(&probe, trTab[lang], trN[lang], sizeof(TrEntry), trCmp);
  return hit ? hit->v : nullptr;
}

/* Active language, then English, then the key itself. Never nullptr. */
const char *tr(const char *key) {
  const char *v = trIn(cfgLang, key);
  if (!v) v = trIn(0, key);
  return v ? v : key;
}

/* tr() + printf. The format comes from our own tables, never from input. */
String trf(const char *key, ...) {
  const char *fmt = tr(key);
  va_list ap, ap2;
  va_start(ap, key);
  va_copy(ap2, ap);
  int len = vsnprintf(nullptr, 0, fmt, ap);
  va_end(ap);
  String out;
  if (len > 0) {
    char *buf = (char *)malloc(len + 1);
    if (buf) { vsnprintf(buf, len + 1, fmt, ap2); out = buf; free(buf); }
  }
  va_end(ap2);
  return out;
}

/* "en" -> 0, "de" -> 1, unknown -> -1 (case-insensitive). */
int langIndex(const String &code) {
  for (uint8_t l = 0; l < LANG_COUNT; l++)
    if (code.equalsIgnoreCase(LANG_CODES[l])) return l;
  return -1;
}

/* "en|de" — for usage hints, so they list whatever languages are compiled in */
String langCodeList() {
  String s;
  for (uint8_t l = 0; l < LANG_COUNT; l++) { if (l) s += "|"; s += LANG_CODES[l]; }
  return s;
}

/* True if `text` equals `key`'s value in ANY language. Telegram reply-keyboard
   buttons arrive as their label text, and a keyboard sent before a language
   switch stays on the user's phone, so a tap must match every language. */
bool trMatchesAny(const String &text, const char *key) {
  for (uint8_t l = 0; l < LANG_COUNT; l++) {
    const char *v = trIn(l, key);
    if (v && text == v) return true;
  }
  return false;
}
