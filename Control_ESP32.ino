#include <Wire.h>                                  // i2c comm library
#include <Adafruit_GFX.h>                          // screen graphics library
#include <Adafruit_SSD1306.h>                      // screen library
#include <PID_v1.h>                                // pid library
#include <math.h>

// Digital Control Pins
#define FAN_PWM_PIN           13                 // blue      wire
#define FAN_TACH_PIN          12                 // yellow    wire, diode attached
#define COLD_TEC_PWM_PIN      14                 // Orange    wire, to NMOS header
#define HOT_TEC_PWM_PIN       27                 // green     wire, to NMOS Header
#define PUMP_PWM_PIN          26                 // magenta   wire, to NMOS Header

// ADC Pins, 36 is labeled Vp and 39 is labeled Vn   25.33.32.35.34.vn.vp
#define ColdThermistorPin     25                 // Cold side of Tec,    white
#define HotWaterThermistor    33                 // Lid of jacket,       magenta
#define ColdWaterThermistor   32                 // floor of jacket,     yellow
#define HotThermistorPin      35                 // Hot  side of TEC,    lavender
#define RadThermistorPin      34                 // Under Radiator,      Green
#define LaserTiaPin           39                 // Laser Power Sensing, Vn
#define AmbientThermistorPin  36                 // On-Board ,           Vp

// UART handling pins
#define RX2_PIN          16                       // uses rx2 because rx1 has to be low during boot
#define TX2_PIN          17                       // use Tx2 because it is related to rx2
#define UART_BAUDRATE    115200                   // MUST match baud in other code

// Switch Pins
#define ColdStartPin 5                           // activates the cold sweep function for the TEC cooler if switched on (manually)
#define HotStartPin  18                          // activates the hot  sweep function for the Tec Cooler if switched on (Manually)
#define SweepStartPin   19                       // begins a slow sweep for broad data collection

// Instantiate Screens
Adafruit_SSD1306 display(128, 64, &Wire, -1);    // 128 wide, 64 high, I2C, no reset pin
Adafruit_SSD1306 screeny(128,64, &Wire, -1 );

// Temperature variables. returned by SteinHart(), used in all PID(), ScreenTime(), and handleUARTrequest()
double ColdTemp                     = 20;        // cold side of peltier, neighboring FBG
double HotWaterTemp                 = 20;        // hot side of peltier, contacting cooling jacket
double ColdWaterTemp                = 20;        // cold side of peltier, contacting cooling jacket
double HotTemp                      = 20;        // hot side of peltier, neighboring FBG
double RadTemp                      = 20;        // radiator water temperature
double AmbientTemp                  = 20;        // Ambient air temperature. Thermistor located under ESP32(CTRL)

// display values only. used in ScreenTime() and handleUARTrequest()
float Fan_Percent         = 0;                    // fan duty cycle in % (display only)
float ColdTecPercent      = 0;                    // peltier duty cycle in % (display only)
float HotTecPercent       = 0;                    // peltier duty cycle in % (display only)
float Pump_Percent        = 0;                    // pump duty cycle in % (display only)

// control variables. returned by PID(), passed to Screentime() and handleUARTrequest(), 
double Fan_Duty         = 0;                    // fan duty cycle (0-256), driven by FanPID
double Cold_TEC_Duty    = 0;                    // peltier duty cycle (0-256), driven by ColdPID
double Hot_TEC_Duty     = 0;                    // peltier duty cycle, (0-256), driven by HotPID
double Pump_Duty        = 0;                    // pump duty cycle, (0-256), driven by PumpPID

// PID Setpoints. used in PID() and altered by Sweep()
double ColdSetPoint  = 10.0;                   // cold extreme made as start point
double HotSetPoint   = 40.0;                   // hot extreme made as start point

int Pump_Min = 116;
int Fan_Min  = 24;

// PID Instantiations
//  pid name | input     |     Output       |    SetPoint     |  Kp    | Ki    | Kd     | Relationship between input/output       
PID HotPID(  &HotTemp,        &Hot_TEC_Duty,    &HotSetPoint,    8.3   , 0.17 , 0.069   , DIRECT );   // temp of hot side of peltier
PID ColdPID( &ColdTemp,       &Cold_TEC_Duty,   &ColdSetPoint,   5.1   , 0.16 , 0.062   , REVERSE);   // temp of cold side of peltier
PID PumpPID( &HotWaterTemp,   &Pump_Duty,       &AmbientTemp,    55.0  , 0.14 , 3.5     , REVERSE);   // pump controller
PID FanPID ( &RadTemp,        &Fan_Duty,        &AmbientTemp,    50.0  , 0.12 , 3.0     , REVERSE);   // fan controller


void setup() {
  Serial.begin(115200);                                           // serial comm for computer use. Not needed for normal function
  Serial2.begin(UART_BAUDRATE, SERIAL_8N1, RX2_PIN, TX2_PIN);     // predefined baud, 8bit1stop no parity, predefined rx&tx pins
  Serial2.setTimeout(50);                                         // exit if serial comm takes more than 50ms
  Wire.begin(21, 22);                                             // define 21 and 22 as I2C pins
  display.begin( SSD1306_SWITCHCAPVCC, 0x3C );                    // OLED initialization
  screeny.begin( SSD1306_SWITCHCAPVCC, 0x3D );                    // Different address for screeny

  int PwmFreq = 20000;
  int numbits = 8;

  ledcAttach( FAN_PWM_PIN,        25000  , numbits );             // 25kHz, 8-bit PWM, as in datasheet for different fan. (corsair has no datasheet)
  ledcAttach( COLD_TEC_PWM_PIN,   PwmFreq, numbits );             // outside audible range. audible whining observed at 500Hz
  ledcAttach( HOT_TEC_PWM_PIN,    PwmFreq, numbits );             // Mosfets only rated to 20KHz
  ledcAttach( PUMP_PWM_PIN,       PwmFreq, numbits );             // pump is DC. controlled via NMOS with flyback D5019
  ledcWrite(  FAN_PWM_PIN,        0 );                            // start all at 0% 
  ledcWrite(  COLD_TEC_PWM_PIN,   0 );                            // ^
  ledcWrite(  HOT_TEC_PWM_PIN,    0 );                            // ^^
  ledcWrite(  PUMP_PWM_PIN,       0 );                            // ^^^

  pinMode(FAN_TACH_PIN,  INPUT_PULLUP);                            // activate pullup resistor
  pinMode(ColdStartPin,  INPUT_PULLUP);                            // voltage high, negative logic is used. switch connects to ground
  pinMode(HotStartPin,   INPUT_PULLUP);                            // voltage high, negative logic used. Switch connects to ground
  pinMode(SweepStartPin, INPUT_PULLUP);

  // Commands for screen #1 (named display)
  display.clearDisplay();                                       // remove previous screen data
  display.setTextColor(SSD1306_WHITE);                          // max brightness
  display.display();                                            // update display

  // Commands for screen #2 (named screeny)
  screeny.clearDisplay();                                       // remove previous sceen data
  screeny.setTextColor(SSD1306_WHITE);                          // max brightness
  screeny.display();                                            // update screen

  ColdPID.SetMode(AUTOMATIC);                                   // PID is on auto, allowing closed loop control
  PumpPID.SetMode(AUTOMATIC);
  FanPID.SetMode(AUTOMATIC );
  HotPID.SetMode(AUTOMATIC );

 ColdPID.SetSampleTime(150);                                    // small sample time to keep oscillations to a minimum
 HotPID.SetSampleTime (150);
 PumpPID.SetSampleTime(1000);                                   // large sample time because 1s is not long enough 
 FanPID.SetSampleTime (1000);                                   // ^ for water temp to change significantly

  ColdPID.SetOutputLimits(0, 255);                              // set for 8 bit 
  HotPID.SetOutputLimits (0, 255);                              // ^
  PumpPID.SetOutputLimits(116, 255);                            // ^^, except it cant be set below 45% PWM
  FanPID.SetOutputLimits (23, 255);                             // ^^^, except it cant be set below 9% PWM
}



void loop() {

// Sample(--,true)  is used with thermistors only, and will return RESISTANCE, not Voltage. (enacts transfer function)
// Sample(--,false) is used in all other cases, and will return VOLTAGE, not Resistance

float ColdThermistorResistance      = Sample( ColdThermistorPin    , true  ); // reads cold side FBG thermistor resistance
float HotWaterThermistorResistance  = Sample( HotWaterThermistor   , true  ); // reads top of cooling jacket located in lid
float ColdWaterThermistorResistance = Sample( ColdWaterThermistor  , true  ); // reads the cold side of the cooling jacket
float HotThermistorResistance       = Sample( HotThermistorPin     , true  ); // reads hot  side FBG thermistor resistance
float RadThermistorResistance       = Sample( RadThermistorPin     , true  ); // reads water   temp thermistor, local to radiator
float AmbientThermistorResistance   = Sample( AmbientThermistorPin , true  ); // reads ambient temp thermistor, under ESP32 (CTRL)
float LaserPower                    = Sample( LaserTiaPin          , false ); // reads laser intensity, amplified and filtered by lm358n and RC passive filter

// SteinHart() calculates temperature from resistance

ColdTemp      = SteinHart( ColdThermistorResistance      );               // calculate temp of Cold TEC cooler next to FBG
HotWaterTemp  = SteinHart( HotWaterThermistorResistance  );               // calculate temp of top of cooling jacket
ColdWaterTemp = SteinHart( ColdWaterThermistorResistance );               // calculate temp of bottom of cooling jacket
HotTemp       = SteinHart( HotThermistorResistance       );               // calculate temp of Hot  TEC cooler next to FBG
RadTemp       = SteinHart( RadThermistorResistance       );               // calculate temp of Radiator water (therm is under radiator)
AmbientTemp   = SteinHart( AmbientThermistorResistance   );               // calculate temp of ambient environment ( therm is under CTRL ESP32)

// ScreenTime() updates an OLEd screen on the circuit board with live data
//        (   Name, Font Size, Vertical Location, Label      ,         Value, Decimal, Clearscreen?, Update display? )
ScreenTime(display,         1,               5 , "Hot  : "   , HotTemp      , 2      , true        , false           );       // updates OLED screen named display
ScreenTime(display,         1,               20, "HotW : "   , HotWaterTemp , 2      , false       , false           );       // ^    first bool is always TRUE
ScreenTime(display,         1,               30, "Cold : "   , ColdTemp     , 2      , false       , false           );       // ^^   all mid bools are FALSE
ScreenTime(display,         1,               40, "AmT  : "   , AmbientTemp  , 2      , false       , false           );       // ^^^
ScreenTime(display,         1,               50, "Rad  : "   , RadTemp      , 2      , false       , true            );       // ^^^^ last bool is always TRUE

ScreenTime(screeny, 1, 20, "Fan:      " , Fan_Percent    , 2, true , false );        // ^
ScreenTime(screeny, 1, 30, "Pump:     " , Pump_Percent   , 2, false, false );        // ^^
ScreenTime(screeny, 1, 40, "Cold TEC: " , ColdTecPercent , 2, false, false );        // ^^^
ScreenTime(screeny, 1, 50, "Hot  TEC: " , HotTecPercent  , 2, false, true  );        // ^^^^

// PID() computes and enacts PID control on the 4 closed loop controllers. 
PID();                        // PID temperature controls

// Sweep() beings a thermal sweep. It only begins if the enable pin is LOW, following negative logic
if(!digitalRead(SweepStartPin)) Sweep();

//handleUARTRequest() will send data to the other ESP32 so it can handle the time-consuming task of uploading over Wi-Fi.
// The only variable that is not global sent over UART is LaserPower, so it must be passed.
handleUARTRequest(LaserPower);     

}

void Sweep(){
  const unsigned long  StepSec    = 20;                        // seconds between sweep steps
  unsigned long        StepMs     = StepSec * 1000;            // convert to ms
  static unsigned long lastStepMs = 0;                         // reference to make steps occur at the right time
  const double         TempStep   = 0.1  ;                     // difference in temp per step
  unsigned long        now        = millis();                  // current timestamp

  ColdSetPoint = constrain( ColdSetPoint, 9.0 , AmbientTemp);  // sets boundary for PID control during sweep
  HotSetPoint  = constrain( HotSetPoint , AmbientTemp, 41.0);  // ^

  if(now - lastStepMs >= StepMs){                              // checks if a step is needed
    lastStepMs = now;                                          // reset reference point
    if(ColdSetPoint < AmbientTemp) ColdSetPoint += TempStep;   // sweep if less than ambient
    if(HotSetPoint  > AmbientTemp) HotSetPoint  -= TempStep;   // sweep if greater than ambient
  }
}
  
  

void PID(){
  PumpPID.Compute();                                                      // use coolant block temp to calculate pump pwm
  FanPID.Compute();                                                       // use rad temp to calculate fan pwm

// FAN STALLS UNDER 9% DUTY
  if(Fan_Duty < Fan_Min) ledcWrite(FAN_PWM_PIN, 0 );                      // dont burn out the fan with too low PWM
  else ledcWrite(FAN_PWM_PIN,       (int)Fan_Duty      );                 // always apply calculated value to fan

  // PUMP STALLS UNDER 45% DUTY
  if(Pump_Duty < Pump_Min) ledcWrite(PUMP_PWM_PIN,               0 );     // pump stalls under 40% duty cycle
  else                     ledcWrite(PUMP_PWM_PIN,  (int)Pump_Duty );  

  if(!digitalRead(ColdStartPin)){                          // if the manual switch (5) is on, activate peltier (NEGATIVE LOGIC)
     ColdPID.Compute();                                    // use peltier temp to calculate cold peltier pwm
     ledcWrite(COLD_TEC_PWM_PIN,  (int)Cold_TEC_Duty );    // apply pwm 
  } else ledcWrite(COLD_TEC_PWM_PIN, 0);                   // makes sure pwm doesnt stick when disabled

  if(!digitalRead(HotStartPin)){                           // if manual switch (18) is on, activate peltier (NEGATIVE LOGIC)
     HotPID.Compute();                                     // use pletier temp to calculate hot peltier pwm
     ledcWrite(HOT_TEC_PWM_PIN,  (int)Hot_TEC_Duty  );     // apply pwm

  // FREEZE PROTECTION
  if(ColdWaterTemp < 16){                                  // the cooling PIDs wont react to cold temps. maintain efficiency and prevent freezing
    ledcWrite(PUMP_PWM_PIN, 200);                          // enact high PWM for pump
    ledcWrite(FAN_PWM_PIN, 200);                           // enact high pwm for fan
    }

  } else ledcWrite(HOT_TEC_PWM_PIN, 0);                    // this is connected to the switch logic, NOT FREEZE PROTECTION. disables Hot peltier when not in use
} 


float Sample(int SamplePin, bool Thermistor){              // sample function for ADC readings
  float        Accumulator = 0   ;                         // reset accumulator variable
  const float  R1          = 9870;                         // measured Rc value
  const int    NumSamples  = 2000;                         // number of samples when Sample() is called

    for(int i = 0; i < NumSamples; i++) {                  // take many samples and average them
      float Sample = analogRead(SamplePin);                // passed sample pin by function call. Reusable function
      Accumulator += Sample;                               // accumulate the value
  }

  float VoltageAvg = (Accumulator / NumSamples) *(3.3/4095.0);                        // get average sample
  if(Thermistor){                                                                     // Only runs if Thermistor == True
  float SampleToVccRatio = VoltageAvg / 3.3f;                                         // transfer function uses Vs/Vcc. 
  float ThermistorResistance = (SampleToVccRatio * R1) / (1.0f - SampleToVccRatio);   // apply transfer function
  return ThermistorResistance;                                                        // returns the thermistor resistance of the 10k voltage divider
  }
  else{ return VoltageAvg;}                                                           // if not a thermistor, voltage average is returned
}



float SteinHart(float Resistance){                          // converts thermistor resistance to temperature
  const double A           =  3.051767385e-3;               //coefficient of Steinhart-Hart
  const double B           = -1.254360670e-4;               // ^^^
  const double C           =  1.926991631e-6;               // ^^^

  float logR = log(Resistance);                             // simplifies next line
  float deno = A + (B * logR) + (C * logR * logR * logR);   // Steinhart-hart equation
  float kelvin = (1.0000f / deno);                          // 1/^^ completes the calculation
  float Temp = kelvin - 273.15f;                            // convert to celcius (f forces float)
  return Temp;                                              // return temperature in celcius
}



              //     which screen?      | fontsize | vertical location | output label       | output value | # decimal places | delete screen data? | update display?  
void ScreenTime(Adafruit_SSD1306 &screen, int Font, int Location       , const String& Label, float Value  , int Decimal      , bool CLEARSCREEN    , bool UpDate){

  Fan_Percent    = (Fan_Duty / 255.0) * 100.0;         // calculates % duty by mapping to 0-255 value
  ColdTecPercent = (Cold_TEC_Duty / 255.0) * 100.0;    // ^
  HotTecPercent  = (Hot_TEC_Duty / 255.0) * 100.0;     // ^^
  Pump_Percent   = (Pump_Duty / 255.0) * 100.0;        // ^^^

  if(Pump_Duty < Pump_Min) Pump_Percent = 0;           // sends actual implemented duty cycles
  if(Fan_Duty  < Fan_Min ) Fan_Percent  = 0;

  if(CLEARSCREEN){ screen.clearDisplay(); }           // if true, clear screen data. screen is a pointer to acutal screen name being changed. Only done in first function call of ScreenTime

  screen.setTextSize(Font);                           // font size on screen
  screen.setCursor(0,Location);                       // vertical location. # of pixels from top of screen 
  screen.print( Label );                              // name of output that appears on screen. e.g. "Voltage : "
  screen.println( Value, Decimal );                   // value of variable, followed by number of decimal places to display

  if(UpDate){screen.display();}                       // update data for screen. only done in the last function call of Screentime
}


void handleUARTRequest(float LaserPower) {
  bool sawRequest = false;                            // default to no serial request
  while (Serial2.available()) {                       // while comm is established, 
    char c = (char)Serial2.read();                    // read request
    if (c == 'R') sawRequest = true;                  // if request is "R", request was received
  }                                                   
  if (!sawRequest) return;                            // if no request, bail
  
  String line;                                        // envelope
  line.reserve(128);                                  // (9 floats x 7) + (4ints x 3) + 12  +        1 =    88, round up to power of 2
  line  += String(ColdTemp,       2); line += ',';    // account of variables sent ^   | comma 
  line  += String(HotTemp,        2); line += ',';    // ^
  line  += String(HotWaterTemp,   2); line += ',';    // ^^
  line  += String(ColdWaterTemp,  2); line += ',';    // ^^^
  line  += String(RadTemp,        2); line += ',';    // ^^^^
  line  += String(AmbientTemp,    2); line += ',';    // ^^^^^
  line  += String(Fan_Percent,    2); line += ',';    // ^^^^^^
  line  += String(ColdTecPercent, 2); line += ',';    // ^^^^^^^
  line  += String(HotTecPercent,  2); line += ',';    // ^^^^^^^^
  line  += String(Pump_Percent,   2); line += ',';    // ^^^^^^^^^
  line  += String(ColdSetPoint,   2); line += ',';    // ^^^^^^^^^^
  line  += String(HotSetPoint,    2); line += ',';    // ^^^^^^^^^^^
  line  += String(LaserPower,     2);                 // no comma at end. would confuse other microcontroller
  line += '\n';                                       // end with /n, denotes end of transmission
  Serial2.print(line);                                // send data packet
}
