/* DigiFrame — scrolling text renderer */
#pragma once

/**********************  9. SCROLLING TEXT  ***************************/
/* Speed is expressed in pixels per second and stepped through a 1/256-px
   accumulator, so the text moves at the same rate whatever RENDER_FPS is
   and a dropped frame costs a frame, not a slowdown. (It used to advance
   1 px per 45 ms gate of its own, which both drifted and hard-coded the
   speed to the gate.) */
#define SCROLL_PX_PER_SEC 22
static uint32_t scrollAccum = 0;           // sub-pixel remainder, 1/256 px

bool renderScroll(uint16_t color) {        // returns true if a new frame was drawn
  if (!frameDue) return false;             // paced by the shared frame clock
  dma->fillScreen(0);
  dma->setTextWrap(false);
  dma->setTextSize(2);
  dma->setTextColor(color);
  dma->setCursor(scrollX, 25);
  dma->print(scrollText);
  drawSpark(4, 54, C_ACCENT);
  drawSpark(53, 54, C_ACCENT);
  scrollAccum += (SCROLL_PX_PER_SEC * FRAME_MS * 256UL) / 1000UL;
  scrollX     -= (int)(scrollAccum >> 8);
  scrollAccum &= 0xFF;
  // Measure exact rendered width once per text change using getTextBounds,
  // so the loop point is pixel-perfect regardless of string content.
  static String lastMeasured = "";
  static int    measuredW    = 0;
  if (scrollText != lastMeasured) {
    int16_t x1, y1; uint16_t tw, th;
    dma->getTextBounds(scrollText, 0, 0, &x1, &y1, &tw, &th);
    measuredW    = (int)tw;
    lastMeasured = scrollText;
  }
  if (scrollX < -measuredW) scrollX = PANEL_W;
  return true;
}
