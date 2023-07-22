/*  Grotto Time
 *  My little clock/weather station for my "Nerd Grotto" (aka the garage)
 *  Arduino clock is synchronised via NTP (or RTC if offline)
 *  Uses DHT22 & DMP085 sensors for some weather info
 *  Uses an LDR to capture ambient light value
 *  Uses a PIR sensor to switch LCD backlight off/on
 *  Uploads the data to a PHP script at a website so the data can be viewed online
 *  18-Feb-2016: Changed LCD backlight handling from LDR to PIR sensor
 *  28-Feb-2016: Bit more code-cleaning, added DEBUG_LEVEL to reduce the serial 
 *               monitor noise
 *   2-Mar-2016: added User-agent to HTTP POST function so my webserver would stop 
 *               rejecting it with 403 forbidden
 *               added approx. dew point output
 *   3-Mar-2016: dropped the hundredths off the temperatures
 *   6-Mar-2016: added sunrise/sunset calculations
 *  25-Mar-2016: avoid negative time, i.e. -5.06 for sunset when it should be 19:xx
 *   2-Apr-2016: changed time sync. from every minute (60s) to every hour (3600s)
 */

#include <LiquidCrystal.h>
#include <DHT.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP085.h>
#include <SPI.h>         
#include <Ethernet.h>
#include <TimeLib.h>
#include <Wire.h>
#include <DS1307RTC.h>
#include <EEPROM.h>
#include <math.h>

#define DEBUG_LEVEL 0 // 0 = everything, 1 = network stuff only, 2 = sunrise/sunset stuff only

/* EEPROM stuff */
const byte eepromID        = 0x99; // valid data in EEPROM?
const int eepromIDAddr     = 0;                             // address of EEPROM valididy byte for temperature (1 byte)
const int maxTempAddr      = eepromIDAddr+sizeof(byte);     // address of max Temp float (4 bytes)
const int minTempAddr      = maxTempAddr+sizeof(float);     // address of min Temp float (4 bytes)
const int maxHumidityAddr  = minTempAddr+sizeof(float);     // address of max humidity float (4 bytes)
const int minHumidityAddr  = maxHumidityAddr+sizeof(float); // address of min humidity float (4 bytes)
const int maxPressureAddr  = minHumidityAddr+sizeof(float); // address of max pressure float (4 bytes)
const int minPressureAddr  = maxPressureAddr+sizeof(float); // address of min pressure float (4 bytes)
const int maxTempTimeAddr  = minPressureAddr+sizeof(time_t);
const int minTempTimeAddr  = maxTempTimeAddr+sizeof(time_t);
const int maxHumTimeAddr   = minTempTimeAddr+sizeof(time_t);
const int minHumTimeAddr   = maxHumTimeAddr+sizeof(time_t);
const int maxPressTimeAddr = minHumTimeAddr+sizeof(time_t);
const int minPressTimeAddr = maxPressTimeAddr+sizeof(time_t);
// 2-Mar-2016, added dew point
const int maxDewPointAddr  = minPressTimeAddr+sizeof(time_t);
const int minDewPointAddr  = maxDewPointAddr+sizeof(float);
const int maxDPTimeAddr    = minDewPointAddr+sizeof(float);
const int minDPTimeAddr    = maxDPTimeAddr+sizeof(time_t);
char GrottoTimeVers[] = "GrottoTime 1.5";

/* Uptime */
time_t lastRestart = 0;

/* Ethernet/NTP stuff */
byte mac[] = {0xDE,0xAD,0xBE,0xEF,0x00,0x00 }; // Newer Ethernet shields have a MAC address printed on a sticker on the shield
const uint8_t ip[] = {192,168,4,1};
const uint8_t googleDNS[] = {8,8,8,8};
const uint8_t gateway[] = {192,168,1,1};
const uint8_t subnet[] = {255,255,0,0};
unsigned int localPort = 8888;                 // local port to listen for UDP packets
EthernetUDP Udp;                               // A UDP instance to let us send and receive packets over UDP
char ntpServerName[] = "0.au.pool.ntp.org";    // NTP server addresses
// char ntpServerName[] = "time.nist.gov";
const int NTP_PACKET_SIZE = 48;                // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[NTP_PACKET_SIZE];            // buffer to hold incoming and outgoing packets 
const long TIMEZONE_OFFSET = 36000L;           // set this to the offset in seconds to your local time - SYD is +10
boolean onLine    = false;                     // do we have Ethernet connected?  
boolean wasOnline = false;                     // did we have Ethernet connected?  If we lost it, try again later

/* PHP stuff */
#define PHP_PAGE_LOCATION "/arduino/index.php"
#define SERVER_ADDRESS "website.com.au"
#define HTTP_PORT 80
#define STATUS_BUFFER_SIZE 50
EthernetClient client;
boolean waitingClientConnect = false; // Don't use delay(), so need to keep track of where we are each loop
boolean readyToPost = false;          // Do we have sensor data to post?
long lastHTTPPost = 0;
long clientConnectTime = 30000;
long previousMillisClientConnect = 0;
char tempChar[75] = "\0";
char tChar[6]     = "\0"; // temperature float -> char array
char tFarChar[6]  = "\0"; // temperature in Farenheitr
char hChar[6]     = "\0"; // humidity float -> char array
char pChar[8]     = "\0"; // pressure float -> char array
char ldrChar[4]   = "\0"; // LDR float -> char array
char maxTChar[6]  = "\0"; // max temp -> char array
char minTChar[6]  = "\0"; // min temp -> char array
char maxHChar[6]  = "\0"; // max humidity -> char array
char minHChar[6]  = "\0"; // min humidity -> char array
char maxPChar[8]  = "\0"; // max pressure -> char array
char minPChar[8]  = "\0"; // min pressure -> char array
// 2-Mar-2016, added dew point
char dpChar[6]    = "\0"; // dew point
char minDPChar[6] = "\0";
char maxDPChar[6] = "\0";
// 6-Mar-2016: added sunrise/sunset calculations
String sunriseChar    = "";
String sunsetChar     = "";
String sunriseTomChar = "";
String sunsetTomChar  = "";

/* RTC stuff */
boolean rtcOrNTPSync     = false; // using RTC (false) or NTP (true) time sync
boolean daylightSavingOn = false;
#define clockSyncInterval 3600 // sync Arduino's clock every hour with NTP or RTC
#define batteryPin 4 // battery voltage level pin of RTC module
char *dayOfWeek[]   = {"", "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
char *monthOfYear[] = {"", "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

/* LDR/PIR stuff */
#define ldrPin 9
int ldrValue = 0;
// These LDR hysteresis values are replaced by use of PIR
/* #define ldrBacklightOff 50
#define ldrBacklightOn  110 */
#define pirPin 44
unsigned long lastMillisPIR = 0;
#define backlightOnTime 300000 // 5min 
boolean pirWasTripped = false;

/* DHT22/AM2302 stuff */
#define DHTpin 46
#define DHTtype DHT22 // AM2302
float dhtHumidity           = 0;
float lastHumidity          = 0;
float dhtTemperature        = 0;
float lastTemp              = 0;
float dhtTemperature_F      = 0;
float dhtApproxDewPoint     = 0;
float lastDewPoint          = 0;
float maxTemp               = -100;
time_t maxTempTimestamp     = 0;
float minTemp               = 100;
time_t minTempTimestamp     = 0;
float maxHumidity           = 0;
time_t maxHumidityTimestamp = 0;
float minHumidity           = 100;
time_t minHumidityTimestamp = 0;
float maxDewPoint           = -100;
time_t maxDewPointTimestamp = 0;
float minDewPoint           = 100;
time_t minDewPointTimestamp = 0;
boolean resetTemps          = false;
#define sensorUpdateInterval 2000 // Check sensors every 2s
unsigned long previousMillisSensor = 0;
DHT dht(DHTpin,DHTtype);

/* BMP085 stuff */
Adafruit_BMP085 bmp;
sensors_event_t event;
float bmpPressure           = 0;
float lastPressure          = 0; // for checking sanity of pressure reading
float maxPressure           = 0;
time_t maxPressureTimestamp = 0;
float minPressure           = 1100; 
time_t minPressureTimestamp = 0;
float bmpTemperature        = 0;
float seaLevelPressure      = SENSORS_PRESSURE_SEALEVELHPA;
boolean bmpStarted          = false;

/* LCD stuff */
#define LCDbit4        4 // D4
#define LCDbit5        5 // D5
#define LCDbit6        6 // D6
#define LCDbit7        7 // D7
#define LCDrs          8 // D8
#define LCDen          9 // D9
boolean backlightOn  = true;
int currentDisplay      = 0; // Which display of info are we on?
#define dispTimeDate      0 // the time/date display
#define dispTempHum       1 // the temperature/humidity display
#define dispDewPoint      2 // approx. dew point
#define dispMaxMinTemp    3 // the max/min temps display
#define dispPressure      4 // the pressure display
#define dispSunriseSunset 5
#define dispIPAddress     6 // my current IP address display
#define dispTimeSync      7 // time sync source & DST display
#define dispBatteryLDR    8 // the RTC battery & LDR display
#define maxDisplay        8 // The last valid display number - REMEMBER TO UPDATE IF MORE DISPLAYS ADDED!
unsigned long lastDisplayUpdate = 0UL;
#define sensorDisplayInterval 5000 // Change display every 5s
unsigned long previousMillisSensorDisp = 0;
LiquidCrystal lcd(LCDrs,LCDen,LCDbit4,LCDbit5,LCDbit6,LCDbit7);

/* Freetronics LCD shield
   from http://www.freetronics.com.au/pages/16x2-lcd-shield-quickstart-guide */
#define BUTTON_ADC_PIN           A0  // A0 is the button ADC input
#define LCD_BACKLIGHT_PIN         3  // D3 controls LCD backlight
// ADC readings expected for the 5 buttons on the ADC input
#define RIGHT_10BIT_ADC           0  // right
#define UP_10BIT_ADC            145  // up
#define DOWN_10BIT_ADC          329  // down
#define LEFT_10BIT_ADC          505  // left
#define SELECT_10BIT_ADC        741  // right
#define BUTTONHYSTERESIS         10  // hysteresis for valid button sensing window
// return values for ReadButtons()
#define BUTTON_NONE               0   
#define BUTTON_RIGHT              1   
#define BUTTON_UP                 2   
#define BUTTON_DOWN               3   
#define BUTTON_LEFT               4   
#define BUTTON_SELECT             5   
// some example macros with friendly labels for LCD backlight/pin control, tested and can be swapped into the example code as you like
#define LCD_BACKLIGHT_OFF()     digitalWrite( LCD_BACKLIGHT_PIN, LOW )
#define LCD_BACKLIGHT_ON()      digitalWrite( LCD_BACKLIGHT_PIN, HIGH )
#define LCD_BACKLIGHT(state)    { if(state){digitalWrite(LCD_BACKLIGHT_PIN,HIGH);}else{digitalWrite(LCD_BACKLIGHT_PIN,LOW);} }
byte buttonJustPressed  = false;         // this will be true after a ReadButtons() call if triggered
byte buttonJustReleased = false;         // this will be true after a ReadButtons() call if triggered
byte buttonWas          = BUTTON_NONE;   // used by ReadButtons() for detection of button events

/* Sunrise/Sunset stuff */
#define sydneyLatitude  -33.7
#define sydneyLongitude 150.9
#define ZENITH          90.833333
float sunriseTime, sunsetTime = 0;
float sunriseTomorrow, sunsetTomorrow = 0;
boolean sunriseCalculated, sunsetCalculated = false;

void setup() 
{
  Serial.begin(115200);
  Serial.println("--------------------------------------------");

  // check EEPROM validity after startup
  if (eepromID == EEPROM.read(eepromIDAddr))
  {
    // We've got data stored to EEPROM from previous restart - initialise max/min variables
    EEPROM.get(minTempAddr,minTemp);                   dtostrf(minTemp,3,1,minTChar);
    EEPROM.get(minTempTimeAddr,minTempTimestamp);
    EEPROM.get(maxTempAddr,maxTemp);                   dtostrf(maxTemp,3,1,maxTChar);
    EEPROM.get(maxTempTimeAddr,maxTempTimestamp);
    EEPROM.get(minHumidityAddr,minHumidity);           dtostrf(minHumidity,3,1,minHChar);
    EEPROM.get(minHumTimeAddr,minHumidityTimestamp);
    EEPROM.get(maxHumidityAddr,maxHumidity);           dtostrf(maxHumidity,3,1,maxHChar);
    EEPROM.get(maxHumTimeAddr,maxHumidityTimestamp);
    EEPROM.get(minPressureAddr,minPressure);           dtostrf(minPressure,6,1,minPChar);
    EEPROM.get(minPressTimeAddr,minPressureTimestamp);
    EEPROM.get(maxPressureAddr,maxPressure);           dtostrf(maxPressure,6,1,maxPChar);
    EEPROM.get(maxPressTimeAddr,maxPressureTimestamp);
    EEPROM.get(minDewPointAddr,minDewPoint);           dtostrf(minDewPoint,3,1,minDPChar);
    EEPROM.get(minDPTimeAddr,minDewPointTimestamp);
    EEPROM.get(maxDewPointAddr,maxDewPoint);           dtostrf(maxDewPoint,3,1,maxDPChar);
    EEPROM.get(maxDPTimeAddr,maxDewPointTimestamp);
  }
  
  // Freetronics LCD + Keypad shield setup
  // button ADC input
  pinMode(BUTTON_ADC_PIN,INPUT);         // ensure A0 is an input
  digitalWrite(BUTTON_ADC_PIN,LOW);      // ensure pullup is off on A0
  // LCD backlight control
  pinMode(LCD_BACKLIGHT_PIN,OUTPUT);     // D3 is an output
  digitalWrite(LCD_BACKLIGHT_PIN,HIGH);  // backlight control pin D3 is high (on)
   
   //set up the LCD number of columns and rows: 
   lcd.begin(16,2);
   lcd.setCursor(0,0);
   //         1234567890123456
   lcd.print(GrottoTimeVers);
   lcd.setCursor(0,1);
   //         1234567890123456
   lcd.print("Setup started");
  
  // We need to wait for the WIZnet chip to startup, so take
  // a temp/humidity reading first ...
  dht.begin();
  Serial.println("DHT started");
  lcd.setCursor(0,1);
  //         1234567890123456
  lcd.print("DHT started     ");
  dhtHumidity = dht.readHumidity();
  lastHumidity = dhtHumidity;
  dhtTemperature = dht.readTemperature();
  lastTemp = dhtTemperature;
  dhtTemperature_F = dht.convertCtoF(dhtTemperature);
  // en.wikipedia.org/wiki/Dew_point
  dhtApproxDewPoint = calculateDewPoint(dhtTemperature,dhtHumidity);
  lastDewPoint = dhtApproxDewPoint;
  // Barometric pressure
  if (!bmp.begin())
  {
    Serial.println("Couldn't start BMP085!");
    lcd.setCursor(0,1);
    //         1234567890123456
    lcd.print("BMP085 failed   ");
    bmpStarted = false;
  }
  else
  {
    Serial.println("BMP085 started");
    lcd.setCursor(0,1);
    //         1234567890123456
    lcd.print("BMP085 started  ");
    bmpStarted = true;
    bmp.getEvent(&event);
    bmpPressure = event.pressure;
    lastPressure = bmpPressure;
  }

  // LDR & PIR
  ldrValue = analogRead(ldrPin);
 
  // Start Ethernet and UDP -> obtain address from DHCP
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Waiting 60s for");
  lcd.setCursor(0,1);
  lcd.print("Ethernet start  ");
  setSyncInterval(clockSyncInterval); 
  Ethernet.begin(mac,ip,googleDNS,gateway,subnet); 
  onLine = true;
  wasOnline = true;
  // Output IP address obtained via DHCP
  Serial.print("My IP address is ");
  Serial.println(Ethernet.localIP());
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Ethernet started");
  lcd.setCursor(0,1);
  lcd.print(Ethernet.localIP());
  // Set time from NTP
  Udp.begin(localPort);
  setSyncProvider(getNTPTime);
  if (timeStatus() == timeNotSet)
  {
    Serial.println("Couldn't sync. with NTP");
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("NTP sync failed ");
  } // timeNotSet
  else
  {
    time_t t = now();
    RTC.set(t);
    Serial.println("Time has been updated to the RTC");
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Got time from ");
    lcd.setCursor(0,1);
    lcd.print("NTP, updated RTC");
    rtcOrNTPSync = true;
  } // time is set

  // capture the restart time, 2-Mar-2016 don't need to adjust for local time
  lastRestart = now();
 
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Setup completed");
}

void loop() 
{
  byte button;
  byte timestamp;

  unsigned long currentMillisTime = millis();
  unsigned long currentMillisSensor = currentMillisTime;
  unsigned long currentMillisClientConnect = currentMillisTime;

  time_t timeNow = 0;
  time_t timeNowSyd = 0;
  String tempString = "";

  // Set time to local Sydney time
  timeNow = now();
  daylightSavingOn = checkDST(timeNow);
  if (daylightSavingOn)
  {
    timeNowSyd = timeNow+TIMEZONE_OFFSET+3600;
  }
  else
  {
    timeNowSyd = timeNow+TIMEZONE_OFFSET;
  }

  if ((hour(timeNowSyd) == 0) && (sunriseCalculated))
  {
    sunriseCalculated = false;
    sunriseChar = "";
    sunriseTomChar = "";
  }

  if ((hour(timeNowSyd) == 0) && (sunsetCalculated))
  {
    sunsetCalculated = false;
    sunsetChar = "";
    sunsetTomChar = "";
  }
  
  if (sunriseCalculated == false)
  {
    sunriseTime = calculateSunriseSunset(timeNowSyd, false, true, sydneyLongitude, sydneyLatitude, ZENITH);
    sunriseChar = printSunriseSunsetTime(sunriseTime);
    sunriseTomorrow = calculateSunriseSunset(timeNowSyd, true, true, sydneyLongitude, sydneyLatitude, ZENITH);
    sunriseTomChar = printSunriseSunsetTime(sunriseTomorrow);
    sunriseCalculated = true;
  }

  if (sunsetCalculated == false)
  {
    sunsetTime = calculateSunriseSunset(timeNowSyd, false, false, sydneyLongitude, sydneyLatitude, ZENITH);
    sunsetChar = printSunriseSunsetTime(sunsetTime);
    sunsetTomorrow = calculateSunriseSunset(timeNowSyd, true, false, sydneyLongitude, sydneyLatitude, ZENITH);
    sunsetTomChar = printSunriseSunsetTime(sunsetTomorrow);
    sunsetCalculated = true;
  }

  // reset max/min temps every Sunday after midnight
  if ((weekday(timeNowSyd) == 1) and (resetTemps == false))
  {
    resetTemps = true;
    maxTemp = -100;
    strcpy(maxTChar,"\0");
    minTemp = 100;
    strcpy(minTChar,"\0");
    maxTempTimestamp = 0;
    minTempTimestamp = 0;
    maxHumidity = 0;
    strcpy(maxHChar,"\0");
    minHumidity = 100;
    strcpy(minHChar,"\0");
    maxHumidityTimestamp = 0;
    minHumidityTimestamp = 0;
    maxPressure = 0;
    strcpy(maxPChar,"\0");
    minPressure = 1100;
    strcpy(minPChar,"\0");
    maxPressureTimestamp = 0;
    minPressureTimestamp = 0;
    maxDewPoint = -100;
    strcpy(maxDPChar,"\0");
    minDewPoint = 100;
    strcpy(minDPChar,"\0");
  }
  else if (weekday(timeNowSyd) > 1)
  {
    resetTemps = false; // reset max/min readings next Sunday
  }

  // automatically cycle through the displays every sensorDisplayInterval
  if ((currentMillisTime - previousMillisSensorDisp) > sensorDisplayInterval)
  {
    previousMillisSensorDisp = currentMillisTime;
    currentDisplay += 1;
  }

  // loop back to first display
  if (currentDisplay > maxDisplay)
  {
    currentDisplay = dispTimeDate; 
  }

  // update the display every second
  if ((currentMillisTime - lastDisplayUpdate) > 1000)
  {
    lastDisplayUpdate = currentMillisTime;
    lcd.clear();
    lcd.setCursor(0,0);

    switch (currentDisplay)
    {
      case dispTimeDate: // date & time
        lcd.print("Date: ");
        lcd.print(dayOfWeek[weekday(timeNowSyd)]);
        lcd.print(" ");
        lcd.print(monthOfYear[month(timeNowSyd)]);
        lcd.print(" ");
        lcd.print(day(timeNowSyd));
        lcd.setCursor(0,1);
        lcd.print("Time: ");
        if (hour(timeNowSyd) < 10)
          lcd.print("0");
        lcd.print(hour(timeNowSyd));
        lcd.print(":");
        if (minute(timeNowSyd) < 10)
          lcd.print("0");
        lcd.print(minute(timeNowSyd));
        lcd.print(":");
        if (second(timeNowSyd) < 10)
          lcd.print("0");
        lcd.print(second(timeNowSyd));
        if (daylightSavingOn)
          lcd.print(" *");
        break;
      case dispTempHum: // temp & humidity
        if (isnan(dhtTemperature) || isnan(dhtHumidity))
        {
          Serial.println("Failed to read from DHT");
          lcd.print("Failed to read ");
          lcd.setCursor(0,1);
          lcd.print("from DHT");
          strcpy(tChar,"\0");
          strcpy(hChar,"\0");
        }
        else
        {
          lcd.print("Temp:    ");
          lcd.print(tChar);
          lcd.print(char(223)); // degrees symbol
          lcd.print("C");
          lcd.setCursor(0,1);
          lcd.print("Humidity:");
          lcd.print(hChar);
          lcd.print("%");
        }
        break;
      case dispMaxMinTemp: // max/min temps since Sunday
        lcd.print("Max Temp:");
        lcd.print(maxTChar);
        lcd.print(char(223)); // degrees symbol
        lcd.print("C");
        lcd.setCursor(0,1);
        lcd.print("Min Temp:");
        lcd.print(minTChar);
        lcd.print(char(223)); // degrees symbol
        lcd.print("C");
        break;
      case dispPressure: // barometric pressure
        lcd.print("Barometric Pres.");
        lcd.setCursor(0,1);
        if (event.pressure)
        {
          lcd.print(pChar);
          lcd.print("hPa");
        }
        else
        {
          lcd.print("no event!");
          strcpy(pChar,"\0");
        }
        break;
      case dispDewPoint: // approx. dew point
        //         1234567890123456
        lcd.print("Approximate Dew");
        lcd.setCursor(0,1);
        lcd.print("Point: ");
        lcd.print(dpChar);
        lcd.print(char(223)); // degrees symbol
        lcd.print("C");
        break;      
      case dispSunriseSunset:
        // if sunrise has already passed and it's not yet sunset, display tomorrow's sunrise
        if ((hour(timeNowSyd) > sunriseTime) && (hour(timeNowSyd) < sunsetTime))
        {
          //         1234567890123456
          lcd.print("Sunset : ");
          lcd.print(sunsetChar);
          lcd.setCursor(0,1);
          lcd.print("Sunrise: ");
          lcd.print(sunriseTomChar);
          lcd.print(" *"); // to indicate tomorrow
        }
        else if (hour(timeNowSyd) > sunsetTime)
        {
          // if it's past sunset, display tomorrow's sunrise & sunset
          lcd.print("Sunrise: ");
          lcd.print(sunriseTomChar);
          lcd.print(" *");
          lcd.setCursor(0,1);
          lcd.print("Sunset : ");
          lcd.print(sunsetTomChar);
          lcd.print(" *");
        }
        else
        {
          // print today's sunrise & sunset
          lcd.print("Sunrise: ");
          lcd.print(sunriseChar);
          lcd.setCursor(0,1);
          lcd.print("Sunset : ");
          lcd.print(sunsetChar);
        }
        break;  
      case dispIPAddress: // IP address
        lcd.print("My IP Address");
        lcd.setCursor(0,1);
        lcd.print(Ethernet.localIP());
        break;
      case dispTimeSync: // RTC or NTP time
        if (rtcOrNTPSync)
        {
          lcd.print("Time sync: NTP");
        }
        else
        {
          lcd.print("Time sync: RTC");
        }
        lcd.setCursor(0,1);
        if (daylightSavingOn)
        {
          lcd.print("DST:       On");
        }
        else
        {
          lcd.print("DST:       Off");
        }
        break;
      case dispBatteryLDR: // RTC battery voltage & LDR 
        lcd.print("Battery: ");
        lcd.print(analogRead(batteryPin));
        lcd.setCursor(0,1);
        lcd.print("Light:   ");
        lcd.print(analogRead(ldrPin));
        break;
      default:
        currentDisplay = dispTimeDate;
        break;
      }
  }

  // check sensors every sensorUpdateInterval
  if ((currentMillisSensor - previousMillisSensor) > sensorUpdateInterval)
  {
    if (DEBUG_LEVEL == 0)
    {
      Serial.println("Checking sensors");
    }
    previousMillisSensor = currentMillisSensor;
    // Humidity
    dhtHumidity = dht.readHumidity();
    dtostrf(dhtHumidity,3,1,hChar);
    // sanity check - ignore if more than 10% change
    if (abs(dhtHumidity-lastHumidity) < (lastHumidity*0.1))
    {
      if (dhtHumidity >= maxHumidity)
      {
        maxHumidity = dhtHumidity;
        dtostrf(maxHumidity,3,1,maxHChar);
        maxHumidityTimestamp = timeNow;
        if (writeFloatToEEPROM(maxHumidity,maxHumidityAddr))
        {
          Serial.println("Updated EEPROM with max humidity");
          EEPROM.put(maxHumTimeAddr,maxHumidityTimestamp);
        }
        else
        {
          Serial.println("Failed EEPROM update with max humidity!");
        }
      }
      if (dhtHumidity <= minHumidity)
      {
        minHumidity = dhtHumidity;
        dtostrf(minHumidity,3,1,minHChar);
        minHumidityTimestamp = timeNow;
        if (writeFloatToEEPROM(minHumidity,minHumidityAddr))
        {
          Serial.println("Updated EEPROM with min humidity");
          EEPROM.put(minHumTimeAddr,minHumidityTimestamp);
       }
        else
        {
          Serial.println("Failed EEPROM update with min humidity!");
        }
      }
      lastHumidity = dhtHumidity;
    }
    // Temperature - degrees Celsius
    dhtTemperature = dht.readTemperature();
    dtostrf(dhtTemperature,3,1,tChar);
    // sanity check - ignore if more than 10% change
    if (abs(dhtTemperature-lastTemp) < (lastTemp*0.1))
    {
      // check min/max - reset every Sunday
      if (dhtTemperature >= maxTemp)
      {
        maxTemp = dhtTemperature;
        dtostrf(maxTemp,3,1,maxTChar);
        maxTempTimestamp = timeNow;
        if (writeFloatToEEPROM(maxTemp,maxTempAddr))
        {
          Serial.println("Updated EEPROM with max temp");
          EEPROM.put(maxTempTimeAddr,maxTempTimestamp);
        }
        else
        {
          Serial.println("Failed EEPROM update with max temp!");
        }
      }
      if (dhtTemperature <= minTemp)
      {
        minTemp = dhtTemperature;
        dtostrf(minTemp,3,1,minTChar);
        minTempTimestamp = timeNow;
        if (writeFloatToEEPROM(minTemp,minTempAddr))
        {
          Serial.println("Updated EEPROM with min temp");
          EEPROM.put(minTempTimeAddr,minTempTimestamp);
        }
        else
        {
          Serial.println("Failed EEPROM update with min temp!");
        }
      }
      lastTemp = dhtTemperature;
    }
    // Temperature - degrees Farenheit
    dhtTemperature_F = dht.convertCtoF(dhtTemperature);
    dtostrf(dhtTemperature_F,3,1,tFarChar);
    // approx. dew point
    dhtApproxDewPoint = calculateDewPoint(dhtTemperature,dhtHumidity);
    // sanity check - ignore if more than 10% change
    if (abs(dhtApproxDewPoint-lastDewPoint) < (lastDewPoint*0.1))
    {
      // check min/max - reset every Sunday
      if (dhtApproxDewPoint >= maxDewPoint)
      {
        maxDewPoint = dhtApproxDewPoint;
        dtostrf(maxDewPoint,3,1,maxDPChar);
        maxDewPointTimestamp = timeNow;
        if (writeFloatToEEPROM(maxDewPoint,maxDewPointAddr))
        {
          Serial.println("Updated EEPROM with max dew point");
          EEPROM.put(maxDPTimeAddr,maxDewPointTimestamp);
        }
        else
        {
          Serial.println("Failed EEPROM update with max dew point!");
        }
      }
      if (dhtApproxDewPoint <= minDewPoint)
      {
        minDewPoint = dhtApproxDewPoint;
        dtostrf(minDewPoint,3,1,minDPChar);
        minDewPointTimestamp = timeNow;
        if (writeFloatToEEPROM(minDewPoint,minDewPointAddr))
        {
          Serial.println("Updated EEPROM with min dew point");
          EEPROM.put(minDewPointTimestamp,minDPTimeAddr);
        }
        else
        {
          Serial.println("Failed EEPROM update with min dew point!");
        }
      }
      strcpy(tempChar,"\0");
      if (dhtApproxDewPoint < 10.0)
      {
        strcpy(tempChar,"(a bit dry for some)");
      }
      else if ((dhtApproxDewPoint >= 10.0) && (dhtApproxDewPoint < 13.0))
      {
        strcpy(tempChar,"(very comfortable)");
      }
      else if ((dhtApproxDewPoint >= 13.0) && (dhtApproxDewPoint < 16.0))
      {
        strcpy(tempChar,"(comfortable)");
      }
      else if ((dhtApproxDewPoint >= 16.0) && (dhtApproxDewPoint < 17.0))
      {
        strcpy(tempChar,"(ok for most)");
      }
      else if ((dhtApproxDewPoint >= 17.0) && (dhtApproxDewPoint < 18.0))
      {
        strcpy(tempChar,"(getting a bit humid)");
      }
      else if ((dhtApproxDewPoint >= 18.0) && (dhtApproxDewPoint < 21.0))
      {
        strcpy(tempChar,"(somewhat uncomfortable)");
      }
      else if ((dhtApproxDewPoint >= 21.0) && (dhtApproxDewPoint < 24.0))
      {
        strcpy(tempChar,"(very humid, quite uncomfortable)");
      }
      else if ((dhtApproxDewPoint >= 24.0) && (dhtApproxDewPoint < 26.0))
      {
        strcpy(tempChar,"(extremely uncomfortable)");
      }
      else if (dhtApproxDewPoint >= 26.0)
      {
        strcpy(tempChar,"(deadly!)");
      }
      else
      {
        strcpy(tempChar,"\0");
      }
      
      lastDewPoint = dhtApproxDewPoint;
    }
    dtostrf(dhtApproxDewPoint,3,1,dpChar);
    // Check pressure sensor
    if (bmpStarted)
    {
      bmp.getEvent(&event);
      bmpPressure = event.pressure;
      dtostrf(bmpPressure,6,1,pChar);
      if (abs(bmpPressure-lastPressure) < (lastPressure*0.1))
      {
        if (bmpPressure >= maxPressure)
        {
          maxPressure = bmpPressure;
          dtostrf(maxPressure,6,1,maxPChar);
          maxPressureTimestamp = timeNow;
          if (writeFloatToEEPROM(maxPressure,maxPressureAddr))
          {
            Serial.println("Updated EEPROM with max pressure");
            EEPROM.put(maxPressTimeAddr,maxPressureTimestamp);
          }
          else
          {
            Serial.println("Failed EEPROM update with max pressure!");
          }
        }
        if (bmpPressure <= minPressure)
        {
          minPressure = bmpPressure;
          dtostrf(minPressure,6,1,minPChar);
          minPressureTimestamp = timeNow;
          if (writeFloatToEEPROM(minPressure,minPressureAddr))
          {
            Serial.println("Updated EEPROM with min pressure");
            EEPROM.put(minPressTimeAddr,minPressureTimestamp);
          }
          else
          {
            Serial.println("Failed EEPROM update with min pressure!");
          }
        }
        lastPressure = bmpPressure;
      }
    } // BMP is up & running
    else
    {
      bmpPressure = 0;
    }
    
    // check LDR
    ldrValue = analogRead(ldrPin);

    // check PIR sensor
    if ((digitalRead(pirPin) == HIGH) && ((pirWasTripped == false) || (backlightOn == false)))
    {
      pirWasTripped = true;
      lastMillisPIR = currentMillisTime;
      backlightOn = true;
      currentDisplay = maxDisplay+1; // loop back to first display
      Serial.println("PIR sensor was tripped");
    }
    else
    {
      // keep backlight on for backlightOnTime after it was triggered
      if ((currentMillisTime - lastMillisPIR) > backlightOnTime)
      {
        pirWasTripped = false;
        backlightOn = false;
        Serial.println("Switching backlight off");
      }
    }

    // for debugging
    Serial.print("   timeNowSyd: "); digitalClockDisplay(timeNowSyd);
    Serial.println();
    if (DEBUG_LEVEL == 0)
    {
      Serial.print("   hChar ..... "); Serial.println(hChar);
      Serial.print("   maxHChar .. "); Serial.print(maxHChar); Serial.print(" @ "); digitalClockDisplay(maxHumidityTimestamp); 
      Serial.println();
      Serial.print("   minHChar .. "); Serial.print(minHChar); Serial.print(" @ "); digitalClockDisplay(minHumidityTimestamp); 
      Serial.println();
      Serial.print("   tChar ..... "); Serial.println(tChar);
      Serial.print("   maxTChar .. "); Serial.print(maxTChar); Serial.print(" @ "); digitalClockDisplay(maxTempTimestamp); 
      Serial.println();
      Serial.print("   minTChar .. "); Serial.print(minTChar); Serial.print(" @ "); digitalClockDisplay(minTempTimestamp); 
      Serial.println();
      Serial.print("   tFarChar .. "); Serial.println(tFarChar);
      Serial.print("   DewPoint .. "); Serial.print(dpChar); Serial.print(" "); Serial.println(tempChar);
      Serial.print("   maxDPChar.. "); Serial.print(maxDPChar); Serial.print(" @ "); digitalClockDisplay(maxDewPointTimestamp); 
      Serial.println();
      Serial.print("   minDPChar.. "); Serial.print(minDPChar); Serial.print(" @ "); digitalClockDisplay(minDewPointTimestamp); 
      Serial.println();
      Serial.print("   pChar ..... "); Serial.println(pChar);
      Serial.print("   maxPChar .. "); Serial.print(maxPChar); Serial.print(" @ "); digitalClockDisplay(maxPressureTimestamp); 
      Serial.println();
      Serial.print("   minPChar .. "); Serial.print(minPChar); Serial.print(" @ "); digitalClockDisplay(minPressureTimestamp); 
      Serial.println();
      Serial.print("   Battery ... "); Serial.println(analogRead(batteryPin));
      Serial.print("   LDR ....... "); Serial.println(ldrValue);
      Serial.print("   Restart ... "); digitalClockDisplay(lastRestart);
      Serial.println();
    }
  }

  // Set backlight
  if (backlightOn)
    digitalWrite( LCD_BACKLIGHT_PIN, HIGH );
  else
    digitalWrite( LCD_BACKLIGHT_PIN, LOW );

  // Send data to web
  if (onLine)
  {
    wasOnline = true;
    if (currentMillisTime - lastHTTPPost > 60000) // every minute
    {
      lastHTTPPost = millis();
      readyToPost = true;
      client.stop();
      connectClient();
    }
  }
  else
  {
    readyToPost = false;
  }

  if (readyToPost)
  {
    if (client.connected())
    {
      wasOnline = true;
      Serial.println("Connected");
      waitingClientConnect = false;
      readyToPost = false;
      sendHTTPRequest(timeNow,ldrValue);
    }
  } // Post to web?

  // Incoming data for debugging?
  if (client.available())
  {
    if (DEBUG_LEVEL <= 1)
    {
      Serial.println("-------------------------------------------------------------");
      Serial.println("Response from server:\n");
      while (client.available())
      {
        char c = client.read();
        Serial.print(c);
      }
      Serial.println("-------------------------------------------------------------");
    }
  }

   //get the latest button pressed, also the buttonJustPressed, buttonJustReleased flags
   button = ReadButtons();
   switch( button )
   {
      case BUTTON_NONE:
      {
         break;
      }
      case BUTTON_RIGHT:
      {
        // display sunrise/sunset times
        currentDisplay = dispSunriseSunset;
        // reset display timer
        previousMillisSensorDisp = currentMillisTime;
        break;
      }
      case BUTTON_UP:
      {
        // Move to previous display
        currentDisplay -= 1;
        if (currentDisplay < dispTimeDate)
          currentDisplay = maxDisplay;
        // reset display timer
        previousMillisSensorDisp = currentMillisTime;
        break;
      }
      case BUTTON_DOWN:
      {
        currentDisplay += 1;
        if (currentDisplay > maxDisplay)
          currentDisplay = dispTimeDate;
         // reset display timer
        previousMillisSensorDisp = currentMillisTime;
        break;
      }
      case BUTTON_LEFT:
      {
        // display current temp/humidity
        currentDisplay = dispTempHum;
        // reset display timer
        previousMillisSensorDisp = currentMillisTime;
        break;
     }
     case BUTTON_SELECT:
     {
        // go back to first display to show current time
        currentDisplay = dispTimeDate;
         // reset display timer
        previousMillisSensorDisp = currentMillisTime;
        break;
      }
      default:
     {
        break;
     }
   }
}

/*--------------------------------------------------------------------------------------
  ReadButtons()
  Detect the button pressed and return the value
  Uses global values buttonWas, buttonJustPressed, buttonJustReleased.
--------------------------------------------------------------------------------------*/
// from http://www.freetronics.com.au/pages/16x2-lcd-shield-quickstart-guide
byte ReadButtons()
{
   unsigned int buttonVoltage;
   byte button = BUTTON_NONE;   // return no button pressed if the below checks don't write to btn
   
   //read the button ADC pin voltage
   buttonVoltage = analogRead( BUTTON_ADC_PIN );
   //sense if the voltage falls within valid voltage windows
   if( buttonVoltage < ( RIGHT_10BIT_ADC + BUTTONHYSTERESIS ) )
   {
      button = BUTTON_RIGHT;
   }
   else if(   buttonVoltage >= ( UP_10BIT_ADC - BUTTONHYSTERESIS )
           && buttonVoltage <= ( UP_10BIT_ADC + BUTTONHYSTERESIS ) )
   {
      button = BUTTON_UP;
   }
   else if(   buttonVoltage >= ( DOWN_10BIT_ADC - BUTTONHYSTERESIS )
           && buttonVoltage <= ( DOWN_10BIT_ADC + BUTTONHYSTERESIS ) )
   {
      button = BUTTON_DOWN;
   }
   else if(   buttonVoltage >= ( LEFT_10BIT_ADC - BUTTONHYSTERESIS )
           && buttonVoltage <= ( LEFT_10BIT_ADC + BUTTONHYSTERESIS ) )
   {
      button = BUTTON_LEFT;
   }
   else if(   buttonVoltage >= ( SELECT_10BIT_ADC - BUTTONHYSTERESIS )
           && buttonVoltage <= ( SELECT_10BIT_ADC + BUTTONHYSTERESIS ) )
   {
      button = BUTTON_SELECT;
   }
   //handle button flags for just pressed and just released events
   if( ( buttonWas == BUTTON_NONE ) && ( button != BUTTON_NONE ) )
   {
      //the button was just pressed, set buttonJustPressed, this can optionally be used to trigger a once-off action for a button press event
      //it's the duty of the receiver to clear these flags if it wants to detect a new button change event
      buttonJustPressed  = true;
      buttonJustReleased = false;
   }
   if( ( buttonWas != BUTTON_NONE ) && ( button == BUTTON_NONE ) )
   {
      buttonJustPressed  = false;
      buttonJustReleased = true;
   }
   
   //save the latest button value, for change event detection next time round
   buttonWas = button;
   
   return( button );
}

boolean checkDST(time_t t)
{
  // check whether to set DST or not
  // NSW: starts @ 2am on first Sunday in October
  //      ends @ 3am on first Sunday in April

  // Check if month >= 10 and day of week >= 1 and time >= 02:00 -> DST on
  //       if month >= 4 and day of week >= 1 and time >= 03:00 -> DST off
  // If it's Nov/Dec/Jan/Feb then we know DST is on regardless
  if ((month(t) > 10) || (month(t) < 4))
  {
    return true;
  }
  // If it's Oct, check the date & day of week
  else if (month(t) == 10)
  {
    // if it's the 8th or more, then DST is on
    if (day(t) > 7)
    {
      return true;
    }
    else
    {
      // if it's Sunday and 2am or later, then DST is on
      if ((dayOfWeek(t) <= day(t)) && (hour(t) >= 2))
      {
        return true;
      }
      else
      {
        return false;
      }
    }
  }
  // If it's April ...
  else if (month(t) == 4)
  {
    if (day(t) > 7)
    {return false;}
    else
    {
       // if it's Sunday and 3am or later, then DST is off
      if ((dayOfWeek(t) <= day(t)) && (hour(t) >= 3))
      {return false;}
      else
      {return true;}
    }
  }
  else
  {return false;}
}

unsigned long getNTPTime()
{
  if (DEBUG_LEVEL <= 1)
  {
    Serial.println();
    Serial.println("Checking NTP time");
  }
  if (onLine)
  {
    wasOnline = true;
    while (Udp.parsePacket() > 0) ; // discard any previous packets
    sendNTPPacket();
  
    uint32_t beginWait = millis();
    while (millis() - beginWait < 1500) 
    {
      int size = Udp.parsePacket();
      if (size >= NTP_PACKET_SIZE) 
      {
        if (DEBUG_LEVEL <= 1)
        {
          Serial.println("Received reply");
        }
        // We've received a packet, read the data from it
        Udp.read(packetBuffer,NTP_PACKET_SIZE);  // read the packet into the buffer
  
        // The timestamp starts at byte 40 of the received packet and is four bytes,
        // or two words, long. First, esxtract the two words:
        unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
        unsigned long lowWord  = word(packetBuffer[42], packetBuffer[43]);  
  
        // combine the four bytes (two words) into a long integer
        // this is NTP time (seconds since Jan 1 1900):
        unsigned long secsSince1900 = highWord << 16 | lowWord;
  
        // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
        const unsigned long seventyYears = 2208988800UL;     
  
        // subtract seventy years:
        unsigned long epoch = secsSince1900 - seventyYears;  

        if (DEBUG_LEVEL <= 1)
        {
          Serial.print("Got time from NTP: ");
          digitalClockDisplay(epoch);
          Serial.println(" (UTC)");
          Serial.print("                   ");
          digitalClockDisplay(epoch+TIMEZONE_OFFSET);
          Serial.println(" (SYD)");
        }
        
        rtcOrNTPSync = true; // time set from NTP
  
        return epoch;
      }
    }
  } // only do NTP check if online  
  Serial.println("No NTP response or not online, switching to RTC");
  return RTC.get(); // return time from RTC if unable to get the time from NTP
} 

// send an NTP request to the time server at the given address 
void sendNTPPacket()
{
  // set all bytes in the buffer to 0
  memset(packetBuffer,0,NTP_PACKET_SIZE); 
  // Initialize values needed to form NTP request
  packetBuffer[0] = 0b11100011; // LI, Version, Mode
  packetBuffer[1] = 0;          // Stratum, or type of clock
  packetBuffer[2] = 6;          // Polling Interval
  packetBuffer[3] = 0xEC;       // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49; 
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:        
  Udp.beginPacket(ntpServerName,123); // NTP requests are to port 123
  Udp.write(packetBuffer,NTP_PACKET_SIZE);
  Udp.endPacket(); 
  if (DEBUG_LEVEL <= 1)
  {
    Serial.print("Sent NTP request to ");
    Serial.println(ntpServerName);
  }
}

/*-----------------------*/
/* Time Formatting Start */
/*-----------------------*/
void digitalClockDisplay(time_t t)
{
  // digital clock display of the time
  printDigits(hour(t));
  Serial.print(":");
  printDigits(minute(t));
  Serial.print(":");
  printDigits(second(t));
  Serial.print(" ");
  Serial.print(day(t));
  Serial.print("/");
  Serial.print(month(t));
  Serial.print("/");
  Serial.print(year(t)); 
  Serial.print(" ");
  Serial.print(dayOfWeek[weekday(t)]);
}

void printDigits(int digits)
{
  // Print leading 0 if value is < 10
  if(digits < 10)
    Serial.print('0');
  Serial.print(digits);
}

void connectClient()
{
  String tempString = "";

  if (!client.connected())
  {
    tempString = "Connecting to "+String(SERVER_ADDRESS)+":"+String(HTTP_PORT);
    Serial.println(tempString);
    client.connect(SERVER_ADDRESS,HTTP_PORT);
    // Need to wait up to 30s for SYN -> SYN/ACK -> ACK
    waitingClientConnect = true;
  }
  else
  {
    waitingClientConnect = false;
  }
}

void sendHTTPRequest(time_t timeNow, int ldr)
{
  String tempString = "";
  String postHeader = "";
  String postBody = "";
  
  if (client.connected())
  {
    // The data we're sending goes in the POST body
    postBody = "timeStamp="+String(timeNow)+"&uptime="+String(lastRestart)+"&temperature="+String(tChar)+"&tempFar="+String(tFarChar)+"&humidity="+String(hChar)+"&pressure="+String(pChar)+"&ldr="+String(ldr)+"&maxTemp="+String(maxTChar)+"&maxTempTimestamp="+String(maxTempTimestamp)+"&minTemp="+String(minTChar)+"&minTempTimestamp="+String(minTempTimestamp);
    postBody = postBody + "&maxHumidity="+String(maxHChar)+"&maxHumidityTimestamp="+String(maxHumidityTimestamp)+"&minHumidity="+String(minHChar)+"&minHumidityTimestamp="+String(minHumidityTimestamp)+"&maxPressure="+String(maxPChar)+"&maxPressureTimestamp="+String(maxPressureTimestamp)+"&minPressure="+String(minPChar)+"&minPressureTimestamp="+String(minPressureTimestamp);
    postBody = postBody + "&dewPoint="+String(dpChar)+"&maxDewPoint="+String(maxDPChar)+"&maxDewPointTimestamp="+String(maxDewPointTimestamp)+"&minDewPoint="+String(minDPChar)+"&minDewPointTimestamp="+String(minDewPointTimestamp)+"&dewPointFeeling="+String(tempChar)+"&sunrise="+sunriseChar+"&sunset="+sunsetChar+"&sunriseTomorrow="+sunriseTomChar+"&sunsetTomorrow="+sunsetTomChar;

    // And now the POST header
    postHeader = "POST "+String(PHP_PAGE_LOCATION)+" HTTP/1.1\r\n";
    postHeader = postHeader+"Host: "+String(SERVER_ADDRESS)+"\r\n";
    postHeader = postHeader+"User-Agent: arduino-ethernet\r\n"; // 2-Mar-2016
    postHeader = postHeader+"Content-Type: application/x-www-form-urlencoded\r\n";
    postHeader = postHeader+"Content-Length: "+postBody.length()+"\r\n\r\n";

    // Send the data
    client.println(postHeader+postBody);

    if (DEBUG_LEVEL <= 1)
    {
      Serial.println("Connected!");
      Serial.println("+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++");
      Serial.println("Request sent to server:\n");
      Serial.print(postHeader+postBody);
      Serial.println("\n+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
    }
  } // Connected
  else
  {
    tempString = "Connection failed to "+String(SERVER_ADDRESS)+":"+String(HTTP_PORT);
    Serial.println(tempString);
  } // Couldn't connect
}

boolean writeFloatToEEPROM(float floatValue, int addr)
{
  float checkFloat = 0;
  boolean tempBool = false;

  EEPROM.put(addr,floatValue);
  EEPROM.get(addr,checkFloat);

  if (checkFloat == floatValue)
  {
    tempBool = true;
    EEPROM.write(eepromIDAddr,eepromID); // update the EEPROM ID to confirm EEPROM has valid data next restart
  }
  
  return tempBool;
}

// https://ag.arizona.edu/azmet/dewpoint.html
float calculateDewPoint(float T, float RH)
{
// B = (ln(RH / 100) + ((17.27 * T) / (237.3 + T))) / 17.27
//
// D = (237.3 * B) / (1 - B)
//
// where:
//        T = Air Temperature (Dry Bulb) in Centigrade (C) degrees
//       RH = Relative Humidity in percent (%)
//        B = intermediate value (no units) 
//        D = Dewpoint in Centigrade (C) degrees

  float B = 0;
  float D = 0;

  B = (log(RH/100) + ((17.27*T) / (237.3+T))) / 17.27;
  D = (237.3*B)/(1-B);

  return D;
}

// http://williams.best.vwh.net/sunrise_sunset_algorithm.htm
float calculateSunriseSunset(time_t timeNow, boolean tomorrow, boolean sunrise, float longitude, float latitude, int zenith)
{
  // zenith = 90d50' (official), 96 (civil), 102 (nautical), astronomical (108)
  
  int N, N1, N2, N3, timeOffset = 0;
  float t, lngHour, M, L, RA, lQuadrant, raQuadrant, sinDec, cosDec, cosH, H, T, UT, localT = 0;

  // sunrise == true  --> calculating sunrise
  // sunrise == false --> calculating sunset
  if (sunrise)
    {timeOffset = 6;}
  else
    {timeOffset = 18;}

  // 1. Calculate day of year
  N1 = floor(275 * month(timeNow) / 9);
  N2 = floor((month(timeNow) + 9) / 12);
  N3 = (1 + floor((year(timeNow) - 4 * floor(year(timeNow) / 4) + 2) / 3));
  N  = N1 - (N2 * N3) + day(timeNow) - 30;

  if (tomorrow)
    {N = N + 1;}

  // 2. Convert longitude to hour value & calculate an approximate time
  lngHour = longitude / 15;
  t = N + ((timeOffset - lngHour) / 24);

  // 3. Calculate the Sun's mean anomaly
  M = (0.9856 * t) - 3.289;

  // 4. Calculate the Sun's true longitude
  L = M + (1.916 * sin((M_PI/180) * M)) + (0.020 * sin((M_PI/180) * 2 * M)) + 282.634;
  if (L >= 360)
    {L = L - 360;}
  else if (L < 0)
    {L = L + 360;}

  // 5a. Calculate the Sun's Right Acension
  RA = (180/M_PI) * atan(0.91764 * tan((M_PI/180) * L));
  if (RA >= 360)
    {RA = RA - 360;}
  else if (RA < 0)
    {RA = RA + 360;}

  // 5b. RA value needs to be in the same quadrant as L
  lQuadrant  = (floor(L  / 90)) * 90;
  raQuadrant = (floor(RA / 90)) * 90;

  // 5c. RA value needs to be converted to hours
  RA = RA / 15;

  // 6. Calculate the Sun's declination
  sinDec = 0.39782 * sin((M_PI/180) * L);
  cosDec = cos(asin(sinDec));

  // 7a. Calculate the Sun's local hour angle
  cosH = (cos((M_PI/180) * zenith) - (sinDec * sin((M_PI/180) * latitude))) / (cosDec * cos((M_PI/180) * latitude));
  // if (cosH > 1), the sun never rises
  // if (cosH < -1), the sun never sets

  // 7b. Finish calculating H and convert to hours
  if (sunrise)
    {H = 360 - (180/M_PI) * acos(cosH);}
  else
    {H = (180/M_PI) * acos(cosH);}
  H = H / 15;

  // 8. Calculate local mean time of rising/setting
  T = H + RA - (0.06571 * t) - 6.622;

  // 9. Adjust back to UT
  UT = T - lngHour;

  // 10. Convert UT to local time zone
  if (daylightSavingOn)
    {localT = UT + 11;}
  else
    {localT = UT + 10;}
  
  if (localT >= 24)
    {localT = localT - 24.0;}
  if (localT < 0)
    {localT = localT + 24.0;} // 25-Mar-2016: avoid negative time, i.e. -5.06 for sunset when it should be 19:xx

  if ((DEBUG_LEVEL == 0) || (DEBUG_LEVEL == 2))
  {
    if (sunrise)
      {Serial.println("Calculating Sunrise");}
    else
      {Serial.println("Calculating Sunset");}
    Serial.print("N1 .......... "); Serial.println(N1);
    Serial.print("N2 .......... "); Serial.println(N2);
    Serial.print("N3 .......... "); Serial.println(N3);
    Serial.print("N ........... "); Serial.println(N);
    Serial.print("lngHour ..... "); Serial.println(lngHour);
    Serial.print("t ........... "); Serial.println(t);
    Serial.print("M ........... "); Serial.println(M);
    Serial.print("L ........... "); Serial.println(L);
    Serial.print("RA .......... "); Serial.println(RA);
    Serial.print("lQuadrant ... "); Serial.println(lQuadrant);
    Serial.print("raQuadrant .. "); Serial.println(raQuadrant);
    Serial.print("sinDec....... "); Serial.println(sinDec);
    Serial.print("cosDec....... "); Serial.println(cosDec);
    Serial.print("cosH ........ "); Serial.println(cosH);
    Serial.print("H ........... "); Serial.println(H);
    Serial.print("T ........... "); Serial.println(T);
    Serial.print("UT .......... "); Serial.println(UT);
    Serial.print("localT ...... "); Serial.println(localT);
    
  }

  return localT;

}

int printMinutes(float decimalTime)
{
  // convert 6.88 to 6hr 52min
  int integerPart = (int)decimalTime;
  int minutes = (decimalTime-integerPart) * 60;

  return minutes;
}

String printSunriseSunsetTime(float sunriseSunsetTime)
{
  String timeAsString = "";
  int minutes = 0;
  int hours = (int)sunriseSunsetTime;
  
  if (sunriseSunsetTime < 10)
    {timeAsString = String(0);}

  minutes = printMinutes(sunriseSunsetTime);

  timeAsString = String(timeAsString + String(hours) + ":");

  if (minutes < 10)
    {timeAsString = String(timeAsString + String(0));}

  timeAsString = String(timeAsString + minutes);

  return timeAsString;
}
