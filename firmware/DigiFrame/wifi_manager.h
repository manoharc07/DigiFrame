/* DigiFrame — WiFi connect, setup-hotspot fallback, auto-reconnect */
#pragma once
#include <DNSServer.h>

/**********************  12d. WIFI MANAGER  ***************************/
/* Normal life: plain STA with auto-reconnect to one of up to
 * WIFI_SAVED_MAX saved networks (knownNets, most recent first).
 *
 * Boot tries the most recent network on the exact AP + channel it last
 * used, which skips the scan and is most of the speed-up; if that fails
 * it scans once and tries every visible saved network, strongest first.
 * If none can be reached (at boot, or for WIFI_FAIL_PORTAL_MIN minutes at
 * runtime), a WPA2 hotspot + captive DNS portal starts and the panel
 * shows the join QR (MODE_SETUP). While the portal is up the STA side
 * keeps retrying the saved networks — so when a router comes back, or new
 * creds are saved on the dashboard, the frame reconnects on its own and
 * the portal shuts down.
 *
 * A BSSID-pinned begin() also pins auto-reconnect to that one AP, which
 * would strand the frame if the router were replaced or a mesh node went
 * away. The runtime outage path therefore rescans after WIFI_ROAM_AFTER_MS
 * and re-begins on whatever saved network/AP is actually visible. */

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

  /* Remember which AP and channel we got onto, for the next fast boot.
     This runs on the WiFi event task, not core 1, so it only records —
     wifiManagerTick() does the LittleFS write. */
  WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t info) {
    const auto &c = info.wifi_sta_connected;
    memset(&wifiLearned, 0, sizeof(wifiLearned));
    memcpy(wifiLearned.ssid, c.ssid, min((size_t)c.ssid_len, sizeof(wifiLearned.ssid) - 1));
    memcpy(wifiLearned.bssid, c.bssid, 6);
    wifiLearned.ch   = c.channel;
    wifiLearnedDirty = true;
  }, ARDUINO_EVENT_WIFI_STA_CONNECTED);
}

/* Saved networks seen in the last completed scan, strongest first.
   net[] holds knownNets indices, scan[] the matching scan-result index
   (that network's strongest AP). Returns how many. */
static int wifiVisibleNets(int sc, int8_t *net, int16_t *scan) {
  int m = 0;
  for (int i = 0; i < numKnownNets; i++) {
    int best = -1;
    for (int j = 0; j < sc; j++)
      if (WiFi.SSID(j) == knownNets[i].ssid && (best < 0 || WiFi.RSSI(j) > WiFi.RSSI(best)))
        best = j;
    if (best < 0) continue;
    int k = m++;                                // insertion sort by RSSI
    while (k > 0 && WiFi.RSSI(scan[k - 1]) < WiFi.RSSI(best)) {
      net[k] = net[k - 1]; scan[k] = scan[k - 1]; k--;
    }
    net[k] = i; scan[k] = best;
  }
  return m;
}

/* A connect target picked from a scan. Held by SSID, not by knownNets
   index: the dashboard can forget a network between scan and retry. */
struct WifiPick { char ssid[33]; uint8_t bssid[6]; uint8_t ch; int8_t rssi; };

static bool wifiPickBest(int sc, WifiPick &p) {
  int8_t net[WIFI_SAVED_MAX]; int16_t scan[WIFI_SAVED_MAX];
  if (!wifiVisibleNets(sc, net, scan)) return false;
  strlcpy(p.ssid, knownNets[net[0]].ssid, sizeof(p.ssid));
  memcpy(p.bssid, WiFi.BSSID(scan[0]), 6);
  p.ch   = WiFi.channel(scan[0]);
  p.rssi = WiFi.RSSI(scan[0]);
  return true;
}

/* begin() a saved network, optionally pinned to one AP. Unpinned clears a
   pin left by an earlier begin(): the core rebuilds the STA config each call. */
static bool wifiBeginSsid(const char *ssid, uint8_t ch = 0, const uint8_t *bssid = nullptr) {
  int i = netsFind(knownNets, numKnownNets, ssid);
  if (i < 0) return false;
  WiFi.begin(knownNets[i].ssid, knownNets[i].pass, ch, bssid);
  return true;
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

static bool wifiWait(uint32_t ms) {
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < ms) delay(100);
  return WiFi.status() == WL_CONNECTED;
}

/* Boot-time connect: true if any saved network connected within timeoutMs. */
bool wifiConnect(uint32_t timeoutMs) {
  static bool evReg = false;
  if (!evReg) { evReg = true; wifiEventsInit(); }
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);                         // keeps Telegram snappy
  if (!numKnownNets) { logLine("WiFi: no saved networks"); return false; }
  uint32_t t0 = millis();

  /* 1. fast path: the last network, on the AP + channel it last used */
  const KnownNet &k0 = knownNets[0];
  if (netHasBssid(k0)) {
    WiFi.begin(k0.ssid, k0.pass, k0.ch, k0.bssid);
    if (wifiWait(WIFI_FAST_MS)) {
      logLine("WiFi: '" + String(k0.ssid) + "' fast reconnect in " + String(millis() - t0) + " ms");
      return true;
    }
    WiFi.disconnect();
  }

  /* 2. scan, then every visible saved network, strongest first, sharing
        what is left of the budget between them */
  int sc = WiFi.scanNetworks(false /*sync*/, true /*show hidden*/);
  int8_t net[WIFI_SAVED_MAX]; int16_t scan[WIFI_SAVED_MAX];
  int m = sc > 0 ? wifiVisibleNets(sc, net, scan) : 0;
  WifiPick picks[WIFI_SAVED_MAX];                 // copied out: scanDelete frees the results
  for (int i = 0; i < m; i++) {
    strlcpy(picks[i].ssid, knownNets[net[i]].ssid, sizeof(picks[i].ssid));
    memcpy(picks[i].bssid, WiFi.BSSID(scan[i]), 6);
    picks[i].ch   = WiFi.channel(scan[i]);
    picks[i].rssi = WiFi.RSSI(scan[i]);
  }
  WiFi.scanDelete();
  logLine("WiFi: " + String(m) + " of " + String(numKnownNets) + " saved networks visible");
  for (int i = 0; i < m; i++) {
    uint32_t used = millis() - t0;
    if (used + 2000 > timeoutMs) break;
    wifiBeginSsid(picks[i].ssid, picks[i].ch, picks[i].bssid);
    if (wifiWait((timeoutMs - used) / (m - i))) {
      logLine("WiFi: '" + String(picks[i].ssid) + "' connected in " + String(millis() - t0) + " ms");
      return true;
    }
    WiFi.disconnect();
  }

  /* 3. nothing visible — maybe a hidden SSID, which a scan cannot match:
        spend the rest on the most recent network, unpinned */
  if (!m && millis() - t0 + 2000 < timeoutMs) {
    WiFi.begin(k0.ssid, k0.pass);
    if (wifiWait(timeoutMs - (millis() - t0))) return true;
  }
  return false;
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

  /* a connect succeeded somewhere: promote that network + remember its AP */
  if (wifiLearnedDirty) {
    wifiLearnedDirty = false;
    KnownNet k = wifiLearned;
    if (netsPromote(knownNets, numKnownNets, k.ssid, k.bssid, k.ch)) {
      saveWifiNets();
      logLine("WiFi: remembered '" + String(k.ssid) + "' on ch " + String(k.ch));
    }
  }

  static uint32_t lastScanAt = 0;
  static WifiPick pick;
  static bool     havePick  = false;
  static uint8_t  rrNext    = 0;                // round-robin when the scan saw nothing

  if (portalActive) {
    if (WiFi.status() == WL_CONNECTED) { stopPortal(); return; }

    /* async scan between retries — is any saved network even visible, on
       what channel, how strong? (channel 12/13 and weak signal are the
       classic "reason 201" culprits). Seeing one triggers a retry now. */
    int sc = WiFi.scanComplete();
    if (sc >= 0) {
      havePick = wifiPickBest(sc, pick);
      if (havePick) {
        logLine("scan: '" + String(pick.ssid) + "' ch " + String(pick.ch) + ", " +
                String(pick.rssi) + " dBm");
        lastStaRetryAt = ms - 30001;
      } else {
        logLine("scan: no saved network visible (" + String(sc) + " networks seen)");
      }
      WiFi.scanDelete();
    }

    if (wifiRetryNow || ms - lastStaRetryAt > 30000) {
      lastStaRetryAt = ms;
      WiFi.disconnect();                        // reset a stuck connect state
      if (wifiRetryNow && numKnownNets) {       // just saved on the dashboard
        logLine("portal: trying WiFi '" + String(knownNets[0].ssid) + "' ...");
        wifiBeginSsid(knownNets[0].ssid);
      } else if (havePick && wifiBeginSsid(pick.ssid, pick.ch, pick.bssid)) {
        logLine("portal: trying WiFi '" + String(pick.ssid) + "' ...");
      } else if (numKnownNets) {
        if (rrNext >= numKnownNets) rrNext = 0;
        logLine("portal: trying WiFi '" + String(knownNets[rrNext].ssid) + "' ...");
        wifiBeginSsid(knownNets[rrNext++].ssid);
      }
      wifiRetryNow = false;
      havePick     = false;                     // a pick is only good for one try
    } else if (sc == WIFI_SCAN_FAILED && ms - lastScanAt > 60000 &&
               ms - lastStaRetryAt > 10000) {   // don't abort a fresh connect attempt
      lastScanAt = ms;
      WiFi.scanNetworks(true /*async*/, true /*show hidden*/);
    }
    return;
  }

  if (wifiRetryNow) {                 // creds changed on dashboard while online
    wifiRetryNow = false;
    if (!numKnownNets) return;
    logLine("WiFi: switching to '" + String(knownNets[0].ssid) + "'");
    WiFi.disconnect();
    wifiBeginSsid(knownNets[0].ssid);
    wifiDownSince = 0;
    return;
  }

  if (WiFi.status() == WL_CONNECTED) { wifiDownSince = 0; return; }
  // outage: auto-reconnect handles quick blips; reopen the portal only
  // after a sustained failure (e.g. the frame moved to a new home).
  if (!wifiDownSince) { wifiDownSince = ms ? ms : 1; return; }
  if (ms - wifiDownSince > (uint32_t)WIFI_FAIL_PORTAL_MIN * 60000UL) { startPortal(); return; }

  /* Past a blip: auto-reconnect is pinned to one AP, so look for whatever
     saved network is actually on the air — another one, or the same SSID
     on a new router — and begin on that. */
  if (ms - wifiDownSince < WIFI_ROAM_AFTER_MS || !numKnownNets) return;
  int sc = WiFi.scanComplete();
  if (sc >= 0) {
    if (wifiPickBest(sc, pick) && wifiBeginSsid(pick.ssid, pick.ch, pick.bssid))
      logLine("WiFi: roaming to '" + String(pick.ssid) + "' ch " + String(pick.ch));
    WiFi.scanDelete();
  } else if (sc == WIFI_SCAN_FAILED && ms - lastScanAt > WIFI_ROAM_AFTER_MS) {
    lastScanAt = ms;
    WiFi.scanNetworks(true /*async*/, true /*show hidden*/);
  }
}
