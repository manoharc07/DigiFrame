/* DigiFrame — on-panel QR codes for setup-hotspot mode */
#pragma once
#include <qrcode.h>   // espressif__qrcode component, bundled with the ESP32 core

/**********************  12c. SETUP QR DISPLAY  ***********************/
/* While the setup hotspot is up (MODE_SETUP) the panel shows a QR code.
 * Before a phone joins it encodes a WIFI: payload, so scanning it joins the
 * "DigiFrame" hotspot directly (Android and iOS camera apps both handle
 * this natively). Once a station connects, the QR switches to
 * http://192.168.4.1 so the on-device dashboard opens even if the captive
 * popup didn't appear. Provisioning is HTTP-only now — a cloud page can't
 * reach a LAN device over http:// from an https:// origin (mixed content),
 * which is why the hotspot portal is the bootstrap path.
 *
 * qrToPanel scales by PANEL_W/size (2x for versions <= 3, 1x above), always
 * centered. QR wants dark-on-light, so the background is lit and modules are
 * unlit LEDs. Static image — redraw only when the text changes. */

/* qrLastText lives in globals.h: the setup QR is a static image that only
   repaints when its text changes, so both wifi_manager.h and the /api/frame
   screenshot endpoint (which needs to force one redraw to have anything to
   capture) must be able to clear it, and both are included before this. */

/* WIFI: payload reserves \ ; , : and " — escape them or a password
   containing any of them silently produces an unscannable/wrong network. */
static String qrEscape(const String &s) {
  String out;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"') out += '\\';
    out += c;
  }
  return out;
}

/* esp_qrcode_generate() hands the finished code to this callback. */
static void qrToPanel(esp_qrcode_handle_t qrcode) {
  int size  = esp_qrcode_get_size(qrcode);          // 21/25/29 modules
  int scale = PANEL_W / size;                       // 2 for all of those
  int off   = (PANEL_W - size * scale) / 2;
  uint16_t bg = dma->color565(140, 140, 150);       // readable, not blinding
  for (int b = 0; b < 2; b++) {                     // paint both DMA buffers
    dma->fillScreen(bg);
    for (int y = 0; y < size; y++)
      for (int x = 0; x < size; x++)
        if (esp_qrcode_get_module(qrcode, x, y))
          dma->fillRect(off + x * scale, off + y * scale, scale, scale, 0);
    panelPresent();
  }
}

void renderSetupQR() {
  String txt = (WiFi.softAPgetStationNum() > 0)
                 ? String("http://192.168.4.1")
                 : "WIFI:T:WPA;S:" + qrEscape(AP_SSID) + ";P:" + qrEscape(AP_PASS) + ";;";
  if (txt == qrLastText) return;          // static image — redraw only on change
  qrLastText = txt;

  esp_qrcode_config_t cfg = {
    .display_func       = qrToPanel,
    .max_qrcode_version = 6,              // up to 41x41 -> 41 px at 1x (fits the URL)
    .qrcode_ecc_level   = ESP_QRCODE_ECC_LOW,
  };
  if (esp_qrcode_generate(&cfg, txt.c_str()) != ESP_OK) {
    logLine("QR encode failed for: " + txt);
    return;
  }
  logLine("QR shown: " + txt);
}
