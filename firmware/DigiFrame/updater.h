/* DigiFrame — the OTA install path, shared by the manual upload and the one-click update */
#pragma once

/**********************  11c. FIRMWARE UPDATE  ************************
 * The frame does exactly one thing in this feature: it writes an app image
 * into the spare OTA slot. Everything upstream of that — knowing a release
 * exists, comparing versions, choosing which of its binaries to use — runs
 * in the browser, because api.github.com answers a web page and costs the
 * ESP32 nothing (see the note in config.h).
 *
 * Two front doors reach the code below, and both end in the same three
 * steps — quiesce, validate, write:
 *
 *   handleOtaUpload()  (web_portal.h) a .bin the user picked by hand,
 *                      streamed in by the browser.
 *   updateInstall()    a URL the dashboard handed over after its check. The
 *                      frame does the download itself, because the release
 *                      asset is the one GitHub URL a page cannot read.
 *
 * Both run on core 1 and both block the render loop and the web server for
 * their duration. That is deliberate: flash writes are core 1's alone (see
 * the core-split rules in CLAUDE.md), and there is nothing useful to render
 * while the app underneath you is being replaced.
 *
 * They differ in one large way. The upload path keeps the panel and uses it
 * as a progress bar. The download path CANNOT: a TLS session needs two 16 KB
 * mbedTLS buffers, and with the framebuffer allocated the largest contiguous
 * block on this board is 11 KB. So updateInstall() deletes the panel to get
 * that 64 KB back, which makes the screen dark for the download and makes
 * every exit from that point a reboot. See panelTeardown() for the numbers.
 */

/* ---- the progress screen ----------------------------------------------
   Paints BOTH DMA buffers, because nothing else is rendering while an update
   runs — the frame clock in loop() is not turning, so painting one buffer
   would leave whichever half is on screen showing the frame before it. */
void otaScreen(const String &line) {
  if (!dma) return;                      // panel torn down for the download
  for (int b = 0; b < 2; b++) {          // paint both DMA buffers
    dma->fillScreen(0);
    dma->setTextSize(1);
    dma->setTextColor(C_MSG);
    dma->setCursor(2, 20);
    dma->print("UPDATING");
    dma->setTextColor(C_TEMP);
    dma->setCursor(2, 34);
    dma->print(line);
    panelPresent();
  }
}

/* Nothing else may touch the heap or the flash while Update is writing — and
   for a download, nothing else may be holding internal DRAM either.

   Suspending a task does NOT give its memory back: a task frozen inside an
   mbedTLS handshake still owns its ~32 KB, and with Telegram's session live
   there is not enough left to open a second one. Measured, not guessed: the
   download failed with "HTTP -1" and no heap until this handshake existed.
   So ask first (netQuiesce), let each network task drop its session at the
   top of its own loop, and only suspend once they have parked. Calling
   tgClient.stop() from core 1 instead would free a context out from under a
   task that is about to resume into it.

   The upload path does not need the DRAM, but it costs a few hundred ms and
   keeps one definition of "quiesced". */
void otaQuiesce() {
  uint32_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  netQuiesce = true;
  uint32_t t0 = millis();
  /* Generous, because the ack is only taken at the top of each task's loop:
     weatherTask ticks every 5 s and may be inside a fetch when asked, and
     tgTask every 3 s. Waiting less does not park them faster, it just gives
     up and suspends a task that is still holding its session — which is
     precisely the measurement error that made the first attempt look like
     the sessions were not worth freeing. */
  while ((!tgIdle || !weatherIdle) && millis() - t0 < 15000) delay(20);
  bool parked = tgIdle && weatherIdle;
  if (tgTaskHandle)      vTaskSuspend(tgTaskHandle);
  if (weatherTaskHandle) vTaskSuspend(weatherTaskHandle);
  if (mqttTaskHandle)    vTaskSuspend(mqttTaskHandle);
  if (sportsTaskHandle)  vTaskSuspend(sportsTaskHandle);
  closeGif();
  logLine("update: " + String(parked ? "parked" : "TIMED OUT") + " in " +
          String(millis() - t0) + "ms, free " + String(before / 1024) + "->" +
          String(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024) +
          "KB largest " +
          String(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024) + "KB");
}
void otaResume() {
  if (tgTaskHandle)      vTaskResume(tgTaskHandle);
  if (weatherTaskHandle) vTaskResume(weatherTaskHandle);
  if (mqttTaskHandle)    vTaskResume(mqttTaskHandle);
  if (sportsTaskHandle)  vTaskResume(sportsTaskHandle);
  netQuiesce = false;
}

/* An image carrying an esp_app_desc_t has this magic at offset 0x20. It is
   what tells an app image apart from the bootloader or the merged full-flash
   image, so neither the wrong upload nor the wrong release asset can write a
   slot the board will not boot. */
bool otaLooksLikeApp(const uint8_t *buf, size_t n) {
  if (n < 0x24) return false;
  uint32_t magic = 0;
  memcpy(&magic, buf + 0x20, 4);
  return magic == 0xABCD5432UL;
}

bool     otaBegun     = false;
String   otaError     = "";
uint32_t otaLastShown = 0;

void otaFail(const String &why) {
  otaError = why;
  strlcpy(updErr, why.c_str(), sizeof(updErr));
  Update.abort();
  otaBegun = false;
  otaResume();
  mode = MODE_CLOCK;
  logLine("OTA FAILED: " + why);
}

/* ---- giving the panel's 64 KB back ------------------------------------
   Measured on the device, which is the only reason this exists: with the
   panel up there is ~32 KB of internal DRAM free and the largest contiguous
   block is 11 KB. mbedTLS wants a 16 KB input buffer AND a 16 KB output
   buffer, so the handshake fails with "SSL - Memory allocation failed"
   however the rest of the firmware is arranged. Quiescing the other network
   tasks recovers 2 KB — the Telegram bot connects and closes per poll, so
   between polls there is nothing there to free.

   The framebuffer is the occupant: double-buffered, PANEL_COLOR_DEPTH 8, in
   MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA because the DMA engine cannot read
   PSRAM. The library allocates it per row and frees it in ~rowBitStruct, so
   deleting the panel really does return it.

   This is one-way. stopDMAoutput() is documented as black-until-reboot, so
   anything that goes wrong after this point ends in ESP.restart() rather
   than an attempt to put the panel back — which is also why the failure
   reason is persisted first: the log ring is RAM and does not survive. */
void panelTeardown() {
  if (!dma) return;
  dma->stopDMAoutput();
  delete dma;
  dma = nullptr;
}

/* The log ring does not survive the reboot that a failed download forces, so
   the reason goes to flash and is picked up by loadUpdateErr() at boot. */
void saveUpdateErr(const String &why) {
  File f = LittleFS.open("/update_err.txt", "w");
  if (!f) return;
  f.print(why);
  f.close();
}
void loadUpdateErr() {
  if (!LittleFS.exists("/update_err.txt")) return;
  File f = LittleFS.open("/update_err.txt", "r");
  if (f) {
    strlcpy(updErr, f.readString().c_str(), sizeof(updErr));
    f.close();
    logLine("last update failed: " + String(updErr));
  }
  LittleFS.remove("/update_err.txt");     // reported once, then forgotten
}

/* ---- what the frame will accept as an update URL ----------------------
   The dashboard is unauthenticated and LAN-only, exactly like /api/ota. But
   an uploaded .bin at least had to come from someone on the network, whereas
   a URL is a standing instruction to go and fetch — so it is pinned to the
   releases of the configured repo rather than left open. A wrong-but-honest
   URL still gets caught by otaLooksLikeApp() a moment later; this stops the
   frame being pointed at an arbitrary host in the first place. */
bool updUrlAllowed(const String &u) {
  return u.startsWith("https://github.com/" UPDATE_REPO "/releases/download/");
}

/* ---- the install: pull the asset straight into the OTA slot -----------
   Called from loop() when the dashboard sets updInstallNow. The response to
   /api/update is sent BEFORE this runs, because core 1 is about to stop
   servicing sockets for half a minute — a browser waiting on that reports a
   timeout on an update that is working fine.

   HTTPClient must be told to follow redirects: github.com answers the asset
   URL with a 302 to release-assets.githubusercontent.com. Both hops are
   https, so STRICT is enough and the connection can never be silently
   downgraded on the way.                                                 */
void updateInstall() {
  if (!updUrl[0]) { logLine("update: nothing to install"); return; }
  updBusy      = true;
  otaError     = "";
  otaLastShown = 0;
  logLine("update: installing " + String(updTag) + " (" + String(updSize / 1024) + " KB)");
  otaQuiesce();

  /* Last thing the panel ever shows this boot. Held briefly so it is actually
     readable before the screen goes dark for the download. */
  otaScreen(String(updTag));
  delay(1200);
  panelTeardown();
  logLine("update: panel down, free " +
          String(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024) + "KB largest " +
          String(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024) + "KB");

  /* From here every exit is a reboot — see panelTeardown(). */
  String fail = "";
  uint32_t done = 0;

  /* The download buffer goes in PSRAM: the internal DRAM just freed is for
     mbedTLS, and handing any of it back would defeat the teardown. */
  uint8_t *buf = (uint8_t *)heap_caps_malloc(UPDATE_CHUNK, MALLOC_CAP_SPIRAM);
  if (!buf) buf = (uint8_t *)malloc(UPDATE_CHUNK);
  if (!buf) fail = "no memory for download";

  if (!fail.length()) {
    WiFiClientSecure c;
    c.setInsecure();             // same trade as fetchWeather(): the payload is
    c.setTimeout(20);            // public, and validated by its own app header
    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(20000);
    if (!http.begin(c, String(updUrl))) {
      fail = "download: connect failed";
    } else {
      int code = http.GET();
      if (code != 200) {
        /* HTTPClient collapses every TLS failure into -1, which is
           unactionable. Ask the client what actually went wrong and record
           the DRAM numbers beside it — on this board the answer has always
           been the largest contiguous block, not the total. */
        char tls[96] = "";
        if (code < 0) c.lastError(tls, sizeof(tls));
        fail = "download: HTTP " + String(code) +
               (tls[0] ? (" (" + String(tls) + ")") : String("")) +
               " largest " +
               String(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024) + "KB";
      } else {
        int         total    = http.getSize();   // -1 if the server will not say
        WiFiClient *st       = http.getStreamPtr();
        uint32_t    lastData = millis();
        bool        started  = false;

        while (http.connected() && (total < 0 || done < (uint32_t)total)) {
          size_t avail = st->available();
          if (!avail) {
            if (millis() - lastData > 20000) { fail = "download stalled"; break; }
            vTaskDelay(1);
            continue;
          }
          int n = st->readBytes(buf, avail > UPDATE_CHUNK ? UPDATE_CHUNK : avail);
          if (n <= 0) { vTaskDelay(1); continue; }
          lastData = millis();

          if (!started) {                        // first bytes decide if this is an app
            if (!otaLooksLikeApp(buf, n)) { fail = "not an app image"; break; }
            if (!Update.begin(total > 0 ? (size_t)total : UPDATE_SIZE_UNKNOWN)) {
              fail = Update.errorString(); break;
            }
            otaBegun = started = true;
          }
          if (Update.write(buf, n) != (size_t)n) { fail = Update.errorString(); break; }
          done += n;
          if (done - otaLastShown > 262144) {    // no panel now, so the log is the progress bar
            otaLastShown = done;
            logLine("update: " + String(done / 1024) + " KB");
          }
        }
        if (!fail.length() && !started) fail = "download: empty response";
      }
      http.end();
    }
  }
  free(buf);

  if (!fail.length() && !Update.end(true)) fail = Update.errorString();
  otaBegun = false;
  updBusy  = false;

  if (fail.length()) {
    Update.abort();
    logLine("UPDATE FAILED: " + fail);
    saveUpdateErr(fail);          // the log ring will not survive the restart
    delay(300);
    ESP.restart();                // the panel is gone; a reboot is the only way back
  }
  logLine("update OK (" + String(done / 1024) + " KB) - rebooting");
  delay(400);
  ESP.restart();
}
