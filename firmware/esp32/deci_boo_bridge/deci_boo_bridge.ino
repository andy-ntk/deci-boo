#include <WiFi.h>
#include <WebServer.h>
#include "secrets.h"

// ====== UART กับ STM32 ======
#define STM_RX_PIN 18
#define STM_TX_PIN 17

WebServer server(80);

String stmLine = "";
String pcLine  = "";

int latestAmp   = 0;
int latestWarn  = 200;
int latestAlert = 400;
int latestState = 0; // 0=QUIET, 1=WARN, 2=ALERT
unsigned long lastStmMs = 0;
unsigned long lastWifiCheckMs = 0;

static const char INDEX_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html lang="th">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Deci-Boo! Dashboard</title>
<style>
  body { font-family: system-ui, sans-serif; background:#0f172a; color:#e2e8f0; margin:0; padding:20px; display:flex; justify-content:center; }
  .card { width:100%; max-width:440px; background:#1e293b; border-radius:16px; padding:22px; box-shadow:0 10px 30px rgba(0,0,0,.35); }
  .top { display:flex; justify-content:space-between; align-items:center; margin-bottom:16px; }
  h1 { font-size:20px; margin:0; }
  .pill { font-size:12px; padding:4px 10px; border-radius:999px; background:#334155; }
  .pill.ok { background:#14532d; color:#86efac; }
  .pill.err { background:#7f1d1d; color:#fca5a5; }
  .status { text-align:center; padding:22px; border-radius:12px; font-size:24px; font-weight:700; margin-bottom:18px; transition:.2s; }
  .s0 { background:#14532d; color:#4ade80; border:2px solid #22c55e; }
  .s1 { background:#713f12; color:#fde047; border:2px solid #eab308; }
  .s2 { background:#7f1d1d; color:#fca5a5; border:2px solid #ef4444; animation:blink .5s infinite; }
  @keyframes blink { 50% { opacity:.45; } }
  .row { display:flex; justify-content:space-between; margin:8px 0; font-size:15px; }
  .bar-bg { height:14px; background:#0f172a; border-radius:999px; overflow:hidden; margin:10px 0 18px; }
  .bar { height:100%; width:0%; background:#38bdf8; transition:width .15s; }
  .ctrl { border-top:1px solid #334155; padding-top:16px; }
  input[type=range] { width:100%; margin:10px 0; }
  .btns { display:flex; gap:8px; flex-wrap:wrap; margin-top:8px; }
  button { flex:1; min-width:70px; padding:9px; border:0; border-radius:8px; background:#3b82f6; color:#fff; font-weight:600; cursor:pointer; }
  button.preset { background:#334155; }
  button:hover { opacity:.9; }
</style>
</head>
<body>
<div class="card">
  <div class="top">
    <h1>Deci-Boo!</h1>
    <span id="conn" class="pill err">STM32 Offline</span>
  </div>

  <div id="badge" class="status s0">QUIET (GREEN)</div>

  <div class="row"><span>Sound Amp (ADC)</span><strong id="amp">0</strong></div>
  <div class="bar-bg"><div id="bar" class="bar"></div></div>

  <div class="row"><span>Warn Threshold (Yellow)</span><strong id="warn">200</strong></div>
  <div class="row"><span>Alert Threshold (Red)</span><strong id="alert">400</strong></div>

  <div class="ctrl">
    <div class="row"><span>Set Alert Threshold</span><strong id="sliderVal">400</strong></div>
    <input id="slider" type="range" min="100" max="3000" step="25" value="400">
    <button onclick="sendTh(slider.value)">Apply to STM32</button>
    <div class="btns">
      <button class="preset" onclick="setPreset(400)">400</button>
      <button class="preset" onclick="setPreset(1000)">1000</button>
      <button class="preset" onclick="setPreset(1500)">1500</button>
      <button class="preset" onclick="setPreset(2000)">2000</button>
    </div>
  </div>
</div>

<script>
const slider = document.getElementById('slider');
const sliderVal = document.getElementById('sliderVal');
let lockSliderUntil = 0;

slider.oninput = () => {
  lockSliderUntil = Date.now() + 5000;
  sliderVal.textContent = slider.value;
};

function setPreset(v) {
  slider.value = v;
  sliderVal.textContent = v;
  sendTh(v);
}

async function sendTh(v) {
  lockSliderUntil = Date.now() + 2500;
  await fetch('/api/set?th=' + encodeURIComponent(v));
}

async function poll() {
  try {
    const r = await fetch('/api/status');
    const d = await r.json();
    const conn = document.getElementById('conn');
    conn.textContent = d.online ? 'STM32 Online' : 'Waiting STM32...';
    conn.className = 'pill ' + (d.online ? 'ok' : 'err');

    document.getElementById('amp').textContent = d.amp;
    document.getElementById('warn').textContent = d.warn;
    document.getElementById('alert').textContent = d.alert;
    document.getElementById('bar').style.width = Math.min(100, (d.amp / 4095) * 100) + '%';

    if (Date.now() > lockSliderUntil) {
      slider.value = d.alert;
      sliderVal.textContent = d.alert;
    }

    const b = document.getElementById('badge');
    if (d.state === 0) { b.textContent = 'QUIET (GREEN)'; b.className = 'status s0'; }
    else if (d.state === 1) { b.textContent = 'LOUD (YELLOW)'; b.className = 'status s1'; }
    else { b.textContent = 'ALERT! (RED BLINK)'; b.className = 'status s2'; }
  } catch (e) {}
}
setInterval(poll, 400);
poll();
</script>
</body>
</html>
)rawliteral";

void handleRoot() {
  server.send(200, "text/html", INDEX_HTML);
}

void handleStatus() {
  bool online = (lastStmMs > 0) && ((millis() - lastStmMs) < 3000UL);
  char json[160];
  snprintf(json, sizeof(json),
           "{\"online\":%s,\"amp\":%d,\"warn\":%d,\"alert\":%d,\"state\":%d}",
           online ? "true" : "false", latestAmp, latestWarn, latestAlert, latestState);
  server.send(200, "application/json", json);
}

void handleSet() {
  if (!server.hasArg("th")) {
    server.send(400, "application/json", "{\"ok\":false}");
    return;
  }
  int th = server.arg("th").toInt();
  if (th < 100) th = 100;
  if (th > 3000) th = 3000;

  Serial1.print("SET_TH,");
  Serial1.print(th);
  Serial1.print('\n');
  Serial.printf("[WEB -> STM32] SET_TH,%d\n", th);

  char resp[64];
  snprintf(resp, sizeof(resp), "{\"ok\":true,\"alert\":%d}", th);
  server.send(200, "application/json", resp);
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting WiFi");
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < 15000UL) {
    delay(400);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi Connected! Open browser: http://");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi connect timeout (will keep retrying in loop)");
  }
}

void setup() {
  Serial.begin(115200);
  Serial1.begin(115200, SERIAL_8N1, STM_RX_PIN, STM_TX_PIN);
  delay(500);

  connectWiFi();

  server.on("/", handleRoot);
  server.on("/api/status", handleStatus);
  server.on("/api/set", handleSet);
  server.begin();
  Serial.println("WebServer started on port 80");
}

void loop() {
  while (Serial1.available()) {
    char c = (char)Serial1.read();
    if (c == '\n') {
      stmLine.trim();
      if (stmLine.length() > 0) {
        Serial.print("[STM32] ");
        Serial.println(stmLine);

        if (stmLine.startsWith("STAT,")) {
          int amp, warnTh, alertTh, state;
          if (sscanf(stmLine.c_str(), "STAT,%d,%d,%d,%d", &amp, &warnTh, &alertTh, &state) == 4) {
            latestAmp   = amp;
            latestWarn  = warnTh;
            latestAlert = alertTh;
            latestState = state;
            lastStmMs   = millis();
          }
        }
      }
      stmLine = "";
    } else if (c != '\r') {
      stmLine += c;
    }
  }

  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n') {
      pcLine.trim();
      if (pcLine.length() > 0) {
        Serial1.print(pcLine);
        Serial1.print('\n');
      }
      pcLine = "";
    } else if (c != '\r') {
      pcLine += c;
    }
  }

  server.handleClient();

  if (millis() - lastWifiCheckMs > 10000UL) {
    lastWifiCheckMs = millis();
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.reconnect();
    }
  }
}
