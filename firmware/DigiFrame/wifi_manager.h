/* DigiFrame — WiFi connect, setup-hotspot fallback, auto-reconnect */
#pragma once
#include <DNSServer.h>

/**********************  12d. WIFI MANAGER  ***************************/
/* Normal life: plain STA with auto-reconnect. If the stored network
 * can't be reached (at boot, or for WIFI_FAIL_PORTAL_MIN minutes at
 * runtime), a WPA2 hotspot + captive DNS portal starts and the panel
 * shows the join QR (MODE_SETUP). While the portal is up the STA side
 * keeps retrying the stored credentials every 30 s — so when the old
 * router comes back, or new creds are saved on the dashboard, the frame
 * reconnects on its own and the portal shuts down. */

DNSServer dnsServer;
uint32_t  lastStaRetryAt = 0;
uint32_t  wifiDownSince  = 0;

/* Human-readable STA disconnect reasons (the common ones) so the
 * dashboard log shows WHY a connect attempt failed. */
static const char* wifiReasonStr(uint8_t r) {
  switch (r) {
    case 2:   return " (auth expired)";
    case 8:   return " (left network)";
    case 15:  return " (handshake timeout - wrong password?)";
    case 200: return " (beacon timeout - weak signal?)";
    case 201: return " (network not found - 2.4 GHz? channel 12/13? in range?)";
    case 202: return " (auth failed - wrong password?)";
    case 203: return " (association failed - channel busy? try disconnecting phone from hotspot)";
    case 204: return " (handshake timeout - wrong password?)";
    default:  return "";
  }
}

void wifiEventsInit() {
  WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t info) {
    static uint8_t  lastReason = 0;
    static uint32_t lastLogAt  = 0;
    uint8_t r = info.wifi_sta_disconnected.reason;
    // auto-reconnect fires this repeatedly — log only reason changes, or 30s heartbeats
    if (r == lastReason && millis() - lastLogAt < 30000) return;
    lastReason = r; lastLogAt = millis();
    logLine("WiFi fail: reason " + String(r) + wifiReasonStr(r));
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
}

void startPortal() {
  if (portalActive) return;
  closeGif();
  WiFi.mode(WIFI_AP_STA);                       // STA stays alive for retries
  WiFi.softAP(AP_SSID, AP_PASS);
  dnsServer.start(53, "*", WiFi.softAPIP());    // captive portal: all DNS -> us
  portalActive   = true;
  qrLastText     = "";                          // force QR redraw
  mode           = MODE_SETUP;
  dma->setBrightness8(150);                     // QR must stay scannable
  lastStaRetryAt = millis();
  logLine("PORTAL up: AP '" AP_SSID "' pass '" AP_PASS "' -> http://192.168.4.1");
}

void stopPortal() {
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  portalActive = false;
  if (MDNS.begin("digiframe")) MDNS.addService("http", "tcp", 80);
  dma->setBrightness8(userBrightness);
  if (mode == MODE_SETUP) mode = MODE_CLOCK;
  logLine("PORTAL down — WiFi OK, IP " + WiFi.localIP().toString());
}

/* Boot-time connect: true if connected within timeoutMs. */
bool wifiConnect(uint32_t timeoutMs) {
  static bool evReg = false;
  if (!evReg) { evReg = true; wifiEventsInit(); }
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(cfgWifiSsid.c_str(), cfgWifiPass.c_str());
  WiFi.setSleep(false);                         // keeps Telegram snappy
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) delay(200);
  return WiFi.status() == WL_CONNECTED;
}

/* Called from loop()'s network-service slot on core 1 (every
   NET_SERVICE_MS). The captive-portal DNS is answered at that full rate;
   everything below it is housekeeping on 10-60 s timers that only needs a
   few Hz. The split matters because WiFi.status() reaches into the WiFi
   driver and takes the lock the driver task holds — polling it from the
   render core hundreds of times a second is pure contention, and the retry
   logic below cannot act any sooner for it. */
void wifiManagerTick() {
  uint32_t ms = millis();

  if (portalActive) dnsServer.processNextRequest();

  static uint32_t lastHousekeepAt = 0;
  if (ms - lastHousekeepAt < WIFI_TICK_MS) return;
  lastHousekeepAt = ms;

  if (portalActive) {
    if (WiFi.status() == WL_CONNECTED) { stopPortal(); return; }

    /* diagnostic: async scan between retries — is the target SSID even
       visible, on what channel, how strong? (channel 12/13 and weak
       signal are the classic "reason 201" culprits) */
    static uint32_t lastScanAt = 0;
    int sc = WiFi.scanComplete();
    if (sc >= 0) {
      String s = "scan: '" + cfgWifiSsid + "' ";
      int hit = -1;
      for (int i = 0; i < sc; i++)
        if (WiFi.SSID(i) == cfgWifiSsid && (hit < 0 || WiFi.RSSI(i) > WiFi.RSSI(hit))) hit = i;
      if (hit >= 0) s += "ch " + String(WiFi.channel(hit)) + ", " + String(WiFi.RSSI(hit)) + " dBm";
      else          s += "NOT visible (" + String(sc) + " networks seen)";
      logLine(s);
      WiFi.scanDelete();
    }

    if (wifiRetryNow || ms - lastStaRetryAt > 30000) {
      lastStaRetryAt = ms;
      wifiRetryNow   = false;
      logLine("portal: trying WiFi '" + cfgWifiSsid + "' ...");
      WiFi.disconnect();                        // reset a stuck connect state
      WiFi.begin(cfgWifiSsid.c_str(), cfgWifiPass.c_str());
    } else if (sc == WIFI_SCAN_FAILED && ms - lastScanAt > 60000 &&
               ms - lastStaRetryAt > 10000) {   // don't abort a fresh connect attempt
      lastScanAt = ms;
      WiFi.scanNetworks(true /*async*/, true /*show hidden*/);
    }
    return;
  }

  if (wifiRetryNow) {                 // creds changed on dashboard while online
    wifiRetryNow = false;
    logLine("WiFi: switching to '" + cfgWifiSsid + "'");
    WiFi.disconnect();
    WiFi.begin(cfgWifiSsid.c_str(), cfgWifiPass.c_str());
    wifiDownSince = 0;
    return;
  }

  if (WiFi.status() == WL_CONNECTED) { wifiDownSince = 0; return; }
  // outage: auto-reconnect handles quick blips; reopen the portal only
  // after a sustained failure (e.g. the frame moved to a new home).
  if (!wifiDownSince) wifiDownSince = ms ? ms : 1;
  else if (ms - wifiDownSince > (uint32_t)WIFI_FAIL_PORTAL_MIN * 60000UL) startPortal();
}
