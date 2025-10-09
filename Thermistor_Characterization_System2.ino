#include <Wire.h>                               // I2C libraryD
#include <Adafruit_GFX.h>                       // graphics library
#include <Adafruit_SSD1306.h>                   // OLED library
#include <Adafruit_MLX90614.h>                  // IR temp sensor library
#include <WiFi.h>                               // wifi comm
#include <HTTPClient.h>                         // web client library
#define SamplePin 32                            // thermistor sampling pin
#define LogPin    23

const double A = .003160568262;
const double B = -.0001498702442;
const double C = .00000002096507320;

 Adafruit_SSD1306 display(128, 64, &Wire, -1);  // OLED with pixel Dim == 128x64, I2c, no reset pin
 Adafruit_MLX90614 mlx = Adafruit_MLX90614();   // declaring sensor 

const char* ssid     = "dOnT bLoCk Me";
const char* password = "PangoWango";
const char* scriptURL = "https://script.google.com/macros/s/AKfycbzHzEGv6IP1d-7oJAYDKo_Hlpn0Ic0VsKT2ENDjLMhFJMDu2oM11YYBrIjkPH6kk1SmQA/exec";

float thermaVolt = 1;               // voltage at sample node [32]
float thermaRes  = 1;               // calculated thermistor resistance
const float R1   = 9870;            // measured resistance value

double kelvin =1;                   // kelvin. calculated out of the Steinhart-Hart equation
double celcius = 1;                 // conversion from kelvin
double farenheit = 1;               // conversion from celcius

const int ThermoSamples = 1000;     // number of samples taken in sampleplatter()
const int IRsamples     = 40;

 float irObjectF =  0;              // Object temp from IR sensor
 float irAmbientF = 0;              // Ambient temp from IR sensor
 float irAmbientC = 0;

void setup() {
 Serial.begin(9600);                // set 9600 baud
 Wire.begin(21, 22);                // (SDA, SCL)
 pinMode(23,INPUT);                 // pin 23 is used to toggle data transport


 display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize display
  display.clearDisplay();                    // clear display data
  display.setTextSize(2);                    // set font size
  display.setTextColor(SSD1306_WHITE);       // set max brightness
  display.setCursor(0, 20);                  // vertical location on screen
  display.println("Connecting");             // displays connecting while trying to
  display.display();                         // update the display

 mlx.begin();                                // initialize IR senosr
 WiFi.begin(ssid, password);                 // log into wifi
 Serial.print("Connecting to WiFi");         // Serial message
while (WiFi.status() != WL_CONNECTED) {      // wait for wifi to connect
  delay(1000);                               // half second loop
  Serial.print(".");                         // print to serial monitor (must be plugged into computer)
}
Serial.println("WiFi Connection SUccessful"); // Serial readout
display.clearDisplay();                       // clear display data
display.setTextSize(2);                       // set font size 
display.setTextColor(SSD1306_WHITE);          // set max brightness
  display.setCursor(0, 20);                   // set vertical position
  display.println("WiFi OK");                 // signify wifi connection successful
  display.display();                          // update the screen with above data
}

void loop() {
  samplePlatter();                            // takes samples
  CalTemp();                                  // calculates temp, converts into farenheit
  updateDisplay();                            // updates OLED
  FillSheet();                                // logs data in google sheets
}

void samplePlatter(){                         // samples all the various datapoints
  float averager = 0;                         // holds sum of samples from thermistor
  float irobjectAVG = 0;                      // ^^^ from object temp
  float irambientAVG = 0;                     // ^^ from ambient temp
  float irobjectsample = 0;                   // singular sample of object temp
  float irambientsample = 0;                  // ^^^ of ambient temp

  for(int i = 0; i < ThermoSamples; i++){     // take many samples
    float sample = analogRead(SamplePin);     // reads voltage from thermistor voltage divider
     averager = averager + sample;            // accumulates the discreet samples each loop
  }

  for(int i =0; i< IRsamples; i++){               // take many samples
    irambientsample = mlx.readAmbientTempC();     // take single sample of mlx ambient
    irambientAVG = irambientAVG + irambientsample;// accumulate samples
  }
  thermaVolt = (averager/ThermoSamples) * (3.3/4095.0); // convert samples to voltage
  irAmbientC = irambientAVG / IRsamples;                // divide by num samples
  
}

void CalTemp(){
double VRatio = thermaVolt / 3.3;                        // Ratio to Vcc needed for resistance calculation
thermaRes = (VRatio * R1) / (1.0f - VRatio);             // Transfer function of divider

  //Steinhart-hart equation
float logR = log(thermaRes);                             // simplifies next line
float deno = A + (B * logR) + (C * logR * logR * logR);  // Steinhart-hart equation
kelvin = (1.0000f / deno);                               // 1/^^ completes the calculation

celcius = kelvin - 273.15f;                              // convert to celcius
farenheit = (celcius * 9.0f / 5.0f ) + 32.0f;            // convert to farenheit
}                                                        // using "f" avoids truncation by declaring as float

void updateDisplay(){
  display.clearDisplay();                 // clear display
  display.setTextSize(2);                 // sets text size to double normal
  display.setTextColor(SSD1306_WHITE);    // max brightness for screen 

  display.setCursor(0, 0);                // top left corner
  display.print("IRA:");                  // IR sensor, ambient
  display.println(irAmbientC,2);          // display variable to 2 decimal places

  display.setCursor(0,20);                // 1/3 down the screen
  display.print("ReT:");                  // calculated Thermistor Temp
  display.println(celcius,2);             // display variable to 2 decimal places

  display.setCursor(0,40);                // 2/3 down the screen
  display.print("Res:");                  // calculated resistance of thermistoer
  display.println(thermaRes,0);           // display variable to nearest integer

  display.display();                      // update display with previous commands^^^
}



void FillSheet() {
  if (digitalRead(LogPin) == LOW) return;                     // skip if logging disabled
    HTTPClient http;                                          // HTTp client object
    http.begin(scriptURL);                                    // begin with predefined script 
    http.addHeader("Content-Type", "application/json");       // set content type for JSON
    String payload = "{";                                     // comma separated dataset
    payload += "\"resistance\":"  + String(thermaRes, 1) + ","; // datum
    payload += "\"thermistorC\":" + String(celcius, 2)   + ","; // datum
    payload += "\"irAmbientC\":"  + String(irAmbientC, 2);      // datum
    payload += "}";                                             // end dataset
    Serial.println(payload);                                    // print in console (debug feature)
    int httpResponseCode = http.POST(payload);                  // post the request 
    Serial.println("response: " + String(httpResponseCode));    // print response code

    if (httpResponseCode != 200) {                              // 200 is a total success
      Serial.println("fAiLeD");                                 // if not 200, theres something wrong
    }
    http.end();                                                 // not closing will overload RAM of ESP32
}





