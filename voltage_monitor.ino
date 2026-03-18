#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EmonLib.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

const char* ssid     = "sam";
const char* password = "123456789";

#define RELAY1_PIN   27
#define RELAY2_PIN   26
#define BUZZER_PIN   25
#define RED_LED      13
#define YELLOW_LED   12
#define GREEN_LED    14
#define VOLT_SENSOR  34

EnergyMonitor emon;
float current_Volts  = 0;
float filtered_Volts = 0;
unsigned long printPeriod    = 1000;
unsigned long previousMillis = 0;
unsigned long startTime      = 0;

String relay1State = "AUTO";
String relay2State = "AUTO";
bool   relay1Physical = false;
bool   relay2Physical = false;
bool   manualBuzzerOn = false;
bool   buzzerMuted    = false;

float underVoltThresh = 180.0;
float overVoltThresh  = 240.0;
float noACThresh      = 80.0;

int   underVoltCount = 0;
int   overVoltCount  = 0;
int   normalCount    = 0;
float peakVolt       = 0;
float lowestVolt     = 9999;
String voltageStatus = "Waiting...";
String lastAlert     = "None";

#define MAX_READINGS 60
float  voltageHistory[MAX_READINGS];
String timeHistory[MAX_READINGS];
int    historyIndex  = 0;
int    totalReadings = 0;

WebServer server(80);
LiquidCrystal_I2C lcd(0x27, 16, 2);
Preferences prefs;

void applyRelay1(bool on) {
  relay1Physical = on;
  digitalWrite(RELAY1_PIN, on ? LOW : HIGH);
  Serial.print("Relay1 => "); Serial.println(on ? "ON" : "OFF");
}

void applyRelay2(bool on) {
  relay2Physical = on;
  digitalWrite(RELAY2_PIN, on ? LOW : HIGH);
  Serial.print("Relay2 => "); Serial.println(on ? "ON" : "OFF");
}

void setLEDs(bool r, bool y, bool g) {
  digitalWrite(RED_LED, r);
  digitalWrite(YELLOW_LED, y);
  digitalWrite(GREEN_LED, g);
}

void triggerAlert() {
  if (relay1State == "AUTO") applyRelay1(false);
  if (relay2State == "AUTO") applyRelay2(false);
  if (!manualBuzzerOn && !buzzerMuted) digitalWrite(BUZZER_PIN, HIGH);
}

void clearAlert() {
  if (relay1State == "AUTO") applyRelay1(true);
  if (relay2State == "AUTO") applyRelay2(true);
  if (!manualBuzzerOn) digitalWrite(BUZZER_PIN, LOW);
}

String getUptime() {
  unsigned long t = (millis() - startTime) / 1000;
  char buf[12];
  sprintf(buf, "%02d:%02d:%02d", (int)(t/3600), (int)((t%3600)/60), (int)(t%60));
  return String(buf);
}

String statusColor() {
  if (filtered_Volts < noACThresh)      return "#555555";
  if (filtered_Volts < underVoltThresh) return "#ff8800";
  if (filtered_Volts > overVoltThresh)  return "#ff0000";
  return "#00cc44";
}

void handleLiveData() {
  float minV=9999, maxV=0, sumV=0;
  int cnt = min(totalReadings, MAX_READINGS);
  for(int i=0;i<cnt;i++){
    if(voltageHistory[i]>0){
      if(voltageHistory[i]<minV) minV=voltageHistory[i];
      if(voltageHistory[i]>maxV) maxV=voltageHistory[i];
      sumV+=voltageHistory[i];
    }
  }
  if(minV==9999) minV=0;
  float avgV = cnt>0 ? sumV/cnt : 0;

  String json = "{";
  json += "\"voltage\":"     + String(filtered_Volts,1) + ",";
  json += "\"status\":\""    + voltageStatus + "\",";
  json += "\"uptime\":\""    + getUptime() + "\",";
  json += "\"r1\":"          + String(relay1Physical?"true":"false") + ",";
  json += "\"r2\":"          + String(relay2Physical?"true":"false") + ",";
  json += "\"r1mode\":\""    + relay1State + "\",";
  json += "\"r2mode\":\""    + relay2State + "\",";
  json += "\"buzzer\":"      + String(manualBuzzerOn?"true":"false") + ",";
  json += "\"muted\":"       + String(buzzerMuted?"true":"false") + ",";
  json += "\"normal\":"      + String(normalCount) + ",";
  json += "\"under\":"       + String(underVoltCount) + ",";
  json += "\"over\":"        + String(overVoltCount) + ",";
  json += "\"peak\":"        + String(peakVolt,1) + ",";
  json += "\"lowest\":"      + String(lowestVolt<9999?lowestVolt:0,1) + ",";
  json += "\"avg\":"         + String(avgV,1) + ",";
  json += "\"lastAlert\":\"" + lastAlert + "\",";
  json += "\"readings\":"    + String(totalReadings) + ",";
  json += "\"rssi\":"        + String(WiFi.RSSI()) + ",";
  json += "\"heap\":"        + String(ESP.getFreeHeap()/1024) + ",";
  json += "\"ip\":\""        + WiFi.localIP().toString() + "\",";
  json += "\"underT\":"      + String(underVoltThresh,0) + ",";
  json += "\"overT\":"       + String(overVoltThresh,0);
  json += ",\"histV\":[";
  for(int i=0;i<cnt;i++){
    int idx=(historyIndex-cnt+i+MAX_READINGS)%MAX_READINGS;
    json += String(voltageHistory[idx],1);
    if(i<cnt-1) json+=",";
  }
  json += "],\"histT\":[";
  for(int i=0;i<cnt;i++){
    int idx=(historyIndex-cnt+i+MAX_READINGS)%MAX_READINGS;
    json += "\""+timeHistory[idx]+"\"";
    if(i<cnt-1) json+=",";
  }
  json += "]}";
  server.sendHeader("Access-Control-Allow-Origin","*");
  server.send(200,"application/json",json);
}

String getPage() {
  String p = "<!DOCTYPE html><html><head>";
  p += "<meta charset='UTF-8'>";
  p += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  p += "<title>Voltage Monitor Pro</title>";
  p += "<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>";
  p += "<style>";
  p += "*{box-sizing:border-box;margin:0;padding:0;}";
  p += "body{font-family:'Segoe UI',sans-serif;background:#050510;color:#e0e0e0;min-height:100vh;overflow-x:hidden;}";

  // Animated background
  p += ".bg-grid{position:fixed;top:0;left:0;width:100%;height:100%;";
  p += "background-image:linear-gradient(rgba(0,212,255,0.03) 1px,transparent 1px),linear-gradient(90deg,rgba(0,212,255,0.03) 1px,transparent 1px);";
  p += "background-size:50px 50px;pointer-events:none;z-index:0;}";
  p += ".content{position:relative;z-index:1;}";

  // Header
  p += ".hdr{background:linear-gradient(135deg,#0a0a2a,#0d1b4b);padding:12px 20px;";
  p += "border-bottom:1px solid #00d4ff33;display:flex;align-items:center;justify-content:space-between;";
  p += "position:sticky;top:0;z-index:99;backdrop-filter:blur(10px);}";
  p += ".hdr-left h1{color:#00d4ff;font-size:18px;letter-spacing:4px;font-weight:900;}";
  p += ".hdr-left p{color:#444;font-size:10px;margin-top:2px;}";
  p += ".hdr-right{display:flex;gap:10px;align-items:center;}";
  p += ".hdr-badge{padding:4px 10px;border-radius:20px;font-size:11px;font-weight:bold;}";
  p += ".badge-live{background:#003300;border:1px solid #00cc44;color:#00cc44;}";
  p += ".badge-wifi{background:#001133;border:1px solid #0088ff;color:#0088ff;}";
  p += ".pulse{display:inline-block;width:7px;height:7px;border-radius:50%;margin-right:4px;animation:pulse 1.5s infinite;}";
  p += ".pulse-green{background:#00cc44;box-shadow:0 0 6px #00cc44;}";
  p += ".pulse-blue{background:#0088ff;box-shadow:0 0 6px #0088ff;}";
  p += "@keyframes pulse{0%,100%{transform:scale(1);opacity:1}50%{transform:scale(1.3);opacity:0.5}}";

  // Wrap
  p += ".wrap{max-width:900px;margin:0 auto;padding:12px;}";

  // Voltage card
  p += ".vcard{border-radius:20px;padding:25px;text-align:center;margin:10px 0;";
  p += "background:linear-gradient(135deg,#0a0a2a,#0d1b3a);";
  p += "border:1px solid #1e3a5f;position:relative;overflow:hidden;}";
  p += ".vcard::before{content:'';position:absolute;top:-50%;left:-50%;width:200%;height:200%;";
  p += "background:radial-gradient(circle,rgba(0,212,255,0.04) 0%,transparent 60%);pointer-events:none;}";
  p += ".vnum{font-size:90px;font-weight:900;line-height:1;transition:color 0.5s,text-shadow 0.5s;font-variant-numeric:tabular-nums;}";
  p += ".vunit{font-size:32px;color:#aaa;}";
  p += ".sbadge{display:inline-block;padding:8px 28px;border-radius:30px;font-size:16px;";
  p += "font-weight:bold;margin:12px 0;transition:all 0.5s;letter-spacing:1px;}";
  p += ".pbar{background:#0d1525;border-radius:10px;height:24px;position:relative;overflow:hidden;margin:12px 0;border:1px solid #1e3a5f;}";
  p += ".pfill{height:100%;border-radius:10px;transition:width 1s ease,background 0.5s;}";
  p += ".plabel{position:absolute;right:10px;top:4px;font-size:12px;color:white;font-weight:bold;}";
  p += ".pmarks{display:flex;justify-content:space-between;font-size:10px;color:#333;margin-top:4px;}";

  // Relay big buttons
  p += ".relay-big{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin:10px 0;}";
  p += ".relay-btn-card{border-radius:16px;padding:18px;text-align:center;border:2px solid #1e3a5f;";
  p += "background:#0a0a2a;transition:all 0.3s;cursor:pointer;}";
  p += ".relay-btn-card.r-on{border-color:#00cc44;background:#001a00;box-shadow:0 0 20px #00cc4433;}";
  p += ".relay-btn-card.r-off{border-color:#ff3333;background:#1a0000;box-shadow:0 0 20px #ff333333;}";
  p += ".relay-btn-card.r-auto{border-color:#0088ff;background:#00001a;box-shadow:0 0 20px #0088ff33;}";
  p += ".relay-ch{font-size:12px;color:#555;text-transform:uppercase;letter-spacing:2px;}";
  p += ".relay-gpio{font-size:11px;color:#333;margin:3px 0;}";
  p += ".relay-status{font-size:24px;font-weight:900;margin:8px 0;transition:color 0.3s;}";
  p += ".relay-mode{font-size:11px;margin:5px 0;}";
  p += ".relay-btns-row{display:flex;gap:4px;justify-content:center;margin-top:10px;}";
  p += ".rb{padding:6px 12px;border-radius:8px;font-size:11px;text-decoration:none;color:white;font-weight:bold;border:1px solid transparent;transition:all 0.2s;}";
  p += ".rb:hover{transform:scale(1.08);filter:brightness(1.3);}";
  p += ".rbauto{background:#003388;}";
  p += ".rbon{background:#005500;}";
  p += ".rboff{background:#880000;}";
  p += ".rbact{border-color:white!important;filter:brightness(1.4);}";

  // Stats grid
  p += ".stats-row{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin:10px 0;}";
  p += ".stat-card{background:#0a0a2a;border-radius:14px;padding:14px;text-align:center;border:1px solid #1e3a5f;transition:transform 0.2s;}";
  p += ".stat-card:hover{transform:translateY(-2px);}";
  p += ".sval{font-size:26px;font-weight:bold;transition:all 0.3s;}";
  p += ".slbl{font-size:9px;color:#444;margin-top:4px;text-transform:uppercase;letter-spacing:1px;}";

  // Tabs
  p += ".tabs{display:flex;gap:6px;margin:12px 0;overflow-x:auto;padding-bottom:2px;}";
  p += ".tab{padding:9px 18px;border-radius:10px;font-size:13px;cursor:pointer;";
  p += "background:#0a0a2a;color:#555;border:1px solid #1e3a5f;white-space:nowrap;transition:all 0.2s;}";
  p += ".tab:hover{color:#aaa;border-color:#00d4ff55;}";
  p += ".tab.active{background:linear-gradient(135deg,#003388,#0055cc);color:white;border-color:#0088ff;}";
  p += ".tab-content{display:none;}";
  p += ".tab-content.active{display:block;}";

  // Chart card
  p += ".ccard{background:#0a0a2a;border-radius:16px;padding:16px;margin:8px 0;border:1px solid #1e3a5f;}";
  p += ".ctitle{color:#555;font-size:11px;text-transform:uppercase;letter-spacing:2px;margin-bottom:12px;}";

  // Info rows
  p += ".icard{background:#0a0a2a;border-radius:14px;padding:14px;margin:8px 0;border:1px solid #1e3a5f;}";
  p += ".irow{display:flex;justify-content:space-between;align-items:center;padding:8px 0;border-bottom:1px solid #0d1525;}";
  p += ".irow:last-child{border:none;}";
  p += ".ilbl{font-size:12px;color:#444;}";
  p += ".ival{font-size:13px;font-weight:bold;}";

  // Buzzer
  p += ".buz-display{font-size:60px;text-align:center;padding:20px;transition:all 0.3s;}";
  p += ".buz-btns{display:flex;justify-content:center;gap:12px;flex-wrap:wrap;margin:10px 0;}";
  p += ".bbtn{padding:14px 32px;border-radius:14px;font-size:15px;text-decoration:none;color:white;font-weight:bold;transition:all 0.2s;border:1px solid transparent;}";
  p += ".bbtn:hover{transform:scale(1.05);filter:brightness(1.2);}";
  p += ".bon{background:linear-gradient(135deg,#cc4400,#ff7700);border-color:#ff8800;}";
  p += ".boff{background:linear-gradient(135deg,#004400,#00aa00);border-color:#00cc44;}";
  p += ".bmute{background:linear-gradient(135deg,#222,#444);border-color:#666;}";

  // Settings
  p += ".form-row{display:flex;align-items:center;justify-content:space-between;padding:12px 0;border-bottom:1px solid #0d1525;}";
  p += ".form-row:last-child{border:none;}";
  p += ".form-lbl{font-size:13px;color:#aaa;}";
  p += ".form-inp{background:#050510;border:1px solid #1e3a5f;color:#00d4ff;padding:8px 12px;border-radius:10px;width:110px;font-size:15px;text-align:center;font-weight:bold;}";
  p += ".form-inp:focus{outline:none;border-color:#0088ff;box-shadow:0 0 10px #0088ff44;}";
  p += ".save-btn{display:block;width:100%;padding:14px;background:linear-gradient(135deg,#003388,#0055cc);color:white;border:none;border-radius:12px;font-size:15px;font-weight:bold;cursor:pointer;margin-top:12px;letter-spacing:1px;}";
  p += ".save-btn:hover{background:linear-gradient(135deg,#0044aa,#0077ff);transform:scale(1.02);}";

  // Reset btns
  p += ".reset-btns{display:flex;gap:10px;flex-wrap:wrap;justify-content:center;margin:10px 0;}";
  p += ".rst-btn{padding:12px 24px;border-radius:12px;font-size:14px;text-decoration:none;color:white;font-weight:bold;transition:all 0.2s;border:1px solid;}";
  p += ".rst-btn:hover{transform:scale(1.05);}";

  // Notification toast
  p += ".toast{position:fixed;bottom:20px;right:20px;padding:12px 24px;border-radius:12px;font-size:14px;font-weight:bold;z-index:999;opacity:0;transition:opacity 0.3s;pointer-events:none;}";
  p += ".toast.show{opacity:1;}";
  p += ".toast-ok{background:#003300;border:1px solid #00cc44;color:#00cc44;}";
  p += ".toast-err{background:#1a0000;border:1px solid #ff3333;color:#ff3333;}";

  // Easter egg
  p += ".secret{position:fixed;top:0;left:0;width:100%;height:100%;background:#000;z-index:9999;display:none;align-items:center;justify-content:center;flex-direction:column;}";
  p += ".secret.show{display:flex;}";
  p += ".secret h2{color:#00d4ff;font-size:48px;animation:glow 1s infinite alternate;}";
  p += "@keyframes glow{from{text-shadow:0 0 10px #00d4ff}to{text-shadow:0 0 40px #00d4ff,0 0 60px #0088ff}}";
  p += ".secret p{color:#aaa;margin-top:10px;}";
  p += ".secret button{margin-top:20px;padding:10px 30px;background:#003388;color:white;border:none;border-radius:10px;font-size:16px;cursor:pointer;}";

  // Konami hint
  p += ".konami-hint{position:fixed;bottom:10px;left:50%;transform:translateX(-50%);";
  p += "color:#111;font-size:10px;letter-spacing:2px;pointer-events:none;}";

  p += ".footer{text-align:center;color:#111;font-size:10px;padding:20px;letter-spacing:2px;}";
  p += "</style></head><body>";

  p += "<div class='bg-grid'></div>";
  p += "<div class='content'>";

  // Header
  p += "<div class='hdr'>";
  p += "<div class='hdr-left'><h1>VOLTAGE MONITOR PRO</h1>";
  p += "<p id='hdr-sub'>Initializing...</p></div>";
  p += "<div class='hdr-right'>";
  p += "<div class='hdr-badge badge-wifi'><span class='pulse pulse-blue'></span><span id='hdr-ip'>"+WiFi.localIP().toString()+"</span></div>";
  p += "<div class='hdr-badge badge-live'><span class='pulse pulse-green'></span>LIVE</div>";
  p += "</div></div>";

  p += "<div class='wrap'>";

  // Voltage card
  p += "<div class='vcard' id='vcard'>";
  p += "<div class='vnum' id='vnum'>---<span class='vunit'>V</span></div>";
  p += "<div class='sbadge' id='sbadge'>Connecting...</div>";
  p += "<div class='pbar'><div class='pfill' id='pfill' style='width:0%'></div>";
  p += "<span class='plabel' id='plabel'>0V</span></div>";
  p += "<div class='pmarks'><span>0V</span><span style='color:#ff8800' id='pm-under'>180V</span>";
  p += "<span style='color:#00cc44'>220V</span><span style='color:#ff3333' id='pm-over'>240V</span><span>300V</span></div>";
  p += "<div style='color:#333;font-size:11px;margin-top:8px;'>Readings: <span id='readings'>0</span> | Last alert: <span id='lastalert' style='color:#ff8800'>None</span></div>";
  p += "</div>";

  // Stats
  p += "<div class='stats-row'>";
  p += "<div class='stat-card'><div class='sval' id='stat-peak' style='color:#ff4444'>0V</div><div class='slbl'>Peak Voltage</div></div>";
  p += "<div class='stat-card'><div class='sval' id='stat-avg'  style='color:#00d4ff'>0V</div><div class='slbl'>Average</div></div>";
  p += "<div class='stat-card'><div class='sval' id='stat-min'  style='color:#44ff88'>0V</div><div class='slbl'>Minimum</div></div>";
  p += "</div>";
  p += "<div class='stats-row'>";
  p += "<div class='stat-card'><div class='sval' id='stat-normal' style='color:#00cc44'>0</div><div class='slbl'>Normal Events</div></div>";
  p += "<div class='stat-card'><div class='sval' id='stat-under'  style='color:#ff8800'>0</div><div class='slbl'>Under Voltage</div></div>";
  p += "<div class='stat-card'><div class='sval' id='stat-over'   style='color:#ff3333'>0</div><div class='slbl'>Over Voltage</div></div>";
  p += "</div>";

  // Tabs
  p += "<div class='tabs'>";
  p += "<div class='tab active' onclick='showTab(\"chart\",this)'>Chart</div>";
  p += "<div class='tab' onclick='showTab(\"relay\",this)'>Relays</div>";
  p += "<div class='tab' onclick='showTab(\"buzzer\",this)'>Buzzer</div>";
  p += "<div class='tab' onclick='showTab(\"settings\",this)'>Settings</div>";
  p += "<div class='tab' onclick='showTab(\"log\",this)'>System Log</div>";
  p += "</div>";

  // Chart tab
  p += "<div id='tab-chart' class='tab-content active'>";
  p += "<div class='ccard'><div class='ctitle'>Voltage History — Live</div>";
  p += "<canvas id='vc' height='160'></canvas></div></div>";

  // Relay tab
  String r1a = relay1State=="AUTO"?" rbact":"";
  String r1o = relay1State=="ON"  ?" rbact":"";
  String r1f = relay1State=="OFF" ?" rbact":"";
  String r1cls = relay1State=="ON"?"r-on":relay1State=="OFF"?"r-off":"r-auto";
  String r2a = relay2State=="AUTO"?" rbact":"";
  String r2o = relay2State=="ON"  ?" rbact":"";
  String r2f = relay2State=="OFF" ?" rbact":"";
  String r2cls = relay2State=="ON"?"r-on":relay2State=="OFF"?"r-off":"r-auto";

  p += "<div id='tab-relay' class='tab-content'>";
  p += "<div class='relay-big'>";

  // CH1
  p += "<div class='relay-btn-card "+r1cls+"' id='r1card'>";
  p += "<div class='relay-ch'>Channel 1</div>";
  p += "<div class='relay-gpio'>GPIO 27</div>";
  p += "<div class='relay-status' id='r1status'>"+(relay1Physical?String("ON"):String("OFF"))+"</div>";
  p += "<div class='relay-mode' id='r1mode'>Mode: "+relay1State+"</div>";
  p += "<div class='relay-btns-row'>";
  p += "<a href='/r1auto' class='rb rbauto"+r1a+"' onclick='showToast(\"CH1 set to AUTO\",true)'>Auto</a>";
  p += "<a href='/r1on'   class='rb rbon"+r1o+"'   onclick='showToast(\"CH1 ON\",true)'>ON</a>";
  p += "<a href='/r1off'  class='rb rboff"+r1f+"'  onclick='showToast(\"CH1 OFF\",false)'>OFF</a>";
  p += "</div></div>";

  // CH2
  p += "<div class='relay-btn-card "+r2cls+"' id='r2card'>";
  p += "<div class='relay-ch'>Channel 2</div>";
  p += "<div class='relay-gpio'>GPIO 26</div>";
  p += "<div class='relay-status' id='r2status'>"+(relay2Physical?String("ON"):String("OFF"))+"</div>";
  p += "<div class='relay-mode' id='r2mode'>Mode: "+relay2State+"</div>";
  p += "<div class='relay-btns-row'>";
  p += "<a href='/r2auto' class='rb rbauto"+r2a+"' onclick='showToast(\"CH2 set to AUTO\",true)'>Auto</a>";
  p += "<a href='/r2on'   class='rb rbon"+r2o+"'   onclick='showToast(\"CH2 ON\",true)'>ON</a>";
  p += "<a href='/r2off'  class='rb rboff"+r2f+"'  onclick='showToast(\"CH2 OFF\",false)'>OFF</a>";
  p += "</div></div>";
  p += "</div>";

  // Both control
  p += "<div class='icard' style='text-align:center;'>";
  p += "<div style='color:#444;font-size:11px;text-transform:uppercase;letter-spacing:2px;margin-bottom:12px;'>Both Channels</div>";
  p += "<div style='display:flex;gap:8px;justify-content:center;flex-wrap:wrap;'>";
  p += "<a href='/bothon'   class='rb rbon'   style='padding:10px 20px;' onclick='showToast(\"Both Relays ON\",true)'>Both ON</a>";
  p += "<a href='/bothauto' class='rb rbauto' style='padding:10px 20px;' onclick='showToast(\"Both set to AUTO\",true)'>Both AUTO</a>";
  p += "<a href='/bothoff'  class='rb rboff'  style='padding:10px 20px;' onclick='showToast(\"Both Relays OFF\",false)'>Both OFF</a>";
  p += "</div></div></div>";

  // Buzzer tab
  p += "<div id='tab-buzzer' class='tab-content'>";
  p += "<div class='icard'>";
  p += "<div class='buz-display' id='buz-icon' style='color:#555;'>SILENT</div>";
  p += "<div style='text-align:center;color:#333;font-size:12px;margin-bottom:15px;'>Auto Mute: <span id='buz-mute' style='color:#aaa;'>OFF</span></div>";
  p += "<div class='buz-btns'>";
  p += "<a href='/buzzeron'  class='bbtn bon'  onclick='showToast(\"Buzzer ON!\",true)'>Buzzer ON</a>";
  p += "<a href='/buzzeroff' class='bbtn boff' onclick='showToast(\"Buzzer OFF\",true)'>Buzzer OFF</a>";
  p += "</div>";
  p += "<div class='buz-btns'>";
  p += "<a href='/buzzermute' class='bbtn bmute' id='mute-btn'>Mute Auto Buzzer</a>";
  p += "</div>";
  p += "<div style='color:#222;font-size:11px;text-align:center;margin-top:10px;'>Mute silences automatic alerts only</div>";
  p += "</div></div>";

  // Settings tab
  p += "<div id='tab-settings' class='tab-content'>";
  p += "<div class='icard'>";
  p += "<div style='color:#555;font-size:11px;text-transform:uppercase;letter-spacing:2px;margin-bottom:12px;'>Protection Thresholds</div>";
  p += "<form action='/savesettings' method='GET' onsubmit='showToast(\"Settings Saved!\",true)'>";
  p += "<div class='form-row'><span class='form-lbl'>Under Voltage Limit</span>";
  p += "<input class='form-inp' type='number' name='under' value='"+String(underVoltThresh,0)+"' min='100' max='200'></div>";
  p += "<div class='form-row'><span class='form-lbl'>Over Voltage Limit</span>";
  p += "<input class='form-inp' type='number' name='over' value='"+String(overVoltThresh,0)+"' min='220' max='280'></div>";
  p += "<div class='form-row'><span class='form-lbl'>No AC Detection Threshold</span>";
  p += "<input class='form-inp' type='number' name='noac' value='"+String(noACThresh,0)+"' min='10' max='100'></div>";
  p += "<button class='save-btn' type='submit'>SAVE SETTINGS</button>";
  p += "</form></div>";
  p += "<div class='icard' style='margin-top:8px;'>";
  p += "<div style='color:#555;font-size:11px;text-transform:uppercase;letter-spacing:2px;margin-bottom:12px;'>Reset Options</div>";
  p += "<div class='reset-btns'>";
  p += "<a href='/resetstats' class='rst-btn' style='background:#1a0000;border-color:#ff3333;color:#ff3333;' onclick='showToast(\"Stats Reset!\",true)'>Reset Statistics</a>";
  p += "<a href='/resetpeak'  class='rst-btn' style='background:#001a00;border-color:#00cc44;color:#00cc44;' onclick='showToast(\"Peak Reset!\",true)'>Reset Peak Values</a>";
  p += "</div></div></div>";

  // Log tab
  p += "<div id='tab-log' class='tab-content'>";
  p += "<div class='icard'>";
  p += "<div style='color:#555;font-size:11px;text-transform:uppercase;letter-spacing:2px;margin-bottom:8px;'>System Information</div>";
  p += "<div class='irow'><span class='ilbl'>IP Address</span><span class='ival' style='color:#0088ff'>"+WiFi.localIP().toString()+"</span></div>";
  p += "<div class='irow'><span class='ilbl'>WiFi Network</span><span class='ival' style='color:#00cc44'>"+String(ssid)+"</span></div>";
  p += "<div class='irow'><span class='ilbl'>Signal Strength</span><span class='ival' id='log-rssi'>---</span></div>";
  p += "<div class='irow'><span class='ilbl'>Uptime</span><span class='ival' id='log-uptime'>---</span></div>";
  p += "<div class='irow'><span class='ilbl'>Free Heap</span><span class='ival' id='log-heap'>---</span></div>";
  p += "<div class='irow'><span class='ilbl'>Total Readings</span><span class='ival' id='log-readings'>---</span></div>";
  p += "<div class='irow'><span class='ilbl'>Last Alert</span><span class='ival' id='log-alert' style='color:#ff8800'>None</span></div>";
  p += "<div class='irow'><span class='ilbl'>Under Threshold</span><span class='ival' id='log-under'>"+String(underVoltThresh,0)+"V</span></div>";
  p += "<div class='irow'><span class='ilbl'>Over Threshold</span><span class='ival' id='log-over'>"+String(overVoltThresh,0)+"V</span></div>";
  p += "<div class='irow'><span class='ilbl'>Relay 1 (D27)</span><span class='ival' id='log-r1'>---</span></div>";
  p += "<div class='irow'><span class='ilbl'>Relay 2 (D26)</span><span class='ival' id='log-r2'>---</span></div>";
  p += "</div></div>";

  p += "<div class='footer'>ESP32 VOLTAGE MONITOR PRO &bull; LIVE DATA</div>";
  p += "</div>";

  // Toast
  p += "<div class='toast' id='toast'></div>";

  // Easter egg overlay
  p += "<div class='secret' id='secret'>";
  p += "<h2>POWER GUARD</h2>";
  p += "<p style='color:#00d4ff;font-size:14px;margin-top:15px;'>You found the secret!</p>";
  p += "<p style='color:#555;font-size:12px;margin-top:8px;'>ESP32 Voltage Monitor by SAM</p>";
  p += "<div id='egg-volt' style='font-size:60px;font-weight:900;color:#00cc44;margin:20px;'>---V</div>";
  p += "<p style='color:#333;font-size:11px;'>Konami Code Activated</p>";
  p += "<button onclick='closeSecret()'>Close</button>";
  p += "</div>";

  p += "<div class='konami-hint'>Try: ↑↑↓↓←→←→BA</div>";
  p += "</div>";

  // JavaScript
  p += "<script>";

  // Tab system
  p += "function showTab(n,el){";
  p += "document.querySelectorAll('.tab-content').forEach(t=>t.classList.remove('active'));";
  p += "document.querySelectorAll('.tab').forEach(t=>t.classList.remove('active'));";
  p += "document.getElementById('tab-'+n).classList.add('active');";
  p += "if(el)el.classList.add('active');}";

  // Toast
  p += "function showToast(msg,ok){";
  p += "var t=document.getElementById('toast');";
  p += "t.innerText=msg;t.className='toast '+(ok?'toast-ok':'toast-err')+' show';";
  p += "setTimeout(()=>t.classList.remove('show'),2500);}";

  // Color helper
  p += "function getColor(v){";
  p += "if(v<80)  return '#555555';";
  p += "if(v<180) return '#ff8800';";
  p += "if(v>240) return '#ff0000';";
  p += "return '#00cc44';}";

  // Chart
  p += "var chartInst=new Chart(document.getElementById('vc').getContext('2d'),{";
  p += "type:'line',data:{labels:[],datasets:[";
  p += "{label:'Voltage',data:[],borderColor:'#00d4ff',backgroundColor:'rgba(0,212,255,0.06)',pointRadius:2,borderWidth:2,tension:0.4,fill:true},";
  p += "{label:'Max Limit',data:[],borderColor:'rgba(255,50,50,0.4)',borderWidth:1,borderDash:[6,4],pointRadius:0,fill:false},";
  p += "{label:'Min Limit',data:[],borderColor:'rgba(255,136,0,0.4)',borderWidth:1,borderDash:[6,4],pointRadius:0,fill:false}]},";
  p += "options:{responsive:true,animation:{duration:300},";
  p += "plugins:{legend:{labels:{color:'#333',font:{size:10}}}},";
  p += "scales:{x:{ticks:{color:'#333',maxRotation:45,font:{size:9}},grid:{color:'#0d1525'}},";
  p += "y:{ticks:{color:'#555'},grid:{color:'#0d1525'},min:0,max:300}}}});";

  // Live fetch
  p += "function fetchLive(){";
  p += "fetch('/livedata').then(r=>r.json()).then(d=>{";

  // Voltage update
  p += "var sc=getColor(d.voltage);";
  p += "var vn=document.getElementById('vnum');";
  p += "vn.style.color=sc;vn.style.textShadow='0 0 30px '+sc;";
  p += "vn.innerHTML=(d.voltage<80?'---':d.voltage.toFixed(1))+'<span class=\"vunit\">V</span>';";
  p += "var sb=document.getElementById('sbadge');";
  p += "sb.style.background=sc;sb.style.boxShadow='0 0 20px '+sc+'88';sb.innerText=d.status;";
  p += "var pct=Math.min((d.voltage/300)*100,100);";
  p += "document.getElementById('pfill').style.width=pct+'%';";
  p += "document.getElementById('pfill').style.background=sc;";
  p += "document.getElementById('plabel').innerText=d.voltage.toFixed(0)+'V';";
  p += "document.getElementById('readings').innerText=d.readings;";
  p += "document.getElementById('lastalert').innerText=d.lastAlert;";

  // Header
  p += "document.getElementById('hdr-sub').innerText='Uptime: '+d.uptime+' | Readings: '+d.readings;";

  // Stats
  p += "document.getElementById('stat-peak').innerText=d.peak+'V';";
  p += "document.getElementById('stat-avg').innerText=d.avg+'V';";
  p += "document.getElementById('stat-min').innerText=d.lowest+'V';";
  p += "document.getElementById('stat-normal').innerText=d.normal;";
  p += "document.getElementById('stat-under').innerText=d.under;";
  p += "document.getElementById('stat-over').innerText=d.over;";

  // Relay cards
  p += "var r1on=d.r1;var r1m=d.r1mode;";
  p += "var r1c=document.getElementById('r1card');";
  p += "r1c.className='relay-btn-card '+(r1m=='ON'?'r-on':r1m=='OFF'?'r-off':'r-auto');";
  p += "var r1s=document.getElementById('r1status');";
  p += "r1s.innerText=r1on?'ON':'OFF';r1s.style.color=r1on?'#00cc44':'#ff3333';";
  p += "document.getElementById('r1mode').innerText='Mode: '+r1m;";

  p += "var r2on=d.r2;var r2m=d.r2mode;";
  p += "var r2c=document.getElementById('r2card');";
  p += "r2c.className='relay-btn-card '+(r2m=='ON'?'r-on':r2m=='OFF'?'r-off':'r-auto');";
  p += "var r2s=document.getElementById('r2status');";
  p += "r2s.innerText=r2on?'ON':'OFF';r2s.style.color=r2on?'#00cc44':'#ff3333';";
  p += "document.getElementById('r2mode').innerText='Mode: '+r2m;";

  // Buzzer
  p += "var bi=document.getElementById('buz-icon');";
  p += "bi.innerText=d.buzzer?'ACTIVE':'SILENT';";
  p += "bi.style.color=d.buzzer?'#ff3333':'#00cc44';";
  p += "document.getElementById('buz-mute').innerText=d.muted?'ON':'OFF';";
  p += "document.getElementById('mute-btn').innerText=d.muted?'Unmute Auto Buzzer':'Mute Auto Buzzer';";

  // Log
  p += "document.getElementById('log-rssi').innerText=d.rssi+'dBm';";
  p += "document.getElementById('log-uptime').innerText=d.uptime;";
  p += "document.getElementById('log-heap').innerText=d.heap+'KB';";
  p += "document.getElementById('log-readings').innerText=d.readings;";
  p += "document.getElementById('log-alert').innerText=d.lastAlert;";
  p += "document.getElementById('log-r1').innerText=d.r1?'ON':'OFF';";
  p += "document.getElementById('log-r2').innerText=d.r2?'ON':'OFF';";

  // Easter egg voltage
  p += "var ev=document.getElementById('egg-volt');";
  p += "if(ev)ev.innerText=(d.voltage<80?'---':d.voltage.toFixed(1))+'V';";

  // Chart update
  p += "var colors=d.histV.map(v=>getColor(v));";
  p += "chartInst.data.labels=d.histT;";
  p += "chartInst.data.datasets[0].data=d.histV;";
  p += "chartInst.data.datasets[0].pointBackgroundColor=colors;";
  p += "chartInst.data.datasets[1].data=Array(d.histT.length).fill(d.overT);";
  p += "chartInst.data.datasets[2].data=Array(d.histT.length).fill(d.underT);";
  p += "chartInst.update();";

  p += "}).catch(e=>console.log('err',e));}";

  p += "fetchLive();setInterval(fetchLive,1000);";

  // Easter egg - Konami code
  p += "var kseq=[38,38,40,40,37,39,37,39,66,65];var kpos=0;";
  p += "document.addEventListener('keydown',function(e){";
  p += "if(e.keyCode===kseq[kpos]){kpos++;";
  p += "if(kpos===kseq.length){kpos=0;";
  p += "document.getElementById('secret').classList.add('show');}}";
  p += "else kpos=0;});";

  // Logo click easter egg (click 5 times fast)
  p += "var logoClicks=0;var logoTimer;";
  p += "document.querySelector('.hdr-left h1').addEventListener('click',function(){";
  p += "logoClicks++;clearTimeout(logoTimer);";
  p += "logoTimer=setTimeout(()=>logoClicks=0,1000);";
  p += "if(logoClicks>=5){logoClicks=0;showToast('Easter Egg Found! Try Konami Code!',true);}});";

  // Close secret
  p += "function closeSecret(){document.getElementById('secret').classList.remove('show');}";

  p += "</script></body></html>";
  return p;
}

void handleRoot()       { server.send(200,"text/html",getPage()); }
void handleR1Auto()     { relay1State="AUTO"; server.sendHeader("Location","/"); server.send(303); }
void handleR1On()       { relay1State="ON";  applyRelay1(true);  server.sendHeader("Location","/"); server.send(303); }
void handleR1Off()      { relay1State="OFF"; applyRelay1(false); server.sendHeader("Location","/"); server.send(303); }
void handleR2Auto()     { relay2State="AUTO"; server.sendHeader("Location","/"); server.send(303); }
void handleR2On()       { relay2State="ON";  applyRelay2(true);  server.sendHeader("Location","/"); server.send(303); }
void handleR2Off()      { relay2State="OFF"; applyRelay2(false); server.sendHeader("Location","/"); server.send(303); }
void handleBothOn()     { relay1State="ON";  relay2State="ON";   applyRelay1(true);  applyRelay2(true);  server.sendHeader("Location","/"); server.send(303); }
void handleBothOff()    { relay1State="OFF"; relay2State="OFF";  applyRelay1(false); applyRelay2(false); server.sendHeader("Location","/"); server.send(303); }
void handleBothAuto()   { relay1State="AUTO"; relay2State="AUTO"; server.sendHeader("Location","/"); server.send(303); }
void handleBuzzerOn()   { manualBuzzerOn=true;  digitalWrite(BUZZER_PIN,HIGH); server.sendHeader("Location","/"); server.send(303); }
void handleBuzzerOff()  { manualBuzzerOn=false; digitalWrite(BUZZER_PIN,LOW);  server.sendHeader("Location","/"); server.send(303); }
void handleBuzzerMute() { buzzerMuted=!buzzerMuted; server.sendHeader("Location","/"); server.send(303); }
void handleResetStats() { underVoltCount=0; overVoltCount=0; normalCount=0; server.sendHeader("Location","/"); server.send(303); }
void handleResetPeak()  { peakVolt=0; lowestVolt=9999; server.sendHeader("Location","/"); server.send(303); }

void handleSaveSettings() {
  if(server.hasArg("under")) underVoltThresh=server.arg("under").toFloat();
  if(server.hasArg("over"))  overVoltThresh =server.arg("over").toFloat();
  if(server.hasArg("noac"))  noACThresh     =server.arg("noac").toFloat();
  prefs.begin("vmon",false);
  prefs.putFloat("under",underVoltThresh);
  prefs.putFloat("over", overVoltThresh);
  prefs.putFloat("noac", noACThresh);
  prefs.end();
  server.sendHeader("Location","/"); server.send(303);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  startTime=millis();

  prefs.begin("vmon",true);
  underVoltThresh=prefs.getFloat("under",180.0);
  overVoltThresh =prefs.getFloat("over", 240.0);
  noACThresh     =prefs.getFloat("noac",  80.0);
  prefs.end();

  for(int i=0;i<MAX_READINGS;i++){voltageHistory[i]=0;timeHistory[i]="0";}

  Wire.begin(21,22);
  lcd.init();
  lcd.backlight();

  pinMode(RELAY1_PIN,OUTPUT); pinMode(RELAY2_PIN,OUTPUT);
  pinMode(BUZZER_PIN,OUTPUT); pinMode(RED_LED,OUTPUT);
  pinMode(YELLOW_LED,OUTPUT); pinMode(GREEN_LED,OUTPUT);

  digitalWrite(RELAY1_PIN,HIGH); digitalWrite(RELAY2_PIN,HIGH);
  digitalWrite(BUZZER_PIN,LOW);
  setLEDs(LOW,LOW,LOW);
  relay1Physical=false; relay2Physical=false;

  emon.voltage(VOLT_SENSOR,234.0,1.7);

  // WiFi connect with LCD status
  lcd.setCursor(0,0); lcd.print("Connecting WiFi ");
  lcd.setCursor(0,1); lcd.print("Please wait...  ");
  WiFi.begin(ssid,password);
  int tries=0;
  while(WiFi.status()!=WL_CONNECTED && tries<20){
    delay(500); Serial.print("."); tries++;
    lcd.setCursor(15,0);
    lcd.print(tries%2==0?"|":"-");
  }

  if(WiFi.status()==WL_CONNECTED){
    Serial.println("\nConnected! IP: "+WiFi.localIP().toString());
    // Show WiFi connected + IP for 10 seconds
    lcd.clear();
    lcd.setCursor(0,0); lcd.print("WiFi Connected! ");
    lcd.setCursor(0,1); lcd.print(WiFi.localIP());
    delay(10000);  // Show IP for 10 seconds
  } else {
    lcd.clear();
    lcd.setCursor(0,0); lcd.print("WiFi Failed!    ");
    lcd.setCursor(0,1); lcd.print("Check Settings  ");
    delay(3000);
  }

  server.on("/",             handleRoot);
  server.on("/livedata",     handleLiveData);
  server.on("/r1auto",       handleR1Auto);
  server.on("/r1on",         handleR1On);
  server.on("/r1off",        handleR1Off);
  server.on("/r2auto",       handleR2Auto);
  server.on("/r2on",         handleR2On);
  server.on("/r2off",        handleR2Off);
  server.on("/bothon",       handleBothOn);
  server.on("/bothoff",      handleBothOff);
  server.on("/bothauto",     handleBothAuto);
  server.on("/buzzeron",     handleBuzzerOn);
  server.on("/buzzeroff",    handleBuzzerOff);
  server.on("/buzzermute",   handleBuzzerMute);
  server.on("/resetstats",   handleResetStats);
  server.on("/resetpeak",    handleResetPeak);
  server.on("/savesettings", handleSaveSettings);
  server.begin();
  Serial.println("Server started!");

  lcd.clear();
  lcd.setCursor(0,0); lcd.print("Voltage Monitor ");
  delay(1500); lcd.clear();
  lcd.setCursor(0,0); lcd.print("Under:"+String(underVoltThresh,0)+"V");
  lcd.setCursor(0,1); lcd.print("Over :"+String(overVoltThresh,0)+"V");
  delay(2000); lcd.clear();
}

void loop() {
  server.handleClient();
  emon.calcVI(20,2000);
  current_Volts  = emon.Vrms;
  filtered_Volts = (filtered_Volts*0.7)+(current_Volts*0.3);

  if(millis()-previousMillis>=printPeriod){
    previousMillis=millis();
    unsigned long t=(millis()-startTime)/1000;
    voltageHistory[historyIndex]=filtered_Volts;
    timeHistory[historyIndex]=String(t)+"s";
    historyIndex=(historyIndex+1)%MAX_READINGS;
    if(totalReadings<MAX_READINGS) totalReadings++;

    if(filtered_Volts>noACThresh){
      if(filtered_Volts>peakVolt)   peakVolt=filtered_Volts;
      if(filtered_Volts<lowestVolt) lowestVolt=filtered_Volts;
    }

    lcd.setCursor(0,0);
    if(filtered_Volts<noACThresh){
      voltageStatus="No AC Connected";
      lcd.print("Volt: No AC     ");
      lcd.setCursor(0,1); lcd.print("Connect AC Wire ");
      if(relay1State=="AUTO") applyRelay1(false);
      if(relay2State=="AUTO") applyRelay2(false);
      if(!manualBuzzerOn) digitalWrite(BUZZER_PIN,LOW);
      setLEDs(LOW,LOW,LOW);
    } else if(filtered_Volts<underVoltThresh){
      voltageStatus="Under Voltage!";
      underVoltCount++;
      lastAlert="UnderV "+String(filtered_Volts,1)+"V";
      lcd.print("Volt:"+String(filtered_Volts,1)+"V     ");
      lcd.setCursor(0,1); lcd.print("Status:UnderVolt");
      setLEDs(LOW,HIGH,LOW); triggerAlert();
    } else if(filtered_Volts>overVoltThresh){
      voltageStatus="Over Voltage!";
      overVoltCount++;
      lastAlert="OverV "+String(filtered_Volts,1)+"V";
      lcd.print("Volt:"+String(filtered_Volts,1)+"V     ");
      lcd.setCursor(0,1); lcd.print("Status:OverVolt ");
      setLEDs(HIGH,LOW,LOW); triggerAlert();
    } else {
      voltageStatus="Normal";
      normalCount++;
      lcd.print("Volt:"+String(filtered_Volts,1)+"V     ");
      lcd.setCursor(0,1); lcd.print("Status:Normal   ");
      setLEDs(LOW,LOW,HIGH); clearAlert();
    }
    Serial.print("V:"+String(filtered_Volts,1));
    Serial.print(" R1:"+relay1State+"("+String(relay1Physical)+")");
    Serial.println(" R2:"+relay2State+"("+String(relay2Physical)+")");
  }
}