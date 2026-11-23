/* DigiFrame — Home Assistant integration over MQTT (optional) */
#pragma once
#include <PubSubClient.h>

/**********************  12e. HOME ASSISTANT (MQTT)  ******************/
/* When enabled with a broker host set, the clock connects to MQTT, announces
 * itself to Home Assistant via MQTT discovery, and exposes: brightness
 * (number), a message text box, celebrate/stop buttons, and temperature/mode
 * sensors. Runs on core 0 (mqttTask); every command is marshalled to core 1
 * through postTgCmd() -> control.h, exactly like the Telegram and BLE paths.
 * Off by default (MQTT_ENABLE=false), so non-HA users are unaffected. */

WiFiClient   mqttNet;
PubSubClient mqttClient(mqttNet);
String       mqttBase;                 // "digiframe/<id>"
String       mqttIdStr;                // "<id>" (from the MAC)

String mqttMakeId() {
  uint8_t mac[6] = {0};
  WiFi.macAddress(mac);
  char b[20];
  snprintf(b, sizeof(b), "digiframe_%02x%02x", mac[4], mac[5]);
  return String(b);
}

static const char *mqttModeName(Mode m) {
  switch (m) {
    case MODE_CLOCK:     return "clock";
    case MODE_MSG:       return "message";
    case MODE_GIF:       return "gif";
    case MODE_CELEBRATE: return "celebrate";
    case MODE_TEST:      return "test";
    case MODE_SETUP:     return "setup";
    case MODE_PANELTEST: return "paneltest";
  }
  return "unknown";
}

/* shared HA "device" block + availability topic, added to each discovery doc */
static void mqttPubDisco(const char *comp, const char *obj, JsonDocument &cfg) {
  cfg["avty_t"] = mqttBase + "/status";
  JsonObject dev = cfg["dev"].to<JsonObject>();
  dev["ids"].to<JsonArray>().add(mqttIdStr);
  dev["name"] = "DigiFrame";
  dev["mdl"]  = "ESP32-S3 HUB75 Smart Clock";
  dev["mf"]   = "DigiFrame";
  String topic = "homeassistant/" + String(comp) + "/" + mqttIdStr + "/" + obj + "/config";
  String out;
  serializeJson(cfg, out);
  mqttClient.publish(topic.c_str(), out.c_str(), true);   // retained
}

void mqttPublishDiscovery() {
  { JsonDocument d; d["name"] = "Brightness"; d["uniq_id"] = mqttIdStr + "_bright";
    d["cmd_t"] = mqttBase + "/brightness/set"; d["stat_t"] = mqttBase + "/brightness";
    d["min"] = 1; d["max"] = 255; mqttPubDisco("number", "brightness", d); }
  { JsonDocument d; d["name"] = "Message"; d["uniq_id"] = mqttIdStr + "_msg";
    d["cmd_t"] = mqttBase + "/message/set"; d["stat_t"] = mqttBase + "/message";
    mqttPubDisco("text", "message", d); }
  { JsonDocument d; d["name"] = "Celebrate"; d["uniq_id"] = mqttIdStr + "_celebrate";
    d["cmd_t"] = mqttBase + "/celebrate/set"; mqttPubDisco("button", "celebrate", d); }
  { JsonDocument d; d["name"] = "Back to clock"; d["uniq_id"] = mqttIdStr + "_stop";
    d["cmd_t"] = mqttBase + "/stop/set"; mqttPubDisco("button", "stop", d); }
  { JsonDocument d; d["name"] = "Temperature"; d["uniq_id"] = mqttIdStr + "_temp";
    d["stat_t"] = mqttBase + "/temperature"; d["unit_of_meas"] = "°C";
    d["dev_cla"] = "temperature"; mqttPubDisco("sensor", "temperature", d); }
  { JsonDocument d; d["name"] = "Mode"; d["uniq_id"] = mqttIdStr + "_mode";
    d["stat_t"] = mqttBase + "/mode"; mqttPubDisco("sensor", "mode", d); }
  logLine("MQTT: HA discovery published");
}

void mqttPublishState(bool force) {
  static uint32_t last = 0;
  if (!force && millis() - last < 10000) return;
  last = millis();
  mqttClient.publish((mqttBase + "/brightness").c_str(), String(userBrightness).c_str(), true);
  mqttClient.publish((mqttBase + "/mode").c_str(), mqttModeName(mode), true);
  mqttClient.publish((mqttBase + "/message").c_str(), scrollText.c_str(), true);
  if (!isnan(wTemp))
    mqttClient.publish((mqttBase + "/temperature").c_str(), String(wTemp, 1).c_str(), true);
}

/* MQTT command -> core-1 action via the shared queue (never touch LittleFS/DMA here) */
void mqttCallback(char *topic, byte *payload, unsigned int len) {
  String t = topic;
  String v; v.reserve(len);
  for (unsigned int i = 0; i < len; i++) v += (char)payload[i];
  if      (t.endsWith("/brightness/set")) postTgCmd(TGC_BRIGHTNESS, "", (uint8_t)constrain(v.toInt(), 1, 255));
  else if (t.endsWith("/message/set"))    postTgCmd(TGC_MSG, v);
  else if (t.endsWith("/celebrate/set"))  postTgCmd(TGC_CELEBRATE);
  else if (t.endsWith("/stop/set"))       postTgCmd(TGC_STOP);
  logLine("MQTT cmd: " + t + " = " + v);
}

bool mqttReconnect() {
  mqttIdStr = mqttMakeId();
  mqttBase  = "digiframe/" + mqttIdStr;
  mqttClient.setServer(mqttHost.c_str(), mqttPort > 0 ? mqttPort : 1883);
  mqttClient.setBufferSize(1024);                 // HA discovery payloads > default 256
  mqttClient.setCallback(mqttCallback);
  String will = mqttBase + "/status";             // last-will marks us offline
  bool ok = mqttUser.length()
    ? mqttClient.connect(mqttIdStr.c_str(), mqttUser.c_str(), mqttPass.c_str(), will.c_str(), 0, true, "offline")
    : mqttClient.connect(mqttIdStr.c_str(), nullptr, nullptr, will.c_str(), 0, true, "offline");
  if (!ok) { logLine("MQTT connect failed rc=" + String(mqttClient.state())); return false; }
  logLine("MQTT connected to " + mqttHost);
  mqttClient.publish(will.c_str(), "online", true);
  mqttClient.subscribe((mqttBase + "/brightness/set").c_str());
  mqttClient.subscribe((mqttBase + "/message/set").c_str());
  mqttClient.subscribe((mqttBase + "/celebrate/set").c_str());
  mqttClient.subscribe((mqttBase + "/stop/set").c_str());
  mqttPublishDiscovery();
  mqttPublishState(true);
  return true;
}

void mqttTask(void *pv) {
  vTaskDelay(pdMS_TO_TICKS(4000));       // let setup() + first WiFi settle
  uint32_t lastTry = 0;
  for (;;) {
    if (mqttConfigDirty) {               // config changed on the dashboard/BLE
      mqttConfigDirty = false;
      if (mqttClient.connected()) mqttClient.disconnect();
    }
    if (mqttEnable && mqttHost.length() && WiFi.status() == WL_CONNECTED) {
      if (!mqttClient.connected()) {
        if (millis() - lastTry > 5000) { lastTry = millis(); mqttReconnect(); }
      } else {
        mqttClient.loop();
        mqttPublishState(false);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}
