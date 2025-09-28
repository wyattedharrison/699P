#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <math.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#define RX_PIN          16
#define TX_PIN          17
#define UART_BAUDRATE   115200
#define WIFI_TIMEOUT    15000
#define POST_PERIOD_MS  1
#define Wifi_Enable_Pin 14

const char* SSID       = "BigMommaSix";
const char* PASS       = "BigDickSlangin8===D";
const char* WEBAPP_URL = "https://script.google.com/macros/s/AKfycbzRB5kRTuDU3dlqUVokS2orTb6J8BoIjkudVlq_eNcWCWpixTXBzmH1sCBWQWsQavBv6w/exec";

Adafruit_SSD1306 display (128, 64, &Wire, -1);
Adafruit_SSD1306 screener(128, 64, &Wire, -1);

unsigned long lastPostMs  = 0;
int WifiCode = 0;
bool haveFreshTelemetry = false;
unsigned long postCounter = 0;

struct Telemetry {
  float cold, hotT, hotW, coldW, rad, amT;
  float fanPct, coldTecPct, hotTecPct, pumpPct;
  float coldSP, hotSP, laser;
  bool  valid;
};

String lastLine = "";
Telemetry latest;

WiFiClientSecure client;
HTTPClient http;

void setup() {
  pinMode(Wifi_Enable_Pin, INPUT_PULLDOWN);
  Serial.begin(115200);
   client.setInsecure();
  http.setReuse(true);                 // allow keep-alive reuse
  http.setTimeout(6000);               // keep your 6s ceiling
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  while (!Serial) { delay(10); }

  Wire.begin(21, 22);
  display.begin (SSD1306_SWITCHCAPVCC, 0x3C);
  screener.begin(SSD1306_SWITCHCAPVCC, 0x3D);
  Serial2.begin(UART_BAUDRATE, SERIAL_8N1, RX_PIN, TX_PIN);
  Serial2.setTimeout(50);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  Serial.printf("[WIFI] Connecting to \"%s\"...\n", SSID);
  WiFi.begin(SSID, PASS);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_TIMEOUT) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WIFI] CONNECTED  IP=%s  RSSI=%d dBm\n",
                  WiFi.localIP().toString().c_str(),
                  WiFi.RSSI());
  } else {
    Serial.println("[WIFI] NOT CONNECTED (timeout), will retry in loop.");
  }
}

void loop() {
  ensureWifi();

  Serial2.print("R\n");

  if (Serial2.available()) {
    String line = Serial2.readStringUntil('\n');
    line.replace("\r", "");
    line.trim();
    if (line.length() > 0 && line != lastLine) {
      lastLine = line;
      Telemetry t;
      if (parseCSVtoTelemetry(line, t)) {
        latest = t;
        haveFreshTelemetry = true;

        ScreenTime(display, 1, 5 , "ColdW   : ", t.coldW , 2, true , false, false);
        ScreenTime(display, 1, 20, "ColdSet : ", t.coldSP, 2, false, false, false);
        ScreenTime(display, 1, 30, "HotSet  : ", t.hotSP , 2, false, false, true );

        if (WiFi.status() == WL_CONNECTED) {
          ScreenTime(screener, 2, 0, "WiFi OK ", 0, 0, true, true, false);
        } else {
          ScreenTime(screener, 2, 0, "No WiFi ", 0, 0, true, true, false);
        }
      }
    }
  }

  if (haveFreshTelemetry && (millis() - lastPostMs >= POST_PERIOD_MS) && Wifi_Enable_Pin) {
    lastPostMs = millis();
    postData(latest);
    ScreenTime(screener, 1, 45, "Wifi Code: ", WifiCode, 0, false, false, true);
  }
}


void ensureWifi() {                       // keeps wifi connection on, lowering time between posts
  static wl_status_t last = WL_NO_SHIELD;
  wl_status_t s = WiFi.status();

  if (s != WL_CONNECTED) {
    static uint32_t lastAttempt = 0;
    const uint32_t REATTEMPT_EVERY_MS = 5000;
    if (millis() - lastAttempt >= REATTEMPT_EVERY_MS) {
      lastAttempt = millis();
      Serial.printf("[WIFI] Reconnect attempt (status=%d)\n", (int)s);
      WiFi.begin(SSID, PASS);
    }
  }

  if (s != last) {
    last = s;
    if (s == WL_CONNECTED) {
      Serial.printf("[WIFI] CONNECTED  IP=%s  RSSI=%d dBm\n",
                    WiFi.localIP().toString().c_str(), WiFi.RSSI());
    } else {
      Serial.printf("[WIFI] Status change → %d\n", (int)s);
    }
  }
}


void postData(const Telemetry& t) {
  if (WiFi.status() != WL_CONNECTED)  return;

  String payload = "{";
  payload += "\"ColdTemp\":"       + String(t.cold,       2)  + ",";
  payload += "\"HotTemp\":"        + String(t.hotT,       2)  + ",";
  payload += "\"HotWaterTemp\":"   + String(t.hotW,       2)  + ",";
  payload += "\"ColdWaterTemp\":"  + String(t.coldW,      2)  + ",";
  payload += "\"RadTemp\":"        + String(t.rad,        2)  + ",";
  payload += "\"AmbientTemp\":"    + String(t.amT,        2)  + ",";
  payload += "\"Fan_Percent\":"    + String(t.fanPct,     2)  + ",";
  payload += "\"ColdTecPercent\":" + String(t.coldTecPct, 2)  + ",";
  payload += "\"HotTecPercent\":"  + String(t.hotTecPct,  2)  + ",";
  payload += "\"Pump_Percent\":"   + String(t.pumpPct,    2)  + ",";
  payload += "\"ColdSetPoint\":"   + String(t.coldSP,     2)  + ",";
  payload += "\"HotSetPoint\":"    + String(t.hotSP,      2)  + ",";
  payload += "\"Post#\":"          + String(++postCounter);
  payload += "}";

  WiFiClientSecure client; client.setInsecure();
  HTTPClient http;
  http.setTimeout(6000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  if (!http.begin(client, WEBAPP_URL)) {
    http.end();
    return;
  }

  http.addHeader("Content-Type", "application/json");
  WifiCode = http.POST(payload);
  http.end();
}

void ScreenTime(Adafruit_SSD1306 &screen, int Font, int Location, const String& Label, float Value, int Decimal, bool CLEAR, bool NoNum, bool Update) {
  if (CLEAR) screen.clearDisplay();
  screen.setTextColor(SSD1306_WHITE);
  screen.setTextSize(Font);
  screen.setCursor(0, Location);
  screen.print(Label);
  if (!NoNum) screen.println(Value, Decimal);
  if (Update) screen.display();
}

bool parseCSVtoTelemetry(const String& line, Telemetry& t) {
  t.valid = false;
  const int needed = 13;
  int idx = 0, start = 0;

  for (; idx < needed; ++idx) {
    int comma = line.indexOf(',', start);
    String tok = (comma == -1) ? line.substring(start) : line.substring(start, comma);
    tok.trim();
    if (tok.length() == 0) return false;

    switch (idx) {
      case  0: t.cold       = tok.toFloat(); break;
      case  1: t.hotT       = tok.toFloat(); break;
      case  2: t.hotW       = tok.toFloat(); break;
      case  3: t.coldW      = tok.toFloat(); break;
      case  4: t.rad        = tok.toFloat(); break;
      case  5: t.amT        = tok.toFloat(); break;
      case  6: t.fanPct     = tok.toFloat(); break;
      case  7: t.coldTecPct = tok.toFloat(); break;
      case  8: t.hotTecPct  = tok.toFloat(); break;
      case  9: t.pumpPct    = tok.toFloat(); break;
      case 10: t.coldSP     = tok.toFloat(); break;
      case 11: t.hotSP      = tok.toFloat(); break;
      case 12: t.laser      = tok.toFloat(); break;
    }
    if (comma == -1) { idx++; break; }
    start = comma + 1;
  }

  if (idx != needed) return false;
  t.valid = true;
  return true;
}
