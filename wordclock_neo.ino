





/**************************************************************************
 *                                                                         *
 *  The British W O R D C L O C K   -                                      *
 *  A clock that tells the time using words.                               *
 *                                                                         *
 * Hardware: Custom Arduino code inspired by dougswordclocks               *
 *            English Desgn and English Hardware                           *
 *                                                                         *
 *   Copyright (C) 2014  Battle VA                                         *
 * commands are shifter.clear()*
 *              shifter.write()*
 *              settimeleds(1, sizeof()) or LOW*
 *                                                                         *
 *                                                                         
 *   To Do, modify  to use fastled and remove shifter                                                                      
 *   modify for use on esp8266
 *   include wifimanager with parameters
 *   set up for mqtt listnen to allow clock to be set
 *   set up for mqtt to change pallets seasonally
 *   add dallas one wire temperature
 *   mqtt temperature
 *   set display to show temperature using words on button press
 *   
 ***************************************************************************
 */

#define FASTLED_ESP8266_RAW_PIN_ORDER
#include "FastLED.h"
FASTLED_USING_NAMESPACE

//extern "C" {
//#include "user_interface.h"  -- dont know what this is
//}
#include <DS1302.h> //RTC Library
//#include <Shifter.h> //74 Shift library  - not required anymore
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <OneWire.h>
//#include <DallasTemperature.h> //on LostElements Git
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#include <FS.h>
#include <EEPROM.h>
//#include "GradientPalettes.h"
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <PubSubClient.h> //on Lostelemnts Git

// Define all the constants
//define set name of yor bot
String botname = "neoclock";
char sign_name[10]; //name no spaces or special charcters
char mqtt_port[6] = "8080";
char mqtt_server[40];
//flag for saving data
bool shouldSaveConfig = false;
std::unique_ptr<ESP8266WebServer> server;

#define ONE_WIRE_BUS            D4      // DS18B20 pin
#define DATA_PIN      D5     // Leds pin
#define LED_TYPE      WS2812B
#define COLOR_ORDER   GRB
// Set your number of leds here!
#define NUM_LEDS      56

#define EEPROM_BRIGHTNESS      0
#define EEPROM_PATTERN      1
#define EEPROM_SOLID_R      2
#define EEPROM_SOLID_G      3
#define EEPROM_SOLID_B      4
#define EEPROM_PALETTE      5
#define EEPROM_LIT      6
#define EEPROM_BIG      7
#define MILLI_AMPS         1500     // IMPORTANT: set here the max milli-Amps of your power supply 5V 2A = 2000
#define AP_POWER_SAVE      1   // Set to 0 if you do not want the access point to shut down after 10 minutes of unuse
#define FRAMES_PER_SECOND  120 // here you can control the speed. With the Access Point / Web Server the animations run a bit slower.

#define BUFFER_SIZE 100 // for call back mqtt

CRGB leds[NUM_LEDS];
int lit = NUM_LEDS;
// we may not need all this 
uint8_t patternIndex = 0;
const uint8_t brightnessCount = 5;
uint8_t brightnessMap[brightnessCount] = { 16, 32, 64, 128, 255 };
int brightnessIndex = 0;
uint8_t brightness = brightnessMap[brightnessIndex];
#define ARRAY_SIZE(A) (sizeof(A) / sizeof((A)[0]))
// ten seconds per color palette makes a good demo
// 20-120 is better for deployment
uint8_t secondsPerPalette = 10;
// SPARKING: What chance (out of 255) is there that a new spark will be lit?
// Higher chance = more roaring fire.  Lower chance = more flickery fire.
// Default 120, suggested range 50-200.
uint8_t sparking = 60;
uint8_t speed = 30;
// Current palette number from the 'playlist' of color palettes
uint8_t gCurrentPaletteNumber = 0;

CRGBPalette16 gCurrentPalette( CRGB::Black);
CRGBPalette16 gTargetPalette( gGradientPalettes[0] );
CRGBPalette16 IceColors_p = CRGBPalette16(CRGB::Black, CRGB::Blue, CRGB::Aqua, CRGB::White);
uint8_t currentPatternIndex = 0; // Index number of which pattern is current
uint8_t autoplay = 0;
uint8_t autoplayDuration = 10;
unsigned long autoPlayTimeout = 0;
uint8_t currentPaletteIndex = 0;
uint8_t gHue = 0; // rotating "base color" used by many of the patterns

CRGB solidColor = CRGB::Black;
void dimAll(byte value)
{
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i].nscale8(value);
  }
}

typedef void (*Pattern)();
typedef Pattern PatternList[];
typedef struct {
  Pattern pattern;
  String name;
} PatternAndName;
typedef PatternAndName PatternAndNameList[];

#include "Twinkles.h"
#include "TwinkleFOX.h"

uint8_t power = 1;
uint8_t glitter = 0;
// To HERE?



// Shifter pin Assignments
//const int SER_Pin = 4;  //SER_IN
//const int RCLK_Pin = 3; //L_CLOCK
//const int SRCLK_Pin = 2; //Clock
//const int NUM_REGISTERS = 3; //number of shift registers

// Display pin assignments
// Replaced With Arrays For NEO Version

//const int MINUTES = 1;
int MINUTES[3] ={9,10,11};
//const int MTEN = 2; 
int MTEN[2] ={5,7};
//const int HALF = 3;
int HALF[2] ={16,17};
//const int PAST = 4; 
int PAST[2] ={18,19};
//const int THREE = 5;
int THREE[2] ={30.31};
//const int THETIMEIS = 6;
int THETIMEIS[4] = {0,1,2,3};

//const int TWENTY = 7;
int TWENTY[3] ={13,14,15};
//const int TO = 8;
int TO[1] ={8};
//const int TWO = 9;
int TWO[2] ={22,23};
//const int SIX = 19;
int SIX[2] ={20,21};
//const int TWELVE = 11;
int TWELVE[2] ={43,44};
//const int FIVE = 12;
int FIVE[2] ={28,29};
//const int SEVEN = 13;
int SEVEN[2] ={36,37};
//const int OCLOCK = 14;
int OCLOCK[3] ={40,41,42};
//const int ONE = 15;
int ONE[2] ={24,25};
//const int QUARTER = 16;
int QUARTER[2] ={4,5};
//const int EIGHT = 17;
int EIGHT[2] ={32,33};
//const int MFIVE = 18;
int MFIVE[1] ={12};
//const int MORNING = 0;
int MORNING[3] ={51,52,53};
//const int ELEVEN = 20;
int ELEVEN[3] ={45,46,47};
//const int TEN = 21;
int TEN[2] ={26,27};
//const int NINE = 22;
int NINE[2] ={38,39};
//const int FOUR = 23;
int FOUR[2] ={34,35};
//const int AFTERNOON = 10;
int AFTERNOON[2] ={54,55};
int INTHE[2] = [49,50];


int  hour=3, minute=38, second=00;
static unsigned long msTick =0;  // the number of Millisecond Ticks since we last 
// incremented the second counter
int  count;
int  selftestmode;          // 1 = in self test - flash display
//int  DS1302Present=1;       // flag to indicate that the 1302 is there..    1 = present
char Display1=0, Display2=0, Display3=0, Led1=0, Led2=0, Led3=0, Led4=0;
int  OldHardware = 0;  // 1 = we are running on old hardwrae
int  BTNActive = 1;    // the sense of the button inputs (Changes based on hardware type)

                        


// buttons
int FWDButtonPin=5;
int REVButtonPin=7;

// 1302 RTC Constants
// Change these to use the 680
int DS1302IOPin=10;
int DS1302CEPin=8;
int DS1302CLKPin=9;




/* Create buffers */
char buf[50]; // time output string for debugging

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}


//initaize shifter using the Shifter library
//Shifter shifter(SER_Pin, RCLK_Pin, SRCLK_Pin, NUM_REGISTERS);

// create an object that talks to the RTC
// Add in the 680 
//DS1302 rtc(DS1302CEPin, DS1302IOPin, DS1302CLKPin);

void print_DS1302time()
{
  /* Get the current time and date from the chip */
  //change to get nist time 
  //Time t = rtc.time();

  /* Format the time and date and insert into the temporary buffer */
  snprintf(buf, sizeof(buf), "DS1302 time: %02d:%02d:%02d",
  t.hr, t.min, t.sec);

  /* Print the formatted string to serial so we can see the time */
  Serial.println(buf);

}



void setup()
{
  // initialise the hardware	
  // initialize the appropriate pins as outputs:
 // pinMode(SER_Pin, OUTPUT); 
 // pinMode(RCLK_Pin, OUTPUT); 
 // pinMode(SRCLK_Pin, OUTPUT); 
  pinMode(FWDButtonPin, INPUT); 
  pinMode(REVButtonPin, INPUT); 

  //  setup 1302
  // change to bm680 pins
  pinMode(DS1302IOPin, OUTPUT); 
  pinMode(DS1302CEPin, OUTPUT); 
  pinMode(DS1302CLKPin, OUTPUT); 

  

  Serial.begin(115200);   // setup the serial port to 9600 baud
  delay(100);
  Serial.setDebugOutput(true);
  SWversion ();

   FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);         // for WS2812 (Neopixel)
  //FastLED.addLeds<LED_TYPE,DATA_PIN,CLK_PIN,COLOR_ORDER>(leds, NUM_LEDS); // for APA102 (Dotstar)
  FastLED.setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(brightness);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, MILLI_AMPS);
  fill_solid(leds, NUM_LEDS, solidColor);
  FastLED.show();

  EEPROM.begin(512);
  loadSettings();

  FastLED.setBrightness(brightness);

  Serial.println();
  Serial.print( F("Heap: ") ); Serial.println(system_get_free_heap_size());
  Serial.print( F("Boot Vers: ") ); Serial.println(system_get_boot_version());
  Serial.print( F("CPU: ") ); Serial.println(system_get_cpu_freq());
  Serial.print( F("SDK: ") ); Serial.println(system_get_sdk_version());
  Serial.print( F("Chip ID: ") ); Serial.println(system_get_chip_id());
  Serial.print( F("Flash ID: ") ); Serial.println(spi_flash_get_id());
  Serial.print( F("Flash Size: ") ); Serial.println(ESP.getFlashChipRealSize());
  Serial.print( F("Vcc: ") ); Serial.println(ESP.getVcc());
  Serial.println();


 SPIFFS.begin();
  {
    // Open Our config and read
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");

          strcpy(sign_name, json["sign_name"]);
          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_port, json["mqtt_port"]);
        

        } else {
          Serial.println("failed to load json config");
        }
      }
    }
    Dir dir = SPIFFS.openDir("/");
    while (dir.next()) {
      String fileName = dir.fileName();
      size_t fileSize = dir.fileSize();
      Serial.printf("FS File: %s, size: %s\n", fileName.c_str(), String(fileSize).c_str());
    }
    Serial.printf("\n");
  }
    // id/name placeholder/prompt default length
  WiFiManagerParameter custom_sign_name("name", "Sign Name", sign_name, 10);
  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 6);
  
  
  WiFiManager wifimanager;
  //wifimanager.setAPStaticIPConfig(IPAddress(10,0,1,1), IPAddress(10,0,1,1), IPAddress(255,255,255,0));
  //reset settings for testing only
   //wifimanager.resetSettings();
  //set config save notify callback
  wifimanager.setSaveConfigCallback(saveConfigCallback);
  //add all your parameters here
  wifimanager.addParameter(&custom_sign_name);
  wifimanager.addParameter(&custom_mqtt_server);
  wifimanager.addParameter(&custom_mqtt_port);
 
  //wifimanager.autoConnect("AutoConnectAP");
  Serial.println("connected...yeey before autoconnect:)");
  wifimanager.autoConnect();
  
   //if you get here you have connected to the WiFi
    Serial.println("connected...yeey :)");
    //read updated parameters
  strcpy(sign_name, custom_sign_name.getValue());
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
 
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["sign_name"] = sign_name;
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
   

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }

    server.reset(new ESP8266WebServer(WiFi.localIP(), 80));
    Serial.print("Connected! Open http://");
    Serial.print(WiFi.localIP());
    Serial.println(" in your browser");
 // WiFi.hostname(mdns_hostname);
   WiFi.hostname(sign_name);
 // Start MDNS using spiffs defined name
   MDNS.begin(sign_name);
   MDNS.addService("http", "tcp", 80);
    Serial.println("mdns started");
WiFiClient wclient;
PubSubClient client(wclient, mqtt_server);

//define set name of your sign
//String signname = "Room1"; //should be loaded from spiffs
//define mqtt message names
String thistemp = "ledclock\\" + String(sign_name);//signname;

/* Only needed if listening mqtt
void callback(const MQTT::Publish& pub) {
  // handle message arrived use strcmp on multiple subscriptions
   Serial.print(pub.topic());
  Serial.print(" => ");
  if (pub.has_stream()) {
    uint8_t buf[BUFFER_SIZE];
    int read;
    while (read = pub.payload_stream()->read(buf, BUFFER_SIZE)) {
      Serial.write(buf, read);
    }
    pub.payload_stream()->stop();
    Serial.println("");
  } else
    Serial.println(pub.payload_string());
if (strcmp(pub.topic().c_str(),dinner.c_str())==0){
  
      animate_face(1);//Eating Animation
     
}
if (strcmp(pub.topic().c_str(),dick.c_str())==0){
      String face1 = pub.payload_string();
      
      animate_face(face1.toInt());//Dick Animation
 }
if (strcmp(pub.topic().c_str(),messages.c_str())== 0){
   ledMatrix.setText(pub.payload_string());
  while  (digitalRead(buttonPin) == LOW){
  ledMatrix.clear();
    ledMatrix.scrollTextLeft();
    ledMatrix.drawText();
    ledMatrix.commit();
    delay(100);
    sendtemp();
   }
 }
}
*/
void reconnect() {

  // Loop until we're reconnected  - change to same as basic, try one
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(botname)) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
     /* Serial.print(client.state());*/
      Serial.println(" try on next loop");     
    }
}
void sendtemp(){
  // Change this to send the temp etc from the 680 and also display it on the clock every so often coloured to temperatture
   unsigned long now = millis();
  // function to send the temperature every five minutes rather than leavingb in the loop
   if (now - lastSampleTime >= fiveMinutes)
  {
     float temperature = getTemperature();
  // convert temperature to a string with two digits before the comma and 2 digits for precision
  dtostrf(temperature, 2, 2, temperatureString);
  // send temperature to the serial console
  Serial.println ("Sending temperature: ");
  Serial.println (temperatureString);
  // send temperature to the MQTT topic every 5 minutes
     client.publish(roomtemp, temperatureString);
  //lastSampleTime = now + fiveMinutes;
  lastSampleTime += fiveMinutes;
   // add code to take scroll temperatre 4 times then reset face to static  (temperature is 5 characters * 8  for each scroll
  
    delay(100);
   
  }
}

void settimeleds( int b[], int sizeOfArray ){
   // put a loop in here to set the leds based on the strings, pass string and length
   // add in allways on the time is and in the so we dont need to keep calling unless showing temp
   for (int k = 0 ; k <sizeOfArray ; ++k )
   leds[k] = CRGB::Cornsilk;
  //FastLED.clear();
  // Now, turn on the "It is" leds and timeis
  leds[0] = CRGB::Cornsilk;
  leds[1] = CRGB::Cornsilk;
  leds[2] = CRGB::Cornsilk;
  leds[3] = CRGB::Cornsilk;
  leds[49] = CRGB::Cornsilk;
  leds[50] = CRGB::Cornsilk;

}
void settempleds( int b[], int sizeOfArray ){
   // put a loop in here to set the leds based on the strings, pass string and length
   // add in allways on the time is and in the so we dont need to keep calling unless showing temp
   for (int k = 0 ; k <sizeOfArray ; ++k )
   leds[k] = CRGB::Cornsilk;
  //FastLED.clear();
 //set colour based on temp

}

///////////////////////////////////////////////////////////////////////////////////////////////

  // test whether the DS1302 is there
  //Serial.print("Verifying DS1302 ");
  // start by verifying that the chip has a valid signature
/*  if (rtc.read_register(0x20) == 0xa5) {
    // Signature is there - set the present flag and mmove on
    DS1302Present=1;
    Serial.println("present - Valid Signature");
    rtc.write_protect(false);
    rtc.halt(false);
  }
  else 
  {
    // Signature isnt there - may be a new chip - 
    //   do a write to see if it will hold the signature 
    rtc.write_register(0x20,0xa5);
    if (rtc.read_register(0x20) == 0xa5) {
      // We can store data - assume that it is a new chip that needs initialisation
      // Start by turning off write protection and clearing the clock halt flag.
      rtc.write_protect(false);
      rtc.halt(false);
      // Make a new time object to set the date and time 
      Time t(2010, 04, 28, 10, 30, 10, 01);
      // Set the time and date on the chip 
      rtc.time(t);
      // set the DS1302 present flag
      DS1302Present=1;
      Serial.println("present - new chip initialised.");
    }
    else  Serial.println("absent");
  }  
*/
  // determine whether we are running on old or new hardware
  // old hardware tied the push buttons to ground using 4k7 resistors
  // and relied on the buttons to pull them high
  // new hardware uses internal pullups, and uses the buttons
  // to pull the inputs down.

  digitalWrite(FWDButtonPin, sizeof());  // Turn on weak pullups
  digitalWrite(REVButtonPin, sizeof());  // Turn on weak pullups

  OldHardware=0;
  if ( digitalRead(FWDButtonPin)==0 && digitalRead(REVButtonPin)==0)
  {
    Serial.println("Detected Old Hardware");
    OldHardware=1;  // we have old hardware
    BTNActive = 1; // True = active for old hardware
    digitalWrite(FWDButtonPin,LOW);  // Turn off weak pullups
    digitalWrite(REVButtonPin,LOW);  // Turn off weak pullups

  }
  else
  {
    Serial.println("Detected New Hardware");
    OldHardware=0;  // we have old hardware
    BTNActive = 0; // True = active for old hardware
  }


 


  msTick=millis();      // Initialise the msTick counter

  selftest();  // validate the hardware for the user

  selftestmode=0;

// change this to get nist time is connected to the internet
  if (DS1302Present==1) {
    // Get the current time and date from the chip 
    Time t = rtc.time();
    second=t.sec;     
    minute=t.min;
    hour=t.hr; 
  }

  displaytime();        // display the current time






}






// change tyo use fastled and in the
void selftest(void){
  // start by clearing the display to a known state
  FastLED.clear();
  // now light each led set in turn
  settimeleds (THETIMEIS, sizeof(THETIMEIS));
  FastLED.show();
  delay(500); 
  
  settimeleds(MTEN, sizeof(MTEN));
  FastLED.show();   
  delay(500); 
  
  settimeleds(HALF, sizeof(HALF));
  FastLED.show();    
  delay(500); 
  
  settimeleds(TWENTY, sizeof(TWENTY));
  FastLED.show();  
  delay(500); 
  
  settimeleds(QUARTER, sizeof(QUARTER));
  FastLED.show(); 
  delay(500); 
 
  settimeleds(MFIVE, sizeof(MFIVE));
  FastLED.show();   
  delay(500); 
  
  settimeleds(MINUTES, sizeof(MINUTES));
  FastLED.show(); 
  delay(500); 
  
  settimeleds(PAST, sizeof(PAST));
  FastLED.show();    
  delay(500); 
 
  settimeleds(TO, sizeof(TO));
  FastLED.show();      
  delay(500); 
  
  settimeleds(ONE, sizeof(ONE));
  FastLED.show();     
  delay(500); 
 
  settimeleds(TWO, sizeof(TWO));
  FastLED.show();     
  delay(500); 
  
  settimeleds(THREE, sizeof(THREE));
  FastLED.show();   
  delay(500); 
 
  settimeleds(FOUR, sizeof(FOUR));
  FastLED.show();    
  delay(500); 
  
  settimeleds(FIVE, sizeof(FIVE));
  FastLED.show();   
  delay(500); 
  
  settimeleds(SIX, sizeof(SIX));
  FastLED.show();     
  delay(500); 
  
  settimeleds(SEVEN, sizeof(SEVEN));
  FastLED.show();   
  delay(500); 
  
  settimeleds(EIGHT, sizeof(EIGHT));
  FastLED.show();   
  delay(500); 
  
  settimeleds(NINE, sizeof(NINE));
  FastLED.show();    
  delay(500); 
  
  settimeleds(TEN, sizeof(TEN));
  FastLED.show();    
  delay(500); 
 
  settimeleds(ELEVEN, sizeof(ELEVEN));
  FastLED.show();  
  delay(500); 
  
  settimeleds(TWELVE, sizeof(TWELVE));
  FastLED.show();  
  delay(500); 
 
  settimeleds(OCLOCK, sizeof(OCLOCK));
  FastLED.show();  
  delay(500); 

  settimeleds(INTHE, sizeof(INTHE));
  FastLED.show();    
  delay(500); 
  
  settimeleds(MORNING, sizeof(MORNING));
  FastLED.show();    
  delay(500); 
  
  settimeleds(AFTERNOON, sizeof(AFTERNOON));
  FastLED.show(); 
  delay(500); 
  

  

}

// change to use fast led and it iis
void displaytime(void){

  // start by clearing the led array  to a known state
  
  FastLED.clear();

  // Now, turn on the "It is" leds
  // settimeleds(THETIMEIS, sizeof());

  // now we display the appropriate minute counter
  if ((minute>4) && (minute<10)) { 
    settimeleds(MFIVE, sizeof());
    settimeleds(MINUTES, sizeof()); 
    Serial.print("Five Minutes ");
  } 
  if ((minute>9) && (minute<15)) { 
    settimeleds(MTEN, sizeof()); 
    settimeleds(MINUTES, sizeof()); 
    Serial.print("Ten Minutes ");
  }
  if ((minute>14) && (minute<20)) {
    settimeleds(QUARTER, sizeof()); 
    Serial.print("Quarter ");
  }
  if ((minute>19) && (minute<25)) { 
    settimeleds(TWENTY, sizeof()); 
    settimeleds(MINUTES, sizeof()); 
    Serial.print("Twenty Minutes ");
  }
  if ((minute>24) && (minute<30)) { 
    settimeleds(TWENTY, sizeof()); 
    settimeleds(MFIVE, sizeof()); 
    settimeleds(MINUTES, sizeof());
    Serial.print("Twenty Five Minutes ");
  }  
  if ((minute>29) && (minute<35)) {
    settimeleds(HALF, sizeof());
    Serial.print("Half ");
  }
  if ((minute>34) && (minute<40)) { 
    settimeleds(TWENTY, sizeof()); 
    settimeleds(MFIVE, sizeof()); 
    settimeleds(MINUTES, sizeof());
    Serial.print("Twenty Five Minutes ");
  }  
  if ((minute>39) && (minute<45)) { 
    settimeleds(TWENTY, sizeof()); 
    settimeleds(MINUTES, sizeof()); 
    Serial.print("Twenty Minutes ");
  }
  if ((minute>44) && (minute<50)) {
    settimeleds(QUARTER, sizeof()); 
    Serial.print("Quarter ");
  }
  if ((minute>49) && (minute<55)) { 
    settimeleds(MTEN, sizeof()); 
    settimeleds(MINUTES, sizeof()); 
    Serial.print("Ten Minutes ");
  } 
  if (minute>54) { 
    settimeleds(MFIVE, sizeof()); 
    settimeleds(MINUTES, sizeof()); 
    Serial.print("Five Minutes ");
  }



  if ((minute <5))
  {
    switch (hour) {
    case 1:
    case 13: 
      settimeleds(ONE, sizeof()); 
      Serial.print("One ");
      break;
    case 2:
    case 14: 
      settimeleds(TWO, sizeof()); 
      Serial.print("Two ");
      break;
    case 3: 
    case 15:
      settimeleds(THREE, sizeof()); 
      Serial.print("Three ");
      break;
    case 4: 
    case 16:
      settimeleds(FOUR, sizeof()); 
      Serial.print("Four ");
      break;
    case 5: 
    case 17:
      settimeleds(FIVE, sizeof()); 
      Serial.print("Five ");
      break;
    case 6: 
    case 18:
      settimeleds(SIX, sizeof()); 
      Serial.print("Six ");
      break;
    case 7: 
    case 19:
      settimeleds(SEVEN, sizeof()); 
      Serial.print("Seven ");
      break;
    case 8: 
    case 20:
      settimeleds(EIGHT, sizeof()); 
      Serial.print("Eight ");
      break;
    case 9: 
    case 21:
      settimeleds(NINE, sizeof()); 
      Serial.print("Nine ");
      break;
    case 10:
    case 22: 
      settimeleds(TEN, sizeof()); 
      Serial.print("Ten ");
      break;
    case 11:
    case 23: 
      settimeleds(ELEVEN, sizeof()); 
      Serial.print("Eleven ");
      break;
    case 0:
    case 12: 
      settimeleds(TWELVE, sizeof()); 
      Serial.print("Twelve ");
      break;
    }
    settimeleds(OCLOCK, sizeof());
    Serial.println("O'Clock");
  }
  else
    if ((minute < 35) && (minute >4))
    {
      settimeleds(PAST, sizeof());
      Serial.print("Past ");
      switch (hour) {
      case 1:
      case 13: 
        settimeleds(ONE, sizeof()); 
        Serial.println("One ");
        break;
      case 2: 
      case 14:
        settimeleds(TWO, sizeof()); 
        Serial.println("Two ");
        break;
      case 3: 
      case 15:
        settimeleds(THREE, sizeof()); 
        Serial.println("Three ");
        break;
      case 4: 
      case 16:
        settimeleds(FOUR, sizeof()); 
        Serial.println("Four ");
        break;
      case 5: 
      case 17:
        settimeleds(FIVE, sizeof()); 
        Serial.println("Five ");
        break;
      case 6: 
      case 18:
        settimeleds(SIX, sizeof()); 
        Serial.println("Six ");
        break;
      case 7: 
      case 19:
        settimeleds(SEVEN, sizeof()); 
        Serial.println("Seven ");
        break;
      case 8: 
      case 20:
        settimeleds(EIGHT, sizeof()); 
        Serial.println("Eight ");
        break;
      case 9: 
      case 21:
        settimeleds(NINE, sizeof()); 
        Serial.println("Nine ");
        break;
      case 10:
      case 22: 
        settimeleds(TEN, sizeof()); 
        Serial.println("Ten ");
        break;
      case 11:
      case 23: 
        settimeleds(ELEVEN, sizeof()); 
        Serial.println("Eleven ");
        break;
      case 0:
      case 12: 
        settimeleds(TWELVE, sizeof()); 
        Serial.println("Twelve ");
        break;
      }
    }
    else
    {
      // if we are greater than 34 minutes past the hour then display
      // the next hour, as we will be displaying a 'to' sign
      settimeleds(TO, sizeof());
      Serial.print("To ");
      switch (hour) {
      case 1: 
      case 13:
        settimeleds(TWO, sizeof()); 
        Serial.println("Two ");
        break;
      case 14:
      case 2: 
        settimeleds(THREE, sizeof()); 
        Serial.println("Three ");
        break;
      case 15:
      case 3: 
        settimeleds(FOUR, sizeof()); 
        Serial.println("Four ");
        break;
      case 4: 
      case 16:
        settimeleds(FIVE, sizeof()); 
        Serial.println("Five ");
        break;
      case 5: 
      case 17:
        settimeleds(SIX, sizeof()); 
        Serial.println("Six ");
        break;
      case 6: 
      case 18:
        settimeleds(SEVEN, sizeof()); 
        Serial.println("Seven ");
        break;
      case 7: 
      case 19:
        settimeleds(EIGHT, sizeof()); 
        Serial.println("Eight ");
        break;
      case 8: 
      case 20:
        settimeleds(NINE, sizeof()); 
        Serial.println("Nine ");
        break;
      case 9: 
      case 21:
        settimeleds(TEN, sizeof()); 
        Serial.println("Ten ");
        break;
      case 10: 
      case 22:
        settimeleds(ELEVEN, sizeof()); 
        Serial.println("Eleven ");
        break;
      case 11: 
      case 23:
        settimeleds(TWELVE, sizeof()); 
        Serial.println("Twelve ");
        break;
      case 0:
      case 12: 
        settimeleds(ONE, sizeof()); 
        Serial.println("One ");
        break;
      }
    }

// Now set the AM or PM
 if (hour >= 12 && hour <= 23){
     settimeleds(AFTERNOON, sizeof());
     Serial.println("afternoon");
     Serial.println(hour);
 } 
 else{
   settimeleds(MORNING, sizeof());
   Serial.println("morning");
 }

// Now show the time
FastLED.show();

  


}


void incrementtime(void){
  // increment the time counters keeping care to rollover as required
  second=0;
  if (++minute >= 60) {
    minute=0;
    if (++hour == 25) {
      hour=1;  
    }
  }
  // debug outputs
  Serial.println();
  if (DS1302Present==1) print_DS1302time(); 
  else Serial.print("Arduino Time: " );
  Serial.print(hour);
  Serial.print(":");
  Serial.print(minute);
  Serial.print(":");
  Serial.println(second);

}


void SWversion(void) {
  delay(2000);
  Serial.println("British Word Clock copyright 2014 BattleVA");
  
}


void loop(void)
{
///////////////////////////////////////////////////////////////////////////////////////////////////
// put your main code here, to run repeatedly:
   client.set_callback(callback);
   if (!client.connected()) {
    reconnect();
  }
  

  client.loop();
 
  
 // client.publish(roomtemp, temperatureString);
  //client.subscribe(messages);
  //client.subscribe(dinner);
  //client.subscribe(dick);
  sendtemp();
  ////////////////////////////////////////////////////////////////////////////////////////////////////////
  //Serial.println("Loop Started");
  
  // heart of the timer - keep looking at the millisecond timer on the Arduino
  // and increment the seconds counter every 1000 ms
  if ( millis() - msTick >999) {
    msTick=millis();
    second++;
    // Flash the onboard Pin13 Led so we know something is hapening!
    digitalWrite(13, sizeof());
    delay(50);
    digitalWrite(13,LOW);  
    delay(50);
    digitalWrite(13, sizeof());
    delay(50);
    digitalWrite(13,LOW);  

   // Serial.print(second);
  //  Serial.print("..");

  }



  //test to see if we need to increment the time counters
  if (second==60) 
  {
    incrementtime();
    displaytime();
  }
// change to test nist time once per day
  if (DS1302Present==1) {
    // Get the current time and date from the chip 
    Time t = rtc.time();
    second=t.sec;     
    minute=t.min;
    hour=t.hr; 
  }

 
  

  // test to see if both buttons are being held down
  // if so  - start a self test till both buttons are held
  // down again.
  if ( digitalRead(FWDButtonPin)==BTNActive && digitalRead(REVButtonPin)==BTNActive)
  {
    selftestmode = !selftestmode;
    if (selftestmode) Serial.println("Selftest Mode TRUE");
    else Serial.println("Selftest mode FALSE");
  }

  //    if (selftestmode) { 
  //      for(int i=0; i<100; i++)
  //      {
  //      Display1=255; Display2=255; Display3=255;
  //      WriteLEDs(); delay(101-i);
  //      ledsoff(); WriteLEDs();delay(101-i);
  //      if (digitalRead(FWDButtonPin)==1) selftestmode=!selftestmode;
  //     
  //      }
  //      displaytime();
  //      
  //    }


  // test to see if a forward button is being held down
  // for time setting
  if (digitalRead(FWDButtonPin) ==BTNActive ) 
    // the forward button is down
    // and it has been more than one second since we
    // last looked

  {
    Serial.println("Forward Button Down");
    //minute=(((minute/5)*5) +5); 
    incrementtime();
    second++;  // Increment the second counter to ensure that the name
    // flash doesnt happen when setting time
    if (DS1302Present==1) {
      // Make a new time object to set the date and time 
      Time t(2010, 04, 28, hour, minute, second, 01);
      // Set the time and date on the chip 
      rtc.time(t);
    } 
    delay(100);
    displaytime();
  }

  // test to see if the back button is being held down
  // for time setting
  if (digitalRead(REVButtonPin)==BTNActive ) 
  {

    Serial.println("Backwards Button Down");
    minute--; 
    minute--;
    second=0; // decrement the minute counter
    if (minute<0) { 
      minute=58; 
      if (--hour <0) hour=23;
    } 
    incrementtime();
    second++;  // Increment the second counter to ensure that the name
    // flash doesnt happen when setting time  


    if (DS1302Present==1) {
      // Make a new time object to set the date and time 
      Time t(2010, 04, 28, hour, minute, second, 01);
      // Set the time and date on the chip 
      rtc.time(t);
    }

    displaytime();
    delay(100);
  }

}		  





