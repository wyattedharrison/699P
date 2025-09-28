#include <Wire.h>                   // enable I2C comm
#include <Adafruit_GFX.h>           // graphic library for OLED screen
#include <Adafruit_SSD1306.h>       // Serial Library for OLED screen
#include <math.h>                   // math function library
#include <WiFi.h>                   // WiFi library 
#include <HTTPClient.h>             // enables http requests
#include <WiFiClientSecure.h>       // faster communication by maintaining connection rather than opening and closing

#define RX_PIN          16          // receiving pin from other esp32
#define TX_PIN          17          // transmitting pin to other ESP32
#define UART_BAUDRATE   115200      // set baud for serial comm
#define Wifi_Enable_Pin 14          // onboard switch that enables data posting

const char* SSID       = "WifiName";       // name of WiFi source (my phone)
const char* PASS       = "WifiPassword";        // password for wifi
const char* WEBAPP_URL = "https://script.google.com/macros/s/AKfycbzRB5kRTuDU3dlqUVokS2orTb6J8BoIjkudVlq_eNcWCWpixTXBzmH1sCBWQWsQavBv6w/exec"; // google app script execution URL

Adafruit_SSD1306 display (128, 64, &Wire, -1);      // instantiation of screen
Adafruit_SSD1306 screener(128, 64, &Wire, -1);      // ^^^

int WifiCode = 0;                                   // wifi code sent back in http client request
bool haveFreshTelemetry = false;                    // returns true if different data is present
unsigned long postCounter = 0;                      // logs number of posts

struct Telemetry {                                  // sensor data from control esp32
  float cold, hotT, hotW, coldW, rad, amT;          // temperature data
  float fanPct, coldTecPct, hotTecPct, pumpPct;     // duty cycle data
  float coldSP, hotSP, laser;                       // set points and laser power at photodetector
  bool  valid;                                      // checks for valid data (not null, Nan, etc)
};

WiFiClientSecure client;                            // establish client object and check security once
HTTPClient http;                                    // allow http commands like get() or post()
Telemetry latest = {};                              // set all of Tememetry{} to zero.

void setup() {                                           
  pinMode(Wifi_Enable_Pin, INPUT_PULLDOWN);              // built in pull down resistor for enable pin (Positive logic)
  Serial.begin(115200);                                  // baud rate 
   client.setInsecure();                                 // remove security. while not secure, it does speed up post times
  http.setReuse(true);                                   // allow keep-alive reuse, increased speed for posts
  http.setTimeout(8000);                                 // time before abandoning a post
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);// allows google app script to redirect post (script keeps data clean)
  while (!Serial) { delay(10); }                         // wait for serial comm to be established. its the whole point of this esp32

  Wire.begin(21, 22);                                      // declares the SDA and SCL pins. 
  display.begin (SSD1306_SWITCHCAPVCC, 0x3C);              // screen.
  screener.begin(SSD1306_SWITCHCAPVCC, 0x3D);              // Modified screen
  Serial2.begin(UART_BAUDRATE, SERIAL_8N1, RX_PIN, TX_PIN);// sets rules of comm between microcontrollers
  Serial2.setTimeout(500);                                 // serial timeout is 500ms. hasnt failed yet

  WiFi.mode(WIFI_STA);                                     // ESP32 is in client only mode. ESP32 wont start its own network
  WiFi.setSleep(false);                                    // never disable WiFi power saving. constant on position. 

  WiFi.begin(SSID, PASS);                                  // connect to wifi with credentials above
  while (WiFi.status() != WL_CONNECTED) delay(250);        // wait for connection
  WiFi.setAutoReconnect(true);                             // reconnect to wifi if it is lost. I had a whole code block that did this and found out this command existed
                                                           // additionally, this command decreased post time by nearly 2s. absurd.
}

void loop() {
  Serial2.print("R\n");                              // request data from Control Esp32

 if (Serial2.available()) {                          // check if Rx buffer has data to share
   String line = Serial2.readStringUntil('\n');      // stop reading at newline 
   Telemetry t;                                      // local variable t holds all Telemetry variables
    if (parseCSVtoTelemetry(line, t)) {              // fill out the variable with last UART packet. returns true if t.valid = true
     latest = t;                                     // copy local t into global latest (latest dataset)
     haveFreshTelemetry = true;                      // bool that states fresh data has arrived

     ScreenTime(display, 1, 5 , "ColdW   : ", t.coldW , 2, true , false, false); // update screen with freeze protection variable
     ScreenTime(display, 1, 20, "ColdSet : ", t.coldSP, 2, false, false, false); // update screen with cold set point
     ScreenTime(display, 1, 30, "HotSet  : ", t.hotSP , 2, false, false, true ); // update screen with hot set point

      if (WiFi.status() == WL_CONNECTED) {                                       // if still connected to wifi,
       ScreenTime(screener, 2, 0, "WiFi OK ", 0, 0, true, true, false);          // confirms wifi is good every loop
       } else { ScreenTime(screener, 2, 0, "No WiFi ", 0, 0, true, true, false); // if wifi is out, say that
        }
      }
    }


  if (haveFreshTelemetry && digitalRead(Wifi_Enable_Pin)) {                                 // CHECKLIST: fresh telemetry? switch on?
    postData(latest);                                                                       // post the fresh telemetry data
    ScreenTime(screener, 1, 45, "Wifi Code: ", WifiCode, 0, false, false, true);            // update screen with wifi code (<300 is good)
  }
}

void postData(const Telemetry& t) {                                   // posts fresh telemetry data
  if (WiFi.status() != WL_CONNECTED)  return;                         // if wifi is out, dont bother

  String payload = "{";                                               // start a packet
  payload += "\"ColdTemp\":"       + String(t.cold,       2)  + ",";  // add in variables one by one, followed by a comma
  payload += "\"HotTemp\":"        + String(t.hotT,       2)  + ",";  // ^
  payload += "\"HotWaterTemp\":"   + String(t.hotW,       2)  + ",";  // ^^
  payload += "\"ColdWaterTemp\":"  + String(t.coldW,      2)  + ",";  // ^^^
  payload += "\"RadTemp\":"        + String(t.rad,        2)  + ",";  // ^^^^
  payload += "\"AmbientTemp\":"    + String(t.amT,        2)  + ",";  // ^^^^^
  payload += "\"Fan_Percent\":"    + String(t.fanPct,     2)  + ",";  // ^^^^^^
  payload += "\"ColdTecPercent\":" + String(t.coldTecPct, 2)  + ",";  // ^^^^^^^
  payload += "\"HotTecPercent\":"  + String(t.hotTecPct,  2)  + ",";  // ^^^^^^^^
  payload += "\"Pump_Percent\":"   + String(t.pumpPct,    2)  + ",";  // ^^^^^^^^^
  payload += "\"ColdSetPoint\":"   + String(t.coldSP,     2)  + ",";  // ^^^^^^^^^^
  payload += "\"HotSetPoint\":"    + String(t.hotSP,      2)  + ",";  // ^^^^^^^^^^^
  payload += "\"Post#\":"          + String(++postCounter);           // ^^^^^^^^^^^^
  payload += "}";                                                     // end packet

  WiFiClientSecure client; client.setInsecure();                      //
  HTTPClient http;                                                    //
  http.setTimeout(6000);                                              //
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);             //

  if (!http.begin(client, WEBAPP_URL)) {                              //
    http.end();                                                       //
    return;                                                           //
  }

  http.addHeader("Content-Type", "application/json");                 //
  WifiCode = http.POST(payload);                                      //
  http.end();                                                         //
}
    // Function| Which Screen? (pointer)| Font Size | Vertical Location| Label on screen    | Variable   | Decimal Places| Clear data?| has number?| update screen?
void ScreenTime(Adafruit_SSD1306 &screen, int Font  , int Location     , const String& Label, float Value, int Decimal   , bool CLEAR , bool NoNum , bool Update) {
  if (CLEAR) screen.clearDisplay();                                   // clears screen data
  screen.setTextColor(SSD1306_WHITE);                                 // sets screen to max brightness
  screen.setTextSize(Font);                                           // sets font size
  screen.setCursor(0, Location);                                      // sets vertical location of text 
  screen.print(Label);                                                // prints the label
  if (!NoNum) screen.println(Value, Decimal);                         // messages only, like "Wifi Ok"
  if (Update) screen.display();                                       // update with fresh data
}

bool parseCSVtoTelemetry(const String& line, Telemetry& t) {          // parse csv line into the Telemetry struct t. returns true if successful
  t.valid = false;                                                    // guilty until proven innocent
  const int needed = 13;                                              // number of variables in a packet. if !=13, something is wrong
  int idx = 0, start = 0;                                             // idx is currnet field, start is scan position

  for (; idx < needed; ++idx) {                                       // loop over the 13 required fields
    int comma = line.indexOf(',', start);                             // find next comma. returns -1 if none found
    String tok = (comma == -1) ? line.substring(start) : line.substring(start, comma);// extract token. if no commas, finish line.
    tok.trim();                                                       // remove white space
    if (tok.length() == 0) return false;                              // no token = something wrong. bail early and return false

    switch (idx) {                                  // each line below has a particular token # local to idx
      case  0: t.cold       = tok.toFloat(); break; // 
      case  1: t.hotT       = tok.toFloat(); break; // extract varible from t, convert string to float, leave switch
      case  2: t.hotW       = tok.toFloat(); break; // 
      case  3: t.coldW      = tok.toFloat(); break; //
      case  4: t.rad        = tok.toFloat(); break; //
      case  5: t.amT        = tok.toFloat(); break; // if the string cannot be parsed to float, it will return 0.0
      case  6: t.fanPct     = tok.toFloat(); break; //
      case  7: t.coldTecPct = tok.toFloat(); break; //
      case  8: t.hotTecPct  = tok.toFloat(); break; //
      case  9: t.pumpPct    = tok.toFloat(); break; // Only one of these cases runs each iteration of idx
      case 10: t.coldSP     = tok.toFloat(); break; //
      case 11: t.hotSP      = tok.toFloat(); break; // Some of these values are not super important. consider removing some later
      case 12: t.laser      = tok.toFloat(); break; //
    }
    if (comma == -1) { idx++; break; }              // bug fix. if no comma, increment idx to satisfy condition (idx<needed)
    start = comma + 1;                              // iterate start
  }

  if (idx != needed) return false;                  // if idx is not the # of required field, somethings wrong. 
  return true;                                      // parse successful.
}
