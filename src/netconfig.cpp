// =====================================================================
//  netconfig.cpp  -  soft-AP + settings web page (see netconfig.h)
// =====================================================================
#include "netconfig.h"
#include "settings.h"
#include "config.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <string.h>

static WebServer s_srv(80);
static bool      s_active = false;
static char      s_ssid[24] = { 0 };
static char      s_ip[16]   = "0.0.0.0";
static const char* kPass    = "pet12345";      // WPA2 password (>= 8 chars)

// ---------------------------------------------------------------- helpers
static uint32_t argULong(const char* name, uint32_t dflt, uint32_t lo, uint32_t hi) {
  if (!s_srv.hasArg(name)) return dflt;
  uint32_t v = (uint32_t)strtoul(s_srv.arg(name).c_str(), nullptr, 10);
  return v < lo ? lo : (v > hi ? hi : v);
}

static String page(const String& body) {
  String h;
  h.reserve(body.length() + 700);
  h += F("<!DOCTYPE html><html lang=\"zh\"><head><meta charset=\"utf-8\">"
         "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
         "<title>ESP32 宠物设置</title><style>"
         "*{box-sizing:border-box}body{font-family:system-ui,Arial,sans-serif;margin:0;"
         "background:#0f1424;color:#e6ecff}.wrap{max-width:520px;margin:0 auto;padding:16px}"
         "h1{font-size:19px;margin:10px 0 4px}p.sub{color:#7d8bb5;font-size:13px;margin:0 0 8px}"
         "label{display:block;margin:12px 0 4px;color:#9fb0d8;font-size:14px}"
         "label.ck{margin:6px 0;font-size:16px;color:#e6ecff}"
         "input[type=number]{width:100%;background:#1b2238;color:#e6ecff;border:1px solid #2c3757;"
         "border-radius:8px;padding:10px;font-size:16px}"
         "input[type=checkbox]{transform:scale(1.35);margin-right:8px;vertical-align:middle}"
         "button{width:100%;margin-top:18px;padding:14px;background:#1e88ff;color:#fff;border:0;"
         "border-radius:8px;font-size:17px}button:active{background:#0d6ed6}"
         "a{color:#5fb0ff}.row{display:flex;gap:12px}.row>div{flex:1}"
         ".hint{color:#7d8bb5;font-size:13px;margin-top:8px}</style></head><body>"
         "<div class=\"wrap\">");
  h += body;
  h += F("</div></body></html>");
  return h;
}

// ---------------------------------------------------------------- handlers
static void handleRoot() {
  String b;
  b.reserve(1800);
  b += F("<h1>ESP32 宠物 · 设置</h1><p class=\"sub\">改动保存后立即生效，无需重启</p>"
         "<form method=\"POST\" action=\"/save\">");

  b += F("<label>话术（勾选的句子会随机说出，可增可减）</label>");
  for (int i = 0; i < settingsPhraseCount(); i++) {
    b += F("<label class=\"ck\"><input type=\"checkbox\" name=\"p");
    b += i;
    b += '"';
    if (settingsPhraseEnabled(i)) b += F(" checked");
    b += '>';
    b += settingsPhrase(i);
    b += F("</label>");
  }

  b += F("<label>对话间隔（秒）</label><div class=\"row\"><div>最短 "
         "<input type=\"number\" name=\"talkmin\" min=\"2\" max=\"600\" value=\"");
  b += (uint32_t)(cfg.talkMinMs / 1000);
  b += F("\"></div><div>最长 <input type=\"number\" name=\"talkmax\" min=\"2\" max=\"600\" value=\"");
  b += (uint32_t)(cfg.talkMaxMs / 1000);
  b += F("\"></div></div>");

  b += F("<label>表情变化间隔（秒）</label><div class=\"row\"><div>最短 "
         "<input type=\"number\" name=\"exprmin\" min=\"1\" max=\"600\" value=\"");
  b += (uint32_t)(cfg.exprMinMs / 1000);
  b += F("\"></div><div>最长 <input type=\"number\" name=\"exprmax\" min=\"1\" max=\"600\" value=\"");
  b += (uint32_t)(cfg.exprMaxMs / 1000);
  b += F("\"></div></div>");

  b += F("<label>成长速度（20-400%，100 为正常）</label>"
         "<input type=\"number\" name=\"growth\" min=\"20\" max=\"400\" value=\"");
  b += cfg.growthPct;
  b += F("\">");

  b += F("<button type=\"submit\">保存</button></form>"
         "<div class=\"hint\">提示：数值越小变化越快。</div>");
  s_srv.send(200, "text/html; charset=utf-8", page(b));
}

static void handleSave() {
  for (int i = 0; i < settingsPhraseCount(); i++) {
    char key[8];
    snprintf(key, sizeof key, "p%d", i);
    settingsSetPhraseEnabled(i, s_srv.hasArg(key));
  }
  cfg.talkMinMs = argULong("talkmin", 18, 2, 600) * 1000UL;
  cfg.talkMaxMs = argULong("talkmax", 42, 2, 600) * 1000UL;
  cfg.exprMinMs = argULong("exprmin",  6, 1, 600) * 1000UL;
  cfg.exprMaxMs = argULong("exprmax", 15, 1, 600) * 1000UL;
  cfg.growthPct = (uint16_t)argULong("growth", 100, 20, 400);
  if (cfg.talkMaxMs < cfg.talkMinMs) cfg.talkMaxMs = cfg.talkMinMs;
  if (cfg.exprMaxMs < cfg.exprMinMs) cfg.exprMaxMs = cfg.exprMinMs;
  settingsSave();

  String b;
  b.reserve(300);
  b += F("<h1>已保存</h1><p class=\"sub\">设置已经写入，宠物马上就会用新的配置。</p>"
         "<p><a href=\"/\">← 返回继续设置</a></p>");
  s_srv.send(200, "text/html; charset=utf-8", page(b));
}

static void handleNotFound() {
  s_srv.sendHeader("Location", "/", true);     // captive-portal style bounce
  s_srv.send(302, "text/plain", "");
}

// ---------------------------------------------------------------- lifecycle
bool netConfigBegin() {
  if (s_active) return true;

  // unique-ish SSID from the low 16 bits of the MAC
  const uint64_t mac = ESP.getEfuseMac();
  snprintf(s_ssid, sizeof s_ssid, "ESP32-Pet-%04X", (unsigned)(mac & 0xFFFF));

  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  IPAddress ip(192, 168, 4, 1), gw(192, 168, 4, 1), mask(255, 255, 255, 0);
  WiFi.softAPConfig(ip, gw, mask);
  if (!WiFi.softAP(s_ssid, kPass, 1, 0, 4)) return false;

  strncpy(s_ip, WiFi.softAPIP().toString().c_str(), sizeof s_ip - 1);
  s_ip[sizeof s_ip - 1] = 0;

  s_srv.on("/", HTTP_GET, handleRoot);
  s_srv.on("/save", HTTP_POST, handleSave);
  s_srv.onNotFound(handleNotFound);
  s_srv.begin();
  s_active = true;

  Serial.printf("[net] AP \"%s\" pass \"%s\" up at http://%s\n", s_ssid, kPass, s_ip);
  return true;
}

void netConfigLoop() {
  if (s_active) s_srv.handleClient();
}

void netConfigEnd() {
  if (!s_active) return;
  s_srv.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  s_active = false;
  strcpy(s_ip, "0.0.0.0");
  Serial.println("[net] AP stopped");
}

bool        netConfigActive() { return s_active; }
const char* netConfigSsid()   { return s_ssid; }
const char* netConfigPass()   { return kPass; }
const char* netConfigIp()     { return s_ip; }
