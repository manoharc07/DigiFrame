/* DigiFrame — celebration (special-day) mode + test mode */
#pragma once

/**********************  11. CELEBRATION MODE  ***********************/
/* A special day fires this: it runs the themed visual for its TYPE for a
 * while, then a scrolling MESSAGE banner with fireworks, and loops. Merges
 * the old "party mode" with the special-days engine.
 *   type "birthday" -> cake.gif (or a procedural cake) + confetti
 *   type "custom"   -> fireworks + confetti  */

/* procedural fireworks + confetti scene (the "custom" celebration visual) */
void drawCelebrationScene(uint32_t f) {
  uint16_t confs[4] = { dma->color565(255, 120, 160), dma->color565(140, 200, 255),
                        dma->color565(255, 220, 120), dma->color565(170, 255, 170) };
  for (int k = 0; k < 10; k++) {                    // confetti drifting down
    int rx = (prng8(k * 29 + 3) + k * 5) % PANEL_W;
    int ry = (int)((f / 2 + prng8(k * 13)) % PANEL_H);
    dma->drawPixel(rx, ry, confs[k % 4]);
  }
  drawFirework(14, 22, f,      dma->color565(255, 120, 160), dma->color565(255, 220, 120));
  drawFirework(50, 18, f + 27, dma->color565(140, 200, 255), dma->color565(190, 150, 255));
  drawFirework(32, 32, f + 53, dma->color565(170, 255, 170), dma->color565(255, 200, 120));
}

void startCelebration(const String &type, const String &message) {
  mode         = MODE_CELEBRATE;
  celebType    = type.length() ? type : "custom";
  celebMsg     = message;
  if (!celebMsg.length())                           // default banner per type
    celebMsg = (celebType == "birthday") ? "HAPPY BIRTHDAY!" : "CELEBRATE!";
  celebMsg.toUpperCase();
  celebPhase   = 0;
  celebPhaseAt = millis();
  dma->setBrightness8(CELEBRATE_BRIGHTNESS);
  bool gifVisual = (celebType == "birthday") && openGif("/cake.gif", true);
  logLine("Celebration (" + celebType + "): \"" + celebMsg + "\"" +
          (celebType == "birthday" ? String(" cake.gif=") + (gifVisual ? "yes" : "no") : ""));
}

/* Phase timers below run every pass; only the drawing is paced (by the
   shared frame clock, or by the GIF's own delay). */
void runCelebration() {
  // alternate: ~45 s of the themed visual, then ~20 s of the message banner
  if (celebPhase == 0) {
    if (gifOpen) {
      int res = 1;
      if (playGifFrameIfDue(&res)) {
        panelPresent();
        if (res == 0) gif.reset();
      }
    } else if (frameDue) {
      dma->fillScreen(0);
      if (celebType == "birthday") drawCakeAndCat(frameNo);
      else                          drawCelebrationScene(frameNo);
      panelPresent();
    }
    if (millis() - celebPhaseAt > 45000) {
      closeGif();
      celebPhase = 1;
      celebPhaseAt = millis();
      scrollText = celebMsg;
      scrollX = PANEL_W;
      logLine("celebration -> banner phase");
    }
  } else {
    if (renderScroll(C_MSG)) {
      // fireworks drawn on the same back buffer, then flip once
      drawFirework(12, 10, frameNo,      dma->color565(255, 120, 160), dma->color565(255, 220, 120));
      drawFirework(50, 52, frameNo + 40, dma->color565(140, 200, 255), dma->color565(190, 150, 255));
      panelPresent();
    }
    if (millis() - celebPhaseAt > 20000) {
      celebPhase = 0;
      celebPhaseAt = millis();
      if (celebType == "birthday" && LittleFS.exists("/cake.gif")) openGif("/cake.gif", true);
    }
  }
}

/**********************  11b. TEST MODE  ******************************/
/* /test cycles through every visual screen for ~4 s each:
 *   0  Clock – clear/sunny day
 *   1  Clock – rain
 *   2  Clock – snow
 *   3  Clock – fog
 *   4  Clock – thunderstorm
 *   5  Clock – cloudy
 *   6  Clock – clear night (stars + moon)
 *   7  Scroll message (/msg style)
 *   8  Celebration – procedural cake theme (no GIF needed)
 *   9  Celebration – scroll banner + fireworks
 *  10  GIF  – cake.gif if present, else skip
 *  11+ Live-score card – one step per registered sport, each firing that
 *      sport's signature animation, so /test walks every layout and effect
 *  (restores original weather code and returns to clock when done) */
#define TEST_SCORE_FIRST 11
#define TEST_STEPS       (TEST_SCORE_FIRST + NUM_SPORTS)
#define TEST_STEP_MS   4000   // ms per step

void startTest(const String &chatId) {
  testChat       = chatId;
  testStep       = 0;
  testStepAt     = millis();
  testSavedWCode = wCode;
  mode           = MODE_TEST;
  logLine("Test mode start");
}

void runTest() {
  uint32_t ms = millis();

  // Advance step every TEST_STEP_MS
  if (ms - testStepAt > TEST_STEP_MS) {
    testStep++;
    testStepAt = ms;
    if (testStep >= TEST_STEPS) {
      // Done — restore everything
      wCode = testSavedWCode;
      closeGif();
      clockSub    = SUB_AMBIENT;      // drop the forced score card
      activeMatch = -1;
      numFront    = 0;
      mode = MODE_CLOCK;
      logLine("Test mode done");
      if (testChat.length()) bot.sendMessage(testChat, "✅ Test complete — back to clock.");
      return;
    }
    // entering a score step: build that sport's card and fire its signature
    // animation, so one /test run exercises every layout and every effect
    if (testStep >= TEST_SCORE_FIRST) {
      uint8_t s = testStep - TEST_SCORE_FIRST;
      sportsDemoForce(s);
      const SportModule *mod = SPORTS[s];
      if (mod->numEvents) scoreTestEvent(mod->events[0].native);
    } else if (clockSub == SUB_SCORE) {
      clockSub = SUB_AMBIENT; activeMatch = -1; numFront = 0;
    }
  }

  // Per-step setup (runs once on step change via testStepAt reset above,
  // but we key off testStep directly so rendering is correct every frame)
  switch (testStep) {
    // ---- Clock variants: just spoof wCode + hour ----
    case 0:  wCode = 0;  tmNow.tm_hour = 10; break;   // clear day
    case 1:  wCode = 61; tmNow.tm_hour = 14; break;   // rain
    case 2:  wCode = 73; tmNow.tm_hour = 9;  break;   // snow
    case 3:  wCode = 45; tmNow.tm_hour = 8;  break;   // fog
    case 4:  wCode = 95; tmNow.tm_hour = 16; break;   // thunderstorm
    case 5:  wCode = 2;  tmNow.tm_hour = 12; break;   // cloudy
    case 6:  wCode = 0;  tmNow.tm_hour = 22; break;   // clear night
    default: break;
  }

  if (testStep <= 6) {
    // Clock face with spoofed weather/time
    if (frameDue) renderClock();
    return;
  }

  if (testStep == 7) {
    // Scroll message
    static uint32_t scrollStepAt = 0;
    if (scrollStepAt != testStepAt) {
      scrollStepAt = testStepAt;
      scrollText = "HELLO! THIS IS A TEST MESSAGE";
      scrollX = PANEL_W;
    }
    if (renderScroll(C_MSG)) panelPresent();
    return;
  }

  if (testStep == 8) {
    // Procedural celebration – cake theme
    if (!frameDue) return;
    dma->fillScreen(0);
    drawCakeAndCat(frameNo);
    panelPresent();
    return;
  }

  if (testStep == 9) {
    // Celebration scroll + fireworks
    static uint32_t partyScrollStepAt = 0;
    if (partyScrollStepAt != testStepAt) {
      partyScrollStepAt = testStepAt;
      scrollText = "CELEBRATION TIME!";
      scrollX = PANEL_W;
    }
    if (renderScroll(C_MSG)) {
      drawFirework(12, 10, frameNo,      dma->color565(255, 120, 160), dma->color565(255, 220, 120));
      drawFirework(50, 52, frameNo + 40, dma->color565(140, 200, 255), dma->color565(190, 150, 255));
      panelPresent();
    }
    return;
  }

  if (testStep == 10) {
    // GIF – cake.gif if present
    static uint32_t gifStepAt = 0;
    if (gifStepAt != testStepAt) {
      gifStepAt = testStepAt;
      closeGif();
      if (LittleFS.exists("/cake.gif")) openGif("/cake.gif", true);
      else { testStep++; testStepAt = ms; return; }  // skip if missing
    }
    int res = 1;
    if (playGifFrameIfDue(&res)) {
      panelPresent();
      if (res == 0) gif.reset();
    }
    return;
  }

  if (testStep >= TEST_SCORE_FIRST) {
    // Live-score card for one sport — renderClock() picks it up through
    // clockSub, exactly as it does for a real match
    if (frameDue) renderClock();
    return;
  }
}
