/* DigiFrame — Telegram bot commands + menus */
#pragma once

/**********************  12. TELEGRAM  ********************************/
/* GIF uploads happen on the local dashboard (http://digiframe.local) —
 * the bot only controls playback. The main actions live on a persistent
 * reply keyboard (buttons under the text box); picking a GIF or a
 * brightness level uses inline keyboards answered as callback queries.
 * Runs on core 0 (tgTask): render/mode changes always go through
 * postTgCmd() and are applied by loop() on core 1. */

/* The reply keyboard, built from the active language's labels each time it is
   sent. A tap arrives as the label text, so the matcher in handleTelegram()
   accepts the label in every language (trMatchesAny), not just this one. */
static const char *const REPLY_KB_KEYS[4][2] = {
  {"tg.kb.celebrate", "tg.kb.stop"},
  {"tg.kb.play",      "tg.kb.bright"},
  {"tg.kb.status",    "tg.kb.list"},
  {"tg.kb.events",    "tg.kb.test"},
};
String replyKeyboard() {
  String kb = "[";
  for (int r = 0; r < 4; r++) {
    if (r) kb += ",";
    kb += "[";
    for (int c = 0; c < 2; c++) {
      if (c) kb += ",";
      kb += "\"";
      kb += tr(REPLY_KB_KEYS[r][c]);    // our own labels: no quotes to escape
      kb += "\"";
    }
    kb += "]";
  }
  return kb + "]";
}

void sendMainMenu(const String &chat) {
  bot.sendMessageWithReplyKeyboard(chat, trf("tg.menu", langCodeList().c_str()),
    "", replyKeyboard(), true);   // resize keyboard to fit
}

/* Inline keyboard of stored GIFs — tapping one plays it. */
void sendPlayKeyboard(const String &chat) {
  String kb = "[";
  bool any = false;
  File root = LittleFS.open("/");
  File f = root.openNextFile();
  while (f) {
    String nm = f.name();
    if (nm.endsWith(".gif")) {
      if (any) kb += ",";
      String base = nm.substring(0, nm.length() - 4);
      kb += "[{\"text\":\"\xE2\x96\xB6 " + base + "\",\"callback_data\":\"play:/" + nm + "\"}]";
      any = true;
    }
    f = root.openNextFile();
  }
  kb += "]";
  if (!any) {
    bot.sendMessage(chat, tr("tg.gif.none"));
    return;
  }
  bot.sendMessageWithInlineKeyboard(chat, tr("tg.gif.pick"), "", kb);
}

/* Inline brightness presets. */
void sendBrightnessKeyboard(const String &chat) {
  bot.sendMessageWithInlineKeyboard(chat, tr("tg.bright.pick"), "",
    "[[{\"text\":\"\xF0\x9F\x8C\x91 10\",\"callback_data\":\"bright:10\"},"
      "{\"text\":\"\xF0\x9F\x8C\x98 40\",\"callback_data\":\"bright:40\"},"
      "{\"text\":\"\xF0\x9F\x8C\x97 100\",\"callback_data\":\"bright:100\"}],"
     "[{\"text\":\"\xF0\x9F\x8C\x96 160\",\"callback_data\":\"bright:160\"},"
      "{\"text\":\"\xF0\x9F\x8C\x95 255\",\"callback_data\":\"bright:255\"}]]");
}

void handleTelegram() {
  int n = bot.getUpdates(bot.last_message_received + 1);
  if (n < 0) {
    logLine("TG poll error (getUpdates<0) — check token/TLS");
    return;
  }
  if (n > 0) logLine("TG got " + String(n) + " update(s)");
  while (n) {
    for (int i = 0; i < n; i++) {
      telegramMessage &m = bot.messages[i];
      logLine("TG from chat_id=" + m.chat_id +
              (m.text.length() ? (" text='" + m.text + "'") : String(" (non-text)")));
      if (m.chat_id != allowedChatId) {
        logLine("  -> IGNORED: chat_id != allowed (" + allowedChatId + ")");
        bot.sendMessage(m.chat_id, tr("tg.unauth"));
        continue;
      }

      /* ---- inline-button taps (callback queries) ---- */
      if (m.type == "callback_query") {
        String d = m.text;
        if (d.startsWith("play:")) {
          postTgCmd(TGC_PLAY_GIF, d.substring(5));
          bot.answerCallbackQuery(m.query_id, tr("tg.cb.playing"));
        } else if (d.startsWith("bright:")) {
          uint8_t v = constrain(d.substring(7).toInt(), 1, 255);
          userBrightness = v;
          postTgCmd(TGC_BRIGHTNESS, "", v);
          saveConfig();
          bot.answerCallbackQuery(m.query_id, trf("tg.cb.bright", (int)v));
        } else {
          bot.answerCallbackQuery(m.query_id);
        }
        continue;
      }

      String t = m.text;
      t.trim();

      /* ---- reply-keyboard buttons map onto the classic commands ---- */
      if      (trMatchesAny(t, "tg.kb.celebrate")) t = "/celebrate";
      else if (trMatchesAny(t, "tg.kb.stop"))      t = "/stop";
      else if (trMatchesAny(t, "tg.kb.status"))    t = "/status";
      else if (trMatchesAny(t, "tg.kb.list"))      t = "/list";
      else if (trMatchesAny(t, "tg.kb.events"))    t = "/events";
      else if (trMatchesAny(t, "tg.kb.test"))      t = "/test";
      else if (trMatchesAny(t, "tg.kb.play"))      { sendPlayKeyboard(m.chat_id);       continue; }
      else if (trMatchesAny(t, "tg.kb.bright"))    { sendBrightnessKeyboard(m.chat_id); continue; }

      /* ---- commands ---- */
      if (t.startsWith("/msg ") || t.startsWith("/pin ")) {
        String txt = t.substring(5); txt.toUpperCase();
        postTgCmd(t.startsWith("/msg ") ? TGC_MSG : TGC_PIN, txt);
        bot.sendMessage(m.chat_id, tr("tg.msg.ok"));
      }
      else if (t.startsWith("/play ")) {
        String p = "/" + t.substring(6) + ".gif";
        postTgCmd(TGC_PLAY_GIF, p);
        bot.sendMessage(m.chat_id, trf("tg.play.ok", p.c_str()));
      }
      else if (t == "/list") {
        String out = String(tr("tg.list.head")) + "\n";
        File root = LittleFS.open("/");
        File f = root.openNextFile();
        while (f) {
          String nm = f.name();
          if (nm.endsWith(".gif"))
            out += "  " + nm + "  (" + String(f.size() / 1024) + " KB)\n";
          f = root.openNextFile();
        }
        out += "\n" + trf("tg.list.free", (int)((LittleFS.totalBytes() - LittleFS.usedBytes()) / 1024));
        out += "\n" + String(tr("tg.list.upload"));
        bot.sendMessage(m.chat_id, out);
      }
      else if (t.startsWith("/del ")) {
        String p = "/" + t.substring(5) + ".gif";
        bot.sendMessage(m.chat_id, tr(LittleFS.remove(p) ? "tg.del.ok" : "tg.notfound"));
      }
      else if (t.startsWith("/char every ")) {
        int mn = t.substring(12).toInt();
        charEveryMs = (mn <= 0) ? 0 : (uint32_t)mn * 60000UL;
        saveConfig();
        bot.sendMessage(m.chat_id, mn <= 0 ? String(tr("tg.char.off")) : trf("tg.char.every", mn));
      }
      else if (t == "/char list") {
        String out = String(tr("tg.char.head")) + "\n";
        File root = LittleFS.open("/");
        File f = root.openNextFile();
        int cnt = 0;
        while (f) {
          String nm = f.name();
          if (nm.startsWith("c_") && nm.endsWith(".gif")) { out += "  " + nm + "\n"; cnt++; }
          f = root.openNextFile();
        }
        if (!cnt) out += "  " + String(tr("tg.char.empty"));
        bot.sendMessage(m.chat_id, out);
      }
      else if (t.startsWith("/char del ")) {
        String p = "/c_" + t.substring(10) + ".gif";
        bot.sendMessage(m.chat_id, tr(LittleFS.remove(p) ? "tg.char.removed" : "tg.notfound"));
      }
      else if (t.startsWith("/brightness ")) {
        userBrightness = constrain(t.substring(12).toInt(), 1, 255);
        postTgCmd(TGC_BRIGHTNESS, "", (uint8_t)userBrightness);
        saveConfig();
        bot.sendMessage(m.chat_id, tr("tg.bright.ok"));
      }
      else if (t.startsWith("/orient ")) {
        int deg = t.substring(8).toInt();
        if (deg % 90 || deg < 0 || deg > 270) bot.sendMessage(m.chat_id, tr("tg.orient.use"));
        else { postTgCmd(TGC_ORIENT, "", (uint8_t)(deg / 90)); bot.sendMessage(m.chat_id, tr("tg.orient.ok")); }
      }
      else if (t.startsWith("/units ")) {
        String u = t.substring(7); u.trim(); u.toLowerCase();
        if (u == "f" || u == "fahrenheit")   { postTgCmd(TGC_TEMPUNIT, "", 1); bot.sendMessage(m.chat_id, tr("tg.units.f")); }
        else if (u == "c" || u == "celsius") { postTgCmd(TGC_TEMPUNIT, "", 0); bot.sendMessage(m.chat_id, tr("tg.units.c")); }
        else bot.sendMessage(m.chat_id, tr("tg.units.use"));
      }
      else if (t.startsWith("/clock")) {
        String c = t.substring(6); c.trim();
        if (c == "24")      { postTgCmd(TGC_CLOCK24, "", 1); bot.sendMessage(m.chat_id, tr("tg.clock.24")); }
        else if (c == "12") { postTgCmd(TGC_CLOCK24, "", 0); bot.sendMessage(m.chat_id, tr("tg.clock.12")); }
        else bot.sendMessage(m.chat_id, tr("tg.clock.use"));
      }
      else if (t.startsWith("/lang")) {
        String c = t.substring(5); c.trim();
        int li = langIndex(c);
        if (li < 0) bot.sendMessage(m.chat_id, trf("tg.lang.use", langCodeList().c_str()));
        else {
          postTgCmd(TGC_LANG, "", (uint8_t)li);
          /* Confirm in the NEW language. Core 1 has not applied the switch
             yet, so read the target table directly rather than through tr(). */
          const char *ok = trIn(li, "tg.lang.ok");
          bot.sendMessage(m.chat_id, ok ? ok : "OK");
        }
      }
      else if (t.startsWith("/event add ")) {
        // /event add MM-DD [birthday|custom] message
        String rest = t.substring(11); rest.trim();
        if (rest.length() >= 6 && rest[2] == '-' && rest[5] == ' ') {
          String date = rest.substring(0, 5);
          String tail = rest.substring(6); tail.trim();
          String type = "custom";
          int sp = tail.indexOf(' ');
          String firstTok = (sp < 0) ? tail : tail.substring(0, sp);
          if (firstTok == "birthday" || firstTok == "custom") {   // optional leading type
            type = firstTok;
            tail = (sp < 0) ? "" : tail.substring(sp + 1);
            tail.trim();
          }
          bool ok = ctlAddEvent(date, type, tail);
          bot.sendMessage(m.chat_id, tr(ok ? "tg.ev.saved" : "tg.ev.fail"));
        } else bot.sendMessage(m.chat_id, tr("tg.ev.format"));
      }
      else if (t.startsWith("/event del ")) {
        String d = t.substring(11); d.trim();
        bot.sendMessage(m.chat_id, tr(ctlDelEvent(d) ? "tg.ev.removed" : "tg.ev.nodate"));
      }
      else if (t == "/events") {
        String out = String(tr("tg.ev.head")) + "\n";
        for (int k = 0; k < numEvents; k++)
          out += "  " + events[k].date + "  [" + events[k].type + "]  " + events[k].message + "\n";
        if (!numEvents) out += "  " + String(tr("tg.ev.empty")) + "\n";
        out += "\n" + String(tr("tg.ev.help"));
        bot.sendMessage(m.chat_id, out);
      }
      else if (t == "/celebrate" || t == "/party") {
        postTgCmd(TGC_CELEBRATE);   // empty -> ctlCelebrate (today's event, else generic)
        bot.sendMessage(m.chat_id, tr("tg.celeb.ok"));
      }
      else if (t == "/test") {
        postTgCmd(TGC_TEST, m.chat_id);
        bot.sendMessage(m.chat_id, tr("tg.test.ok"));
      }
      else if (t == "/stop") {
        postTgCmd(TGC_STOP);
        bot.sendMessage(m.chat_id, tr("tg.stop.ok"));
      }
      else if (t == "/status") {
        String evName; int d = daysToNextEvent(evName);
        char tb[32];
        snprintf(tb, sizeof(tb), "%02d:%02d:%02d", tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec);
        String temp = String(tempDisp(wTemp), 1) + tempUnit();
        String next = (d >= 0) ? trf("tg.status.next", evName.c_str(), d) : String(tr("tg.status.none"));
        String out  = trf("tg.status", tb, (int)mode, temp.c_str(), (int)wCode,
                          (int)(ESP.getFreeHeap() / 1024), WiFi.localIP().toString().c_str(),
                          next.c_str());
        bot.sendMessage(m.chat_id, out);
      }
      else if (t == "/help" || t == "/start" || t == "/menu") {
        sendMainMenu(m.chat_id);
      }
    }
    n = bot.getUpdates(bot.last_message_received + 1);
  }
}
