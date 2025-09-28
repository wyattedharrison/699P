#include <Wire.h>                                              // I2C communication library
#include <Adafruit_GFX.h>                                      // screen library
#include <Adafruit_SSD1306.h>                                  // ^^^            
#define TIApin A0                                              // sampling pin for TIA amp
#define NIApin A1                                              // sampling pin for non-inv amp

Adafruit_SSD1306 display(128,64,&Wire,-1);                     // screen is 128x64 pixels, over i2c comm, -1 indicates no reset pin
float TIAvolt   = 0;                                           // voltage at output of TIA Amplifier
float NIAvolt   = 0;                                           //
const int NumSamples = 2000;                                   // num samples in sum of squares operation

void setup() {
Serial.begin(9600);                                            // baud rate = 9600
display.begin(SSD1306_SWITCHCAPVCC, 0x3C);                     // instantiation of screen with standard address
}

void loop() {
readvoltage();                                                 // samples amplifier voltage
updateDisplay();                                               // controls screen
}

void readvoltage(){                                            // takes sample of voltage at node
  uint32_t sumSquaresTIA = 0;                                  // reset TransImpedance Amplifer voltage variable
  uint32_t sumSquaresNIA = 0;                                  // reset Non-Inverting Amplifier voltage variable
  for (uint16_t i = 0; i<NumSamples; i++){                     // take samples
    uint16_t sampleTIA = analogRead(TIApin);                   // read voltage of TIA
    sumSquaresTIA += (uint32_t)sampleTIA * sampleTIA;          // accumulate sum of squares
    uint16_t sampleNIA = analogRead(NIApin);                   // read voltage at NIA
    sumSquaresNIA += (uint32_t)sampleNIA * sampleNIA;          // accumulate sum of squares
  }
  TIAvolt = sqrt(sumSquaresTIA / NumSamples) * (5.0/1023.0);   // root the sum, normalize to voltage
  NIAvolt = sqrt(sumSquaresNIA / NumSamples) * (5.0/1023.0);   // root the sum, normalize to voltage
}

void updateDisplay(){                                          // outputs to screen
  display.clearDisplay();                                      // reset screen
  display.setTextSize(2);                                      // set font size
  display.setTextColor(SSD1306_WHITE);                         // screen is monochromatic. White makes max brightness
  display.setCursor(0,0);                                      // sets position on screen for this write
  display.print("TIA:");                                       // display label
  display.println(TIAvolt,3);                                  // display value to 3 decimal places
  display.setCursor(0,30);                                     // set postion of next write 
  display.print("NIA:");                                       // display label
  display.println(NIAvolt,3);                                  // display value with 3 decimal places
  display.display();                                           // update with given data
}


