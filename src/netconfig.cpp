// =====================================================================
//  netconfig.cpp  -  soft-AP + settings web page (see netconfig.h)
// =====================================================================
#include "netconfig.h"
#include "settings.h"
#include "config.h"
#include "splash.h"
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

// Escape the handful of HTML metacharacters so the title typed on the phone can
// be echoed back into the form's value="" attribute without breaking the page.
static void htmlEsc(String& out, const char* s) {
  for (const char* p = s; p && *p; p++) {
    switch (*p) {
      case '&':  out += F("&amp;");  break;
      case '<':  out += F("&lt;");   break;
      case '>':  out += F("&gt;");   break;
      case '"':  out += F("&quot;"); break;
      default:   out += *p;          break;
    }
  }
}

// The welcome title is drawn by the on-device raster font, which only carries
// printable ASCII (the CJK glyphs are baked at build time), so a title typed on
// the phone is trimmed to 0x20..0x7E on the way into NVS. Chinese goes through
// the built-in greetings (BOOT_LINES) instead.
static void copyAscii(char* dst, size_t n, const char* src) {
  if (!dst || !n) return;
  size_t j = 0;
  for (const char* p = src; p && *p && j + 1 < n; p++) {
    const unsigned char c = (unsigned char)*p;
    if (c >= 0x20 && c < 0x7F) dst[j++] = (char)c;
  }
  dst[j] = 0;
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
         "input[type=number],input[type=text],select{width:100%;background:#1b2238;color:#e6ecff;"
         "border:1px solid #2c3757;border-radius:8px;padding:10px;font-size:16px}"
         "input[type=checkbox]{transform:scale(1.35);margin-right:8px;vertical-align:middle}"
         "button{width:100%;margin-top:18px;padding:14px;background:#1e88ff;color:#fff;border:0;"
         "border-radius:8px;font-size:17px}button:active{background:#0d6ed6}"
         "a{color:#5fb0ff}.row{display:flex;gap:12px}.row>div{flex:1}"
         ".hint{color:#7d8bb5;font-size:13px;margin-top:8px}"
         "h2{font-size:16px;margin:26px 0 6px;padding-top:14px;border-top:1px solid #2c3757}"
         "canvas{width:100%;height:auto;image-rendering:pixelated;background:#000;"
         "border:1px solid #2c3757;border-radius:8px}"
         ".ok{color:#48d597}.bad{color:#ff6b6b}</style></head><body>"
         "<div class=\"wrap\">");
  h += body;
  h += F("</div></body></html>");
  return h;
}

// Every page goes out no-store: the Arduino WebServer sends no Cache-Control at
// all, so phones happily reuse the page they fetched from the *previous*
// firmware and newly added fields would never show up.
static void sendPage(const String& body) {
  s_srv.sendHeader("Cache-Control", "no-store, must-revalidate");
  s_srv.sendHeader("Pragma", "no-cache");
  s_srv.send(200, "text/html; charset=utf-8", page(body));
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

  // --- 开机画面（欢迎屏）---------------------------------------------------
  // 16 ASCII glyphs is about what fits on the 160 px (landscape) / 128 px
  // (portrait) panel - the label clips instead of wrapping, so keep it short.
  b += F("<label>开机标题（英文/数字，最多 16 字符，留空恢复默认）</label>"
         "<input type=\"text\" name=\"boottitle\" maxlength=\"16\" "
         "placeholder=\"" FW_NAME "\" value=\"");
  htmlEsc(b, cfg.bootTitle);
  b += F("\">");

  b += F("<label>开机问候语（中文，显示在标题下方）</label>"
         "<select name=\"bootline\">");
  for (int i = 0; i < settingsBootLineCount(); i++) {
    b += F("<option value=\"");
    b += i;
    b += '\"';
    if ((int)cfg.bootLine == i) b += F(" selected");
    b += '>';
    const char* gl = settingsBootLine(i);
    if (gl && gl[0]) htmlEsc(b, gl);
    else             b += F("关闭（显示版本号）");
    b += F("</option>");
  }
  b += F("</select>");

  b += F("<button type=\"submit\">保存</button></form>"
         "<div class=\"hint\">提示：数值越小变化越快；开机标题只能填英文/数字，"
         "中文请用上面的问候语。改动保存后重启可见。</div>");
  // --- 开机图片（从手机上传）---------------------------------------------
#if USE_SPLASH && USE_SPLASH_UPLOAD
  if (s_srv.hasArg("done"))
    b += F("<p class=\"hint ok\">开机图片已经存进宠物，下次开机先显示它。</p>");

  // The photo never travels: this page scales it to the panel size in a canvas
  // and posts the raw RGB565 bytes (40960 B, the layout splash_map[] and
  // tools/gen_splash.ps1 use), so the pet needs no decoder and no room for one.
  b += F("<h2>开机图片</h2><p class=\"sub\">选一张图片，页面会先在手机里把它"
         "缩放成 160×128 再上传（原图不传，流量很小）。</p>"
         "<p><input type=\"file\" id=\"pic\" accept=\"image/*\"></p>"
         "<label class=\"ck\"><input type=\"radio\" name=\"fit\" value=\"cover\" checked>"
         "铺满全屏（裁掉多出来的边）</label>"
         "<label class=\"ck\"><input type=\"radio\" name=\"fit\" value=\"contain\">"
         "完整显示（四周留底色）</label>"
         "<canvas id=\"cv\" width=\"160\" height=\"128\"></canvas>"
         "<button type=\"button\" id=\"up\" disabled>上传为开机图片</button>");

  const size_t have = splashStoreImageSize();
  b += F("<p class=\"hint\">当前：");
  b += have ? F("<b class=\"ok\">手机上传的图片</b>（直接存在 Flash 里）")
            : F("<b>内置默认图</b>（" FW_NAME " 自带）");
  b += F("</p>");
  if (have)
    b += F("<form method=\"POST\" action=\"/splash-clear\""
           " onsubmit=\"return confirm('恢复成内置默认图？')\">"
           "<button>删掉它，恢复默认图</button></form>");

  b += F("<div class=\"hint\" id=\"st\">图片写进宠物的 Flash，下次上电（或按 RST）"
         "先显示它。上传完可以直接在这里重启宠物。</div>"
         "<form method=\"POST\" action=\"/reboot\">"
         "<button>重启宠物（WiFi 热点会断开）</button></form>"
         "<script>\n"
    "var W=160,H=128,cv=document.getElementById('cv'),cx=cv.getContext('2d'),src=null,dat=null;\n"
    "function st(t,c){var e=document.getElementById('st');e.className='hint '+(c||'');e.innerHTML=t;}\n"
    "function fit(){return document.querySelector('input[name=fit]:checked').value;}\n"
    // Halve until the picture is at most 2x the panel: one drawImage from a
    // 4000 px photo straight down to 160 px is the aliasing case browsers are
    // worst at, and this picture is the first thing anyone sees after a reset.
    "function shrink(o){var k=fit()=='cover'?Math.max(W/o.width,H/o.height):Math.min(W/o.width,H/o.height);\n"
    " var dw=Math.round(o.width*k),dh=Math.round(o.height*k),s=o,w=o.width,h=o.height,c;\n"
    " while(w/2>=dw&&h/2>=dh){c=document.createElement('canvas');c.width=w=w>>1;c.height=h=h>>1;\n"
    "  var g=c.getContext('2d');g.imageSmoothingQuality='high';g.drawImage(s,0,0,w,h);s=c;}\n"
    " cx.imageSmoothingQuality='high';cx.fillStyle='#080a14';cx.fillRect(0,0,W,H);\n"
    " cx.drawImage(s,Math.round((W-dw)/2),Math.round((H-dh)/2),dw,dh);\n"
    // RGB565, low byte first - byte for byte what splash_map[] holds in the
    // pet's flash, which is also what tools/gen_splash.ps1 writes.
    " var d=cx.getImageData(0,0,W,H).data,b=new Uint8Array(W*H*2);\n"
    " for(var i=0,j=0;i<d.length;i+=4,j+=2){var v=((d[i]&248)<<8)|((d[i+1]&252)<<3)|(d[i+2]>>3);\n"
    "  b[j]=v&255;b[j+1]=v>>8&255;}\n"
    " return b;}\n"
    "document.getElementById('pic').onchange=function(e){var f=e.target.files[0];if(!f)return;\n"
    " var u=URL.createObjectURL(f),im=new Image();\n"
    " im.onload=function(){URL.revokeObjectURL(u);src=im;dat=shrink(im);\n"
    "  document.getElementById('up').disabled=false;st('已经缩成 160×128，点下面的按钮上传','ok');};\n"
    " im.onerror=function(){URL.revokeObjectURL(u);st('这个格式浏览器打不开，换 PNG 或 JPG 试试','bad');};\n"
    " im.src=u;};\n"
    "var rs=document.querySelectorAll('input[name=fit]');\n"
    "for(var i=0;i<rs.length;i++)rs[i].onchange=function(){if(src)dat=shrink(src);};\n"
    // multipart/form-data, because that is the one body the core WebServer
    // streams to the pet instead of buffering it in RAM (see netconfig.cpp).
    "document.getElementById('up').onclick=function(){if(!dat)return;\n"
    " var fd=new FormData();\n"
    " fd.append('splash',new Blob([dat],{type:'application/octet-stream'}),'splash.bin');\n"
    " var x=new XMLHttpRequest();x.open('POST','/splash');\n"
    " x.upload.onprogress=function(e){st('上传中 '+Math.round(e.loaded/e.total*100)+'%');};\n"
    " x.onload=function(){if(x.status==200&&x.responseText=='OK')location.href='/?done=1';\n"
    "  else st('上传失败：宠物没有收全这张图，请再试一次','bad');};\n"
    " x.onerror=function(){st('上传断了，检查一下 WiFi 还连着吗','bad');};\n"
    " st('上传中');x.send(fd);};\n"
    "</script>");
#endif

  sendPage(b);
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

  // welcome ("boot") screen: free ASCII title + one of the built-in greetings
  copyAscii(cfg.bootTitle, sizeof(cfg.bootTitle),
            s_srv.hasArg("boottitle") ? s_srv.arg("boottitle").c_str() : "");
  cfg.bootLine = (uint8_t)argULong("bootline", 0, 0, settingsBootLineCount() - 1);

  if (cfg.talkMaxMs < cfg.talkMinMs) cfg.talkMaxMs = cfg.talkMinMs;
  if (cfg.exprMaxMs < cfg.exprMinMs) cfg.exprMaxMs = cfg.exprMinMs;
  settingsSave();

  String b;
  b.reserve(300);
  b += F("<h1>已保存</h1><p class=\"sub\">设置已经写入，宠物马上就会用新的配置；"
         "开机标题 / 问候语会在下次开机时显示。</p>"
         "<p><a href=\"/\">← 返回继续设置</a></p>");
  sendPage(b);
}

#if USE_SPLASH && USE_SPLASH_UPLOAD
// The picture arrives as multipart/form-data because that is the one body the
// core's WebServer *streams* instead of reading it into a String (see
// Parsing.cpp: a plain POST body goes through readBytesWithTimeout + String,
// which truncates at the first 0x00 byte - useless for a bitmap). The file part
// reaches this callback in HTTP_UPLOAD_BUFLEN (1436) byte pieces, so a 40 KB
// picture is never held in RAM, and the last, partial piece arrives as one more
// UPLOAD_FILE_WRITE before UPLOAD_FILE_END closes the file.
// The callback takes no argument - on ESP32 the 4th parameter of on() is a plain
// THandlerFunction and the parser leaves its state in server.upload() (see
// WebServer.h) - but it is called once per piece of the multipart body.
static bool   s_picOk  = false;
static size_t s_picGot = 0;

static void handleSplashUpload() {
  HTTPUpload& up = s_srv.upload();
  switch (up.status) {
    case UPLOAD_FILE_START:
      s_picGot = 0;
      s_picOk  = splashUploadStart();
      break;
    case UPLOAD_FILE_WRITE:
      if (s_picOk && !splashUploadChunk(up.buf, up.currentSize)) {
        splashUploadAbort();               // too big, or the flash said no
        s_picOk = false;
      }
      if (s_picOk) s_picGot += up.currentSize;
      break;
    case UPLOAD_FILE_END:
      if (s_picOk) s_picOk = splashUploadFinish();
      Serial.printf("[net] boot picture: %s (%u bytes)\n",
                    s_picOk ? "saved" : "refused", (unsigned)s_picGot);
      break;
    default:                               // UPLOAD_FILE_ABORTED
      splashUploadAbort();
      s_picOk = false;
      Serial.println("[net] boot picture: aborted");
      break;
  }
}

// Runs once the body has been parsed, i.e. after the callback above has seen
// the whole picture. Plain text on purpose: the page posts with XMLHttpRequest
// and only needs to know whether to reload or to show the error.
static void handleSplashPost() {
  const bool ok = s_picOk;
  s_picOk = false;                    // nothing to report until the next upload
  s_srv.sendHeader("Cache-Control", "no-store, must-revalidate");
  s_srv.send(ok ? 200 : 500, "text/plain; charset=utf-8", ok ? "OK" : "BAD");
}

static void handleSplashClear() {
  String b;
  b.reserve(320);
  if (splashStoreDelete())
    b += F("<h1>已恢复默认图</h1><p class=\"sub\">手机上传的那张已经从 Flash 里删掉，"
           "下次开机显示 " FW_NAME " 自带的那张。</p>");
  else
    b += F("<h1>删除失败</h1><p class=\"sub\">Flash 里没有找到那张图。"
           "开机图片还是原来那张。</p>");
  b += F("<p><a href=\"/\">← 返回继续设置</a></p>");
  sendPage(b);
}

// The picture only shows at power-up, so the page offers the power cycle it
// takes to see it. The answer goes out first and the reset follows, otherwise
// the phone would only see the connection drop.
static void handleReboot() {
  String b;
  b.reserve(320);
  b += F("<h1>正在重启</h1><p class=\"sub\">宠物马上重启，WiFi 热点会跟着断开；"
         "重启后先显示开机图片。要再进这个页面，就重新打开宠物上的「网络」。</p>"
         "<p><a href=\"/\">← 返回设置页</a></p>");
  sendPage(b);
  delay(600);
  ESP.restart();
}
#endif

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
#if USE_SPLASH && USE_SPLASH_UPLOAD
  // The 4th argument is the upload callback: a multipart body goes to
  // handleSplashUpload piece by piece, the response to handleSplashPost.
  s_srv.on("/splash", HTTP_POST, handleSplashPost, handleSplashUpload);
  s_srv.on("/splash-clear", HTTP_POST, handleSplashClear);
  s_srv.on("/reboot", HTTP_POST, handleReboot);
#endif
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
#if USE_SPLASH && USE_SPLASH_UPLOAD
  splashStoreEnd();        // the picture stays in flash, the mount is let go
#endif
  Serial.println("[net] AP stopped");
}

bool        netConfigActive() { return s_active; }
const char* netConfigSsid()   { return s_ssid; }
const char* netConfigPass()   { return kPass; }
const char* netConfigIp()     { return s_ip; }
