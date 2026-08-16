/* DigiFrame — dev tool: mirror the panel into an RGB565 shadow so the
 * dashboard can hand out a real screenshot of what the LEDs are showing.
 *
 * WHY A TEE AND NOT A READBACK: the HUB75 library stores its framebuffer
 * as interleaved bitplanes with the CIE luminance curve already applied,
 * so reading a pixel back gives you a gamma-mangled, depth-truncated
 * value and requires reimplementing the library's memory layout. Instead
 * we mirror the *input* to the draw calls. Every Adafruit_GFX primitive
 * the sketch uses (print/drawChar/drawLine/drawCircle/drawRect/...)
 * funnels through the five virtuals below, so overriding them captures
 * the frame with no change at any of the several hundred draw sites.
 *
 * COST WHEN OFF: one predicted branch per primitive, and nothing else —
 * the shadow buffers are not even allocated until capture is armed.
 *
 * THREADING: none needed. The WebServer handler that reads the shadow
 * runs inside loop() on core 1, which is also the only thing that draws.
 * A request can therefore only ever be serviced *between* frames, never
 * during one, so a captured frame is always whole. Do not call the
 * capture API from a core-0 task — that invariant is the whole design.
 */
#pragma once

#define CAPTURE_PIXELS   (PANEL_W * PANEL_H)
#define CAPTURE_BYTES    (CAPTURE_PIXELS * 2)
#define CAPTURE_IDLE_MS  30000UL   // auto-disarm this long after the last read

class CapturePanel : public MatrixPanel_I2S_DMA {
public:
  using MatrixPanel_I2S_DMA::MatrixPanel_I2S_DMA;

  bool     armed      = false;
  uint16_t *shadowBack  = nullptr;  // mirrors the buffer currently being drawn
  uint16_t *shadowFront = nullptr;  // mirrors the buffer the panel is showing
  uint32_t frameCount   = 0;        // presented frames since arming
  uint32_t lastReadAt   = 0;        // for the idle auto-disarm

  /* Both shadows live in PSRAM: internal DRAM is the binding resource on
     this board (see config.h) and there is no reason to spend 16 KB of it
     on a debug aid. The draw path only ever writes 8 KB/frame here, which
     PSRAM absorbs without trouble. */
  bool arm() {
    lastReadAt = millis();
    if (armed) return true;
    if (!shadowBack)
      shadowBack = (uint16_t *)heap_caps_malloc(CAPTURE_BYTES, MALLOC_CAP_SPIRAM);
    if (!shadowFront)
      shadowFront = (uint16_t *)heap_caps_malloc(CAPTURE_BYTES, MALLOC_CAP_SPIRAM);
    if (!shadowBack || !shadowFront) { disarm(); return false; }
    memset(shadowBack,  0, CAPTURE_BYTES);
    memset(shadowFront, 0, CAPTURE_BYTES);
    frameCount = 0;
    armed      = true;
    return true;
  }

  void disarm() {
    armed = false;
    if (shadowBack)  { heap_caps_free(shadowBack);  shadowBack  = nullptr; }
    if (shadowFront) { heap_caps_free(shadowFront); shadowFront = nullptr; }
    frameCount = 0;
  }

  /* Called in lockstep with flipDMABuffer() (see panelPresent() in
     globals.h). A copy rather than a pointer swap: renderers always
     repaint the whole frame, but a partial-redraw path would leave a
     swapped shadow holding a two-frames-stale image, and a screenshot
     that is subtly wrong is worse than one that costs an 8 KB memcpy. */
  void present() {
    if (!armed) return;
    memcpy(shadowFront, shadowBack, CAPTURE_BYTES);
    frameCount++;
    if (millis() - lastReadAt > CAPTURE_IDLE_MS) disarm();   // nobody watching
  }

  /* --- the tee. Each override does the real draw, then mirrors it. --- */

  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    MatrixPanel_I2S_DMA::drawPixel(x, y, color);
    if (armed) shadowPixel(x, y, color);
  }

  void fillScreen(uint16_t color) override {
    MatrixPanel_I2S_DMA::fillScreen(color);
    if (!armed) return;
    if (color == 0) { memset(shadowBack, 0, CAPTURE_BYTES); return; }
    for (int i = 0; i < CAPTURE_PIXELS; i++) shadowBack[i] = color;
  }

  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override {
    MatrixPanel_I2S_DMA::fillRect(x, y, w, h, color);
    if (!armed) return;
    for (int16_t j = 0; j < h; j++)
      for (int16_t i = 0; i < w; i++) shadowPixel(x + i, y + j, color);
  }

  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override {
    MatrixPanel_I2S_DMA::drawFastHLine(x, y, w, color);
    if (!armed) return;
    for (int16_t i = 0; i < w; i++) shadowPixel(x + i, y, color);
  }

  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override {
    MatrixPanel_I2S_DMA::drawFastVLine(x, y, h, color);
    if (!armed) return;
    for (int16_t j = 0; j < h; j++) shadowPixel(x, y + j, color);
  }

private:
  /* Clipped write in raw panel coordinates. The sketch never calls
     setRotation(), so GFX coordinates are panel coordinates; if rotation
     is ever used, this needs the same transform() the base class applies. */
  inline void shadowPixel(int16_t x, int16_t y, uint16_t color) {
    if (x < 0 || y < 0 || x >= PANEL_W || y >= PANEL_H) return;
    shadowBack[y * PANEL_W + x] = color;
  }
};
