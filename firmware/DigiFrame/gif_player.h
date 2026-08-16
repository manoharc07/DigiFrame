/* DigiFrame — GIF decode callbacks + open/close + character pack */
#pragma once
#include "default_gifs.h"   // embedded default pack (auto-generated)

/**********************  5. GIF CALLBACKS  ****************************/
void *GIFOpenFile(const char *fname, int32_t *pSize) {
  fsGifFile = LittleFS.open(fname);
  if (fsGifFile) { *pSize = fsGifFile.size(); return (void *)&fsGifFile; }
  return NULL;
}
void GIFCloseFile(void *pHandle) {
  File *f = static_cast<File *>(pHandle);
  if (f) f->close();
}
int32_t GIFReadFile(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen) {
  File *f = static_cast<File *>(pFile->fHandle);
  int32_t n = iLen;
  // Clamp to the bytes remaining. (The AnimatedGIF SD example subtracts an
  // extra byte here to dodge an SdFat seek() quirk — LittleFS has no such
  // quirk, and withholding the final byte truncates the last frame, which
  // breaks playback of many real-world GIFs.)
  if ((pFile->iSize - pFile->iPos) < iLen) n = pFile->iSize - pFile->iPos;
  if (n <= 0) return 0;
  n = (int32_t)f->read(pBuf, n);
  pFile->iPos = f->position();
  return n;
}
int32_t GIFSeekFile(GIFFILE *pFile, int32_t iPosition) {
  File *f = static_cast<File *>(pFile->fHandle);
  f->seek(iPosition);
  pFile->iPos = (int32_t)f->position();
  return pFile->iPos;
}
/* GIF display geometry — computed once in openGif, used when blitting */
static int   gifOffX = 0, gifOffY = 0;       // top-left draw offset on panel
static float gifScaleX = 1.0f, gifScaleY = 1.0f;  // > 1 means downscale
static bool  gifNeedsScale = false;

/* Persistent panel-space canvas (RGB565). AnimatedGIF renders frames
 * incrementally — each frame only touches the pixels that changed and
 * relies on the previous frame still being present. The HUB75 panel is
 * double-buffered, so decoding straight to the panel lands each new frame
 * on a buffer that is two frames stale (ghosting/tearing). Instead we
 * composite into this stable canvas — already centered and, for oversized
 * GIFs, downscaled into panel coordinates — then blit the whole thing to
 * the back buffer every frame so both DMA buffers hold a complete image.
 * A fixed panel-sized buffer (8 KB) is used so there is no per-GIF heap
 * allocation to fail (PSRAM may be unavailable / too fragmented). */
static uint16_t gifCanvas[PANEL_W * PANEL_H];

/* Decode one GIF scan line straight into the panel-space canvas, applying
 * the centering offset and (if needed) downscale as we go. Transparent
 * pixels keep whatever the previous frame left there — that's how
 * incremental GIF frames composite; disposal method 2 clears to background. */
void GIFDraw(GIFDRAW *pDraw) {
  uint16_t *pal = pDraw->pPalette;
  int srcY = pDraw->iY + pDraw->y;                // source (GIF canvas) row
  int w    = pDraw->iWidth;
  uint8_t *s = pDraw->pPixels;                    // s[x] maps to source x = iX + x

  if (pDraw->ucDisposalMethod == 2) {             // restore-to-background
    for (int x = 0; x < w; x++)
      if (s[x] == pDraw->ucTransparent) s[x] = pDraw->ucBackground;
    pDraw->ucHasTransparency = 0;
  }
  bool    hasT = pDraw->ucHasTransparency;
  uint8_t tr   = pDraw->ucTransparent;

  int py = gifNeedsScale ? gifOffY + (int)(srcY / gifScaleY)
                         : gifOffY + srcY;
  if (py < 0 || py >= PANEL_H) return;
  uint16_t *dst = gifCanvas + py * PANEL_W;

  for (int x = 0; x < w; x++) {
    uint8_t c = s[x];
    if (hasT && c == tr) continue;                // leave prior pixel in place
    int srcX = pDraw->iX + x;
    int px = gifNeedsScale ? gifOffX + (int)(srcX / gifScaleX)
                           : gifOffX + srcX;
    if (px >= 0 && px < PANEL_W) dst[px] = pal[c];
  }
}

/* Push the composited canvas onto the panel back buffer. Called once per
 * frame after playFrame() and before flipDMABuffer(). */
void blitGifCanvas() {
  for (int y = 0; y < PANEL_H; y++) {
    uint16_t *row = gifCanvas + y * PANEL_W;
    for (int x = 0; x < PANEL_W; x++) dma->drawPixel(x, y, row[x]);
  }
}

/* Decode + composite the next GIF frame, but only once its own inter-frame
 * delay has elapsed. Deliberately non-blocking: playFrame(bSync=true)
 * delay()s *inside* the decoder for the remainder of the frame interval
 * (typically 60-100 ms), and on core 1 that stalls the dashboard, the
 * once-a-second clock tick, the captive-portal DNS and the cross-core
 * command queue for the whole time. Pacing on our own deadline instead
 * costs nothing and keeps loop() responsive between frames.
 *
 * Returns true if a new frame was composited (the caller then flips).
 * If pRes is given it receives playFrame()'s return value: 0 means that
 * was the last frame, so the caller can loop or close. */
bool playGifFrameIfDue(int *pRes = nullptr) {
  if (!gifOpen) return false;
  uint32_t ms = millis();
  if ((int32_t)(ms - gifNextFrameAt) < 0) return false;

  int delayMs = 0;
  int res = gif.playFrame(false, &delayMs);
  if (pRes) *pRes = res;
  blitGifCanvas();
  // Plenty of GIFs declare a 0 ms delay; without a floor we would blit the
  // full 64x64 canvas as fast as the CPU allows and starve everything else.
  if (delayMs < GIF_MIN_FRAME_MS) delayMs = GIF_MIN_FRAME_MS;
  gifNextFrameAt = ms + (uint32_t)delayMs;
  return true;
}

bool openGif(const String &path, bool userPlay = false) {
  if (gifOpen) { gif.close(); gifOpen = false; }
  if (!LittleFS.exists(path)) return false;
  if (gif.open(path.c_str(), GIFOpenFile, GIFCloseFile,
               GIFReadFile, GIFSeekFile, GIFDraw)) {
    gifOpen = true;
    gifIsUserPlay = userPlay;
    currentGifPath = path;

    // Fresh panel-space canvas for this GIF
    memset(gifCanvas, 0, sizeof(gifCanvas));

    // Compute centering + scale-to-fit geometry
    int cw = gif.getCanvasWidth();
    int ch = gif.getCanvasHeight();
    if (cw <= 0) cw = PANEL_W;
    if (ch <= 0) ch = PANEL_H;

    if (cw <= PANEL_W && ch <= PANEL_H) {
      // Fits as-is — just center it
      gifOffX       = (PANEL_W - cw) / 2;
      gifOffY       = (PANEL_H - ch) / 2;
      gifScaleX     = 1.0f;
      gifScaleY     = 1.0f;
      gifNeedsScale = false;
    } else {
      // Scale down to fit while preserving aspect ratio
      float sx = (float)cw / PANEL_W;
      float sy = (float)ch / PANEL_H;
      float scale = (sx > sy) ? sx : sy;   // uniform scale — use the larger ratio
      gifScaleX     = scale;
      gifScaleY     = scale;
      int dw = (int)(cw / scale);
      int dh = (int)(ch / scale);
      gifOffX       = (PANEL_W - dw) / 2;
      gifOffY       = (PANEL_H - dh) / 2;
      gifNeedsScale = true;
    }

    logLine("GIF " + String(cw) + "x" + String(ch) +
            (gifNeedsScale ? " -> scaled, " : " -> centered, ") +
            "off=" + String(gifOffX) + "," + String(gifOffY));

    // Clear both buffers so stale content doesn't bleed through transparent
    // pixels. On the S3 flipDMABuffer() only re-points the descriptor chain
    // and returns immediately — the DMA keeps scanning the buffer we just
    // handed away until it reaches the end of the current pass — so give it
    // a scan period before overwriting the other one.
    dma->fillScreen(0); panelPresent();
    delay(panelScanPeriodMs());
    dma->fillScreen(0); panelPresent();
    gifNextFrameAt = millis();      // first frame plays immediately
    return true;
  }
  // open failed — surface the decoder's reason (e.g. GIF_TOO_WIDE for
  // oversized GIFs) instead of silently staying on the clock.
  logLine("GIF open FAILED: " + path + " err=" + String(gif.getLastError()));
  return false;
}
void closeGif() {
  if (gifOpen) { gif.close(); gifOpen = false; }
}

/* One-time seed of the embedded default GIF pack onto LittleFS. The
 * marker file makes this run exactly once per filesystem, so GIFs the
 * user deletes from the dashboard stay deleted across reboots. */
void seedDefaultGifs() {
  if (LittleFS.exists("/.gifs_seeded")) return;
  int n = 0;
  for (size_t i = 0; i < DEFAULT_GIF_COUNT; i++) {
    const DefaultGif &g = DEFAULT_GIFS[i];
    if (LittleFS.exists(g.path)) continue;      // don't clobber an upload
    File f = LittleFS.open(g.path, "w");
    if (!f) { logLine("seed FAILED: " + String(g.path)); continue; }
    f.write(g.data, g.len);
    f.close();
    n++;
  }
  File m = LittleFS.open("/.gifs_seeded", "w");
  if (m) { m.print("1"); m.close(); }
  logLine("default GIF pack: seeded " + String(n) + " gifs");
}

/* Character pack: any GIF named c_*.gif belongs to the random cameo
 * pool (upload via the dashboard with "character pack" checked). */
String pickRandomChar() {
  String names[16];
  int n = 0;
  File root = LittleFS.open("/");
  File f = root.openNextFile();
  while (f && n < 16) {
    String nm = f.name();
    if (nm.startsWith("c_") && nm.endsWith(".gif")) names[n++] = "/" + nm;
    f = root.openNextFile();
  }
  if (n == 0 && LittleFS.exists("/idle.gif")) return "/idle.gif";
  if (n == 0) return "";
  return names[esp_random() % n];
}
