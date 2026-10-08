// =====================================================================
//  netconfig.h  -  on demand WiFi access point + settings web page
//
//  When the user opens the \"网络\" menu entry the ESP32 raises its own
//  soft access point and serves a small configuration page. The phone
//  connects to the AP, opens the shown address in a browser and edits the
//  pet's settings; Save writes them straight to NVS (see settings.h).
//
//  The AP is only up while the config screen is shown - leaving the screen
//  calls netConfigEnd(), so the radio (and its power draw) stays off during
//  normal use.
// =====================================================================
#pragma once
#include <stdint.h>

bool        netConfigBegin();       // start AP + web server; false on failure
void        netConfigLoop();        // pump the HTTP server (call every frame)
void        netConfigEnd();         // stop the server and drop the AP
bool        netConfigActive();

const char* netConfigSsid();        // \"ESP32-Pet-XXXX\"
const char* netConfigPass();        // AP password
const char* netConfigIp();          // \"192.168.4.1\"
