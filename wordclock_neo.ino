
/**************************************************************************
 *                                                                         *
 *  The British W O R D C L O C K   -                                      *
 *  A clock that tells the time using words.                               *
 *                                                                         *
 * Hardware: Custom Arduino code inspired by dougswordclocks               *
 *            English Desgn and English Hardware                           *
 *                                                                         *
 *   Copyright (C) 2014  Battle VA                                         *
 *                                                                         *
 *                                                                         
 *   done - modify  to use fastled and remove shifter                                                                      
 *   done - modify for use on esp8266
 *   done -include wifimanager with parameters
 *   use nist for clock
 *   set up for mqtt to change pallets seasonally
 *   bme680
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
#include <TimeLib.h> //https://github.com/PaulStoffregen/Time
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <BME680_Library.h> //https://github.com/vicatcu/BME680_Breakout
#include <Wire.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#include <FS.h>
//#include <EEPROM.h>
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <PubSubClient.h> //on Lostelemnts Git
#include <WiFiUdp.h>

// Define all the constants
//define set name of your bot
String botname = "neoclock";
String thistemp = "";
char sign_name[10]; //name no spaces or special charcters
char mqtt_port[6] = "8080";
char mqtt_server[40];
//flag for saving data
bool shouldSaveConfig = false;
std::unique_ptr<ESP8266WebServer> server;


#define DATA_PIN      D5     // Leds pin
#define mysda   D1 // SDA
#define myscl   D2 //SCL
#define palletchange D6 // pallet change button
#define glitterbutton D7 // to turn glitter on and off
#define LED_TYPE      WS2812B
#define COLOR_ORDER   GRB
// Set your number of leds here!
#define NUM_LEDS      56


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
int brightnessIndex = 4;
uint8_t brightness = brightnessMap[brightnessIndex];
#define ARRAY_SIZE(A) (sizeof(A) / sizeof((A)[0]))

// Current palette number from the 'playlist' of color palettes
uint8_t gCurrentPaletteNumber = 7;

CRGBPalette16 gCurrentPalette( CRGB::Cornsilk);
TBlendType    currentBlending;

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

BME680_Library bme680(0x76);
IPAddress timeServerIP;
WiFiClient wclient;
PubSubClient client(wclient);
IPAddress piserver(192, 168, 1, 76); // had to temporarily hard code as mdns not working

const int NTP_PACKET_SIZE = 48;
byte packetBuffer[NTP_PACKET_SIZE];
WiFiUDP udp;

bool isBST(int year, int month, int day, int hour)
{
    // bst begins at 01:00 gmt on the last sunday of march
    // and ends at 01:00 gmt (02:00 bst) on the last sunday of october

    // january, february, and november are out
    if (month < 3 || month > 10) { return false; }

    // april to september are in
    if (month > 3 && month < 10) { return true; }

    // last sunday of march
    int lastMarSunday =  (31 - (5* year /4 + 4) % 7);

    // last sunday of october
    int lastOctSunday = (31 - (5 * year /4 + 1) % 7);

    // in march we are bst if its past 1am gmt on the last sunday in the month
    if (month == 3)
    {
        if (day > lastMarSunday)
        {
            return true;
        }

        if (day < lastMarSunday)
        {
            return false;
        }

        if (hour < 1)
        {
            return false;
        }

        return true;
    }

    // in october we must be before 1am gmt (2am bst) on the last sunday to be bst
    if (month == 10)
    {
        if (day < lastOctSunday)
        {
            return true;
        }

        if (day > lastOctSunday)
        {
            return false;
        }

        if (hour >= 1)
        {
            return false;
        }

        return true;
    }
}

// send an ntp request to the time server at the given address
unsigned long sendNTPpacket(IPAddress& address)
{
    // set all bytes in the buffer to 0
    memset(packetBuffer, 0, NTP_PACKET_SIZE);

    packetBuffer[0] = 0b11100011;   // li, version, mode
    packetBuffer[1] = 0;            // stratum, or type of clock
    packetBuffer[2] = 6;            // polling interval
    packetBuffer[3] = 0xEC;         // peer clock precision

    // 8 bytes of zero for root delay & root dispersion
    packetBuffer[12] = 49;
    packetBuffer[13] = 0x4E;
    packetBuffer[14] = 49;
    packetBuffer[15] = 52;

    // all ntp fields have been given values, send request
    udp.beginPacket(address, 123);
    udp.write(packetBuffer, NTP_PACKET_SIZE);
    udp.endPacket();
}

void updateDate(void)
{
 //get the date stuff first   
    int cb = 0;
    int ntptries = 0;

    while (!cb or (ntptries <=5))
    {
        // send an ntp packet to a time server
        sendNTPpacket(timeServerIP);

        // wait to see if a reply is available
        delay(3000);
        cb = udp.parsePacket();

        // only try 5 times before exiting
        ntptries++;
        
    }
    if (cb) {
      // we've received a packet, read the data into the buffer
    udp.read(packetBuffer, NTP_PACKET_SIZE);

    // the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. first, extract the two words:
    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);

    // combine the four bytes (two words) into a long integer
    // this is ntp time (seconds since jan 1 1900):
    unsigned long secsSince1900 = highWord << 16 | lowWord;

    // unix time starts on jan 1 1970. in seconds, that's 2208988800:
    const unsigned long seventyYears = 2208988800UL;

    // subtract seventy years:
    unsigned long epoch = secsSince1900 - seventyYears;
    // handle british summer time
    time_t nowntp = epoch;
    setTime(epoch);
    int myhour;
    if (isBST(year(nowntp), month(nowntp), day(nowntp), hour(nowntp)))
    {
       // myhour = ((unixtime % 86400L) / 3600) + 1; // bst
        adjustTime(3600);
    }
    

    // Alter below to upte the time in the system
  
  
    }
   // digital clock display of the time

  Serial.print(hour());
  Serial.print(minute());
  Serial.print(second());
  Serial.print(" ");
  Serial.print(day());
  Serial.print(".");
  Serial.print(month());
  Serial.print(".");
  Serial.print(year());
  Serial.println();  
    
}
char temperatureString[6];
char humidityString[6];
char pressureString[8];
char gasString[8];


//const unsigned long fiveMinutes = 5 * 60 * 1000UL;
//static unsigned long lastSampleTime = 0 - fiveMinutes; // initialize such that a reading is due the first time through loop()
long int lastSampleTime = now();
long int lastDisplayTime = now();
int buttonState = 0;

// Display  assignments

int THETIMEIS[4] = {0,1,2,3};
int QUARTER[2] ={4,5};
int MTEN[2] ={6,7};
int TO[1] ={8};
int MINUTES[3] ={9,10,11}; 
int MFIVE[1] ={12};
int TWENTY[3] ={13,14,15};
int HALF[2] ={16,17};
int PAST[2] ={18,19};
int SIX[2] ={20,21};
int TWO[2] ={22,23};
int ONE[2] ={24,25};
int TEN[2] ={26,27};
int FIVE[2] ={28,29};
int THREE[2] ={30,31};
int EIGHT[2] ={32,33};
int FOUR[2] ={34,35};
int SEVEN[2] ={36,37};
int NINE[2] ={38,39};
int OCLOCK[3] ={40,41,42};
int TWELVE[2] ={43,44};
int ELEVEN[3] ={45,46,47};
int INTHE[2] = {48,49};
int MORNING[3] ={50,51,52};
int AFTERNOON[3] ={53,54,55};



//int  hour=3, minute=38, second=00; //carefull of this
static unsigned long msTick =0;  // the number of Millisecond Ticks since we last 
// incremented the second counter
int  count;
int  selftestmode;          // 1 = in self test - flash display
//int  DS1302Present=1;       // flag to indicate that the 1302 is there..    1 = present
//char Display1=0, Display2=0, Display3=0, Led1=0, Led2=0, Led3=0, Led4=0;
//int  OldHardware = 0;  // 1 = we are running on old hardwrae
//int  BTNActive = 1;    // the sense of the button inputs (Changes based on hardware type)


/* Create buffers */
char buf[50]; // time output string for debugging

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}



void SWversion(void) {
  delay(2000);
  Serial.println("British Neo Word Clock copyright 2017 BattleVA");
  
}

void setup()
{
// initialise the hardware	
 
  Serial.begin(115200);   // setup the serial port to 9600 baud
  delay(100);
  Serial.setDebugOutput(true);
  SWversion ();

//SETUP THE FAST LEDS  
  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);         // for WS2812 (Neopixel)
  FastLED.setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(brightness);
  //FastLED.setBrightness(100);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, MILLI_AMPS);
  fill_solid(leds, NUM_LEDS, solidColor);
  FastLED.show();
  //FastLED.setBrightness(100);

//Display Processor Details
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

//SETUP SPIFFS
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
  
// WIFI CUSTOM PARAMETERS id/name placeholder/prompt default length
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

//RESET SERVER WITH NEW SETTINGS (blelow may not be meeded
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
    
//WiFiClient wclient;
 //   client.set_server(mqtt_server,1883);
    client.set_server(piserver,1883);

    Serial.println("the server is");
    Serial.println(mqtt_server);
//PubSubClient client(wclient, mqtt_server);

    udp.begin(2390);
// get a random server from the pool
    WiFi.hostByName("europe.pool.ntp.org", timeServerIP);


//define set name of your sign
//String signname = "Room1"; //should be loaded from spiffs
//define mqtt message names
 thistemp = "ledclock\\" + String(sign_name);//signname;
Serial.print ("the subscribe is ");
Serial.println (thistemp);

 Wire.begin(mysda,myscl);

  Serial.print("BME Initialization...");
  if(bme680.begin()){
     Serial.print("Succeeded!");
  }
  else {
    Serial.print("Failed!");
    for(;;); // spin forever
  }
  Serial.println();

  Serial.print("Configuring Forced Mode...");
  if(bme680.configureForcedMode()){
     Serial.print("Succeeded!");
  }
  else {
    Serial.print("Failed!");
    for(;;); // spin forever
  }
  Serial.println();

  Serial.println(F("Temperature(degC),Relative_Humidity(%),Pressure(hPa),Gas_Resistance(Ohms)"));
 msTick=millis();      // Initialise the msTick counter

  selftest();  // validate the hardware for the user

  selftestmode=0;
  pinMode(palletchange,INPUT);
  pinMode(palletchange,INPUT_PULLUP);

  updateDate(); //get the nist time
  displaytime();        // display the current time






}


// Only needed if listening mqtt
void callback(const MQTT::Publish& pub) {
 /* // handle message arrived use strcmp on multiple subscriptions
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
 }*/
}

void reconnect() {

  // Loop until we're reconnected  - change to same as basic, try one
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(MQTT::Connect(botname)
      .set_auth("user", "secret")))  {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.println(" try on next loop");     
    }
}
void sendtemp(){
  // Change this to send the temp etc from the 680 and also display it on the clock every so often coloured to temperatture
  // unsigned long now = millis();
  // function to send the temperature every five minutes rather than leavingb in the loop
  // if (now - lastSampleTime >= fiveMinutes)
    if (now() - lastSampleTime >= 300) //five minute
  {
    bme680.configureForcedMode(); // otherwise you get BME680_W_NO_NEW_DATA warning code?
  if(bme680.read()){
    Serial.print(bme680.getTemperature(), 2);
    Serial.print(F(","));
    Serial.print(bme680.getRelativeHumidity(), 2);
    Serial.print(F(","));
    Serial.print(bme680.getBarometricPressure(), 2);
    Serial.print(F(","));
    Serial.print(bme680.getGasResistance());
    Serial.println();
  }
  else{
    Serial.println("BME680 Read Failed!");
  }
     float temperature = bme680.getTemperature();
     float humidity = bme680.getRelativeHumidity();
     float pressure = bme680.getBarometricPressure();
     float gas = bme680.getGasResistance();
  // convert temperature to a string with two digits before the comma and 2 digits for precision
  dtostrf(temperature, 2, 2, temperatureString);
  dtostrf(humidity, 2, 2, humidityString);
  dtostrf(pressure, 3, 2, pressureString);
  dtostrf(gas, 6, 0, gasString);
  // send temperature to the serial console
  //Serial.println ("Sending temperature: ");
  //Serial.println (temperatureString);
  //Serial.println (thistemp);
  // send temperature to the MQTT topic every 5 minutes
     client.publish(thistemp +"\\temperature", temperatureString);
     client.publish(thistemp +"\\humidity", humidityString);
     client.publish(thistemp +"\\pressure", pressureString);
     client.publish(thistemp +"\\gas", gasString);
  //lastSampleTime = now + fiveMinutes;
  //lastSampleTime += fiveMinutes;
  lastSampleTime = now();
   // add code to take scroll temperatre 4 times then reset face to static  (temperature is 5 characters * 8  for each scroll
  
    delay(100);
   
  }
}
void settimeleds( int b[], int sizeOfArray ){
//void settimeleds( int *ptr, int sizeOfArray ){
   // put a loop in here to set the leds based on the strings, pass string and length
   // add in allways on the time is and in the so we dont need to keep calling unless showing temp

   currentPaletteIndex = random(0,255);
   int ld;
   for (int k = 0 ; k <(sizeOfArray/4) ; ++k ){
   ld = b[k];
   //Serial.print (ld);
   leds[ld] = ColorFromPalette( gCurrentPalette, currentPaletteIndex, brightness, currentBlending);// CRGB::Cornsilk;
   currentPaletteIndex = random(0,255);
   }
  //FastLED.clear();
  // Now, turn on the "It is" leds and timeis
  leds[0] = ColorFromPalette( gCurrentPalette, currentPaletteIndex, brightness, currentBlending);// CRGB::Cornsilk;
   currentPaletteIndex = random(0,255);
  leds[1] = ColorFromPalette( gCurrentPalette, currentPaletteIndex, brightness, currentBlending);// CRGB::Cornsilk;
   currentPaletteIndex = random(0,255);
  leds[2] = ColorFromPalette( gCurrentPalette, currentPaletteIndex, brightness, currentBlending);// CRGB::Cornsilk;
   currentPaletteIndex = random(0,255);
  leds[3] = ColorFromPalette( gCurrentPalette, currentPaletteIndex, brightness, currentBlending);// CRGB::Cornsilk;
   currentPaletteIndex = random(0,255);
  leds[48] = ColorFromPalette( gCurrentPalette, currentPaletteIndex, brightness, currentBlending);// CRGB::Cornsilk;
   currentPaletteIndex = random(0,255);
  leds[49] = ColorFromPalette( gCurrentPalette, currentPaletteIndex, brightness, currentBlending);// CRGB::Cornsilk;

}
void settempleds( int b[], int sizeOfArray ){
//void settempleds( int *ptr, int sizeOfArray ){
   // put a loop in here to set the leds based on the strings, pass string and length
   // add in allways on the time is and in the so we dont need to keep calling unless showing temp
  currentPaletteIndex = random(0,255);
   int ld;
   //Serial.print ("array:");
   //Serial.print (sizeOfArray);
   //Serial.println ();
   for (int k = 0 ; k <(sizeOfArray/4) ; ++k ){
   ld = b[k];
   //Serial.print (ld);
   //Serial.print ("-");
   // change this to scroll through pallets selected by button
   //leds[ld] = CRGB::Cornsilk;
   leds[ld] = ColorFromPalette( gCurrentPalette, currentPaletteIndex, brightness, currentBlending);
    currentPaletteIndex = random(0,255);
   }
   //Serial.println ();
  //FastLED.clear();
 //set colour based on temp

}

///////////////////////////////////////////////////////////////////////////////////////////////

 


// change tyo use fastled and in the
void selftest(void){
  //test each led in turn REd then black
  for ( byte i = 0; i < NUM_LEDS; i++ ) {

    leds[i] = CRGB::Red;

    FastLED.delay(100);

    leds[i] = CRGB::Black;

  }
  delay(500);
  // start by clearing the display to a known state
  FastLED.clear();
  // now light each led set in turn
  settempleds (THETIMEIS, sizeof(THETIMEIS));
  FastLED.show();
  delay(500); 
   FastLED.clear();
  settempleds(MTEN, sizeof(MTEN));
  FastLED.show();   
  delay(500); 
   FastLED.clear();
  settempleds(HALF, sizeof(HALF));
  FastLED.show();    
  delay(500); 
   FastLED.clear();
  settempleds(TWENTY, sizeof(TWENTY));
  FastLED.show();  
  delay(500); 
   FastLED.clear();
  settempleds(QUARTER, sizeof(QUARTER));
  FastLED.show(); 
  delay(500); 
  FastLED.clear();
  settempleds(MFIVE, sizeof(MFIVE));
  FastLED.show();   
  delay(500); 
   FastLED.clear();
  settempleds(MINUTES, sizeof(MINUTES));
  FastLED.show(); 
  delay(500); 
   FastLED.clear();
  settempleds(PAST, sizeof(PAST));
  FastLED.show();    
  delay(500); 
  FastLED.clear();
  settempleds(TO, sizeof(TO));
  FastLED.show();      
  delay(500); 
   FastLED.clear();
  settempleds(ONE, sizeof(ONE));
  FastLED.show();     
  delay(500); 
  FastLED.clear();
  settempleds(TWO, sizeof(TWO));
  FastLED.show();     
  delay(500); 
  FastLED.clear(); 
  settempleds(THREE, sizeof(THREE));
  FastLED.show();   
  delay(500); 
  FastLED.clear();
  settempleds(FOUR, sizeof(FOUR));
  FastLED.show();    
  delay(500); 
   FastLED.clear();
  settempleds(FIVE, sizeof(FIVE));
  FastLED.show();   
  delay(500); 
   FastLED.clear();
  settempleds(SIX, sizeof(SIX));
  FastLED.show();     
  delay(500); 
   FastLED.clear();
  settempleds(SEVEN, sizeof(SEVEN));
  FastLED.show();   
  delay(500); 
   FastLED.clear();
  settempleds(EIGHT, sizeof(EIGHT));
  FastLED.show();   
  delay(500); 
   FastLED.clear();
  settimeleds(NINE, sizeof(NINE));
  FastLED.show();    
  delay(500); 
   FastLED.clear();
  settempleds(TEN, sizeof(TEN));
  FastLED.show();    
  delay(500); 
  FastLED.clear();
  settempleds(ELEVEN, sizeof(ELEVEN));
  FastLED.show();  
  delay(500); 
   FastLED.clear();
  settempleds(TWELVE, sizeof(TWELVE));
  FastLED.show();  
  delay(500); 
  FastLED.clear();
  settempleds(OCLOCK, sizeof(OCLOCK));
  FastLED.show();  
  delay(500); 
 FastLED.clear();
  settempleds(INTHE, sizeof(INTHE));
  FastLED.show();    
  delay(500); 
   FastLED.clear();
  settempleds(MORNING, sizeof(MORNING));
  FastLED.show();    
  delay(500); 
   FastLED.clear();
  settempleds(AFTERNOON, sizeof(AFTERNOON));
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
  if ((minute()>4) && (minute()<10)) { 
    settimeleds(MFIVE, sizeof(MFIVE));
    settimeleds(MINUTES, sizeof(MINUTES)); 
    //Serial.print("Five Minutes ");
  } 
  if ((minute()>9) && (minute()<15)) { 
    settimeleds(MTEN, sizeof(MTEN)); 
    settimeleds(MINUTES, sizeof(MINUTES)); 
    //Serial.print("Ten Minutes ");
  }
  if ((minute()>14) && (minute()<20)) {
    settimeleds(QUARTER, sizeof(QUARTER)); 
    //Serial.print("Quarter ");
  }
  if ((minute()>19) && (minute()<25)) { 
    settimeleds(TWENTY, sizeof(TWENTY)); 
    settimeleds(MINUTES, sizeof(MINUTES)); 
   // Serial.print("Twenty Minutes ");
  }
  if ((minute()>24) && (minute()<30)) { 
    settimeleds(TWENTY, sizeof(TWENTY)); 
    settimeleds(MFIVE, sizeof(MFIVE)); 
    settimeleds(MINUTES, sizeof(MINUTES));
    //Serial.print("Twenty Five Minutes ");
  }  
  if ((minute()>29) && (minute()<35)) {
    settimeleds(HALF, sizeof(HALF));
    //Serial.print("Half ");
  }
  if ((minute()>34) && (minute()<40)) { 
    settimeleds(TWENTY, sizeof(TWENTY)); 
    settimeleds(MFIVE, sizeof(MFIVE)); 
    settimeleds(MINUTES, sizeof(MINUTES));
    //Serial.print("Twenty Five Minutes ");
  }  
  if ((minute()>39) && (minute()<45)) { 
    settimeleds(TWENTY, sizeof(TWENTY)); 
    settimeleds(MINUTES, sizeof(MINUTES)); 
    //Serial.print("Twenty Minutes ");
  }
  if ((minute()>44) && (minute()<50)) {
    settimeleds(QUARTER, sizeof(QUARTER)); 
    //Serial.print("Quarter ");
  }
  if ((minute()>49) && (minute()<55)) { 
    settimeleds(MTEN, sizeof(MTEN)); 
    settimeleds(MINUTES, sizeof(MINUTES)); 
    //Serial.print("Ten Minutes ");
  } 
  if (minute()>54) { 
    settimeleds(MFIVE, sizeof(MFIVE)); 
    settimeleds(MINUTES, sizeof(MINUTES)); 
    //Serial.print("Five Minutes ");
  }



  if ((minute() <5))
  {
    switch (hour()) {
    case 1:
    case 13: 
      settimeleds(ONE, sizeof(ONE)); 
      //Serial.print("One ");
      break;
    case 2:
    case 14: 
      settimeleds(TWO, sizeof(TWO)); 
      //Serial.print("Two ");
      break;
    case 3: 
    case 15:
      settimeleds(THREE, sizeof(THREE)); 
     //Serial.print("Three ");
      break;
    case 4: 
    case 16:
      settimeleds(FOUR, sizeof(FOUR)); 
      //Serial.print("Four ");
      break;
    case 5: 
    case 17:
      settimeleds(FIVE, sizeof(FIVE)); 
      //Serial.print("Five ");
      break;
    case 6: 
    case 18:
      settimeleds(SIX, sizeof(SIX)); 
      //Serial.print("Six ");
      break;
    case 7: 
    case 19:
      settimeleds(SEVEN, sizeof(SEVEN)); 
      //Serial.print("Seven ");
      break;
    case 8: 
    case 20:
      settimeleds(EIGHT, sizeof(EIGHT)); 
      //Serial.print("Eight ");
      break;
    case 9: 
    case 21:
      settimeleds(NINE, sizeof(NINE)); 
      //Serial.print("Nine ");
      break;
    case 10:
    case 22: 
      settimeleds(TEN, sizeof(TEN)); 
      //Serial.print("Ten ");
      break;
    case 11:
    case 23: 
      settimeleds(ELEVEN, sizeof(ELEVEN)); 
      //Serial.print("Eleven ");
      break;
    case 0:
    case 12: 
      settimeleds(TWELVE, sizeof(TWELVE)); 
      //Serial.print("Twelve ");
      break;
    }
    settimeleds(OCLOCK, sizeof(OCLOCK));
    //Serial.println("O'Clock");
  }
  else
    if ((minute() < 35) && (minute() >4))
    {
      settimeleds(PAST, sizeof(PAST));
      //Serial.print("Past ");
      switch (hour()) {
      case 1:
      case 13: 
        settimeleds(ONE, sizeof(ONE)); 
        //Serial.println("One ");
        break;
      case 2: 
      case 14:
        settimeleds(TWO, sizeof(TWO)); 
       // Serial.println("Two ");
        break;
      case 3: 
      case 15:
        settimeleds(THREE, sizeof(THREE)); 
        //Serial.println("Three ");
        break;
      case 4: 
      case 16:
        settimeleds(FOUR, sizeof(FOUR)); 
        //Serial.println("Four ");
        break;
      case 5: 
      case 17:
        settimeleds(FIVE, sizeof(FIVE)); 
        //Serial.println("Five ");
        break;
      case 6: 
      case 18:
        settimeleds(SIX, sizeof(SIX)); 
        //Serial.println("Six ");
        break;
      case 7: 
      case 19:
        settimeleds(SEVEN, sizeof(SEVEN)); 
       // Serial.println("Seven ");
        break;
      case 8: 
      case 20:
        settimeleds(EIGHT, sizeof(EIGHT)); 
        //Serial.println("Eight ");
        break;
      case 9: 
      case 21:
        settimeleds(NINE, sizeof(NINE)); 
       // Serial.println("Nine ");
        break;
      case 10:
      case 22: 
        settimeleds(TEN, sizeof(TEN)); 
        //Serial.println("Ten ");
        break;
      case 11:
      case 23: 
        settimeleds(ELEVEN, sizeof(ELEVEN)); 
        //Serial.println("Eleven ");
        break;
      case 0:
      case 12: 
        settimeleds(TWELVE, sizeof(TWELVE)); 
        //Serial.println("Twelve ");
        break;
      }
    }
    else
    {
      // if we are greater than 34 minutes past the hour then display
      // the next hour, as we will be displaying a 'to' sign
      settimeleds(TO, sizeof(TO));
      //Serial.print("To ");
      switch (hour()) {
      case 1: 
      case 13:
        settimeleds(TWO, sizeof(TWO)); 
        //Serial.println("Two ");
        break;
      case 14:
      case 2: 
        settimeleds(THREE, sizeof(THREE)); 
        //Serial.println("Three ");
        break;
      case 15:
      case 3: 
        settimeleds(FOUR, sizeof(FOUR)); 
        //Serial.println("Four ");
        break;
      case 4: 
      case 16:
        settimeleds(FIVE, sizeof(FIVE)); 
        //Serial.println("Five ");
        break;
      case 5: 
      case 17:
        settimeleds(SIX, sizeof(SIX)); 
        //Serial.println("Six ");
        break;
      case 6: 
      case 18:
        settimeleds(SEVEN, sizeof(SEVEN)); 
        //Serial.println("Seven ");
        break;
      case 7: 
      case 19:
        settimeleds(EIGHT, sizeof(EIGHT)); 
        //Serial.println("Eight ");
        break;
      case 8: 
      case 20:
        settimeleds(NINE, sizeof(NINE)); 
        //Serial.println("Nine ");
        break;
      case 9: 
      case 21:
        settimeleds(TEN, sizeof(TEN)); 
        //Serial.println("Ten ");
        break;
      case 10: 
      case 22:
        settimeleds(ELEVEN, sizeof(ELEVEN)); 
        //Serial.println("Eleven ");
        break;
      case 11: 
      case 23:
        settimeleds(TWELVE, sizeof(TWELVE)); 
        //Serial.println("Twelve ");
        break;
      case 0:
      case 12: 
        settimeleds(ONE, sizeof(ONE)); 
        //Serial.println("One ");
        break;
      }
    }

// Now set the AM or PM
 if (hour() >= 12 && hour() <= 23){
     settimeleds(AFTERNOON, sizeof(AFTERNOON));
     //Serial.println("afternoon");
     //Serial.println(hour());
 } 
 else{
   settimeleds(MORNING, sizeof(MORNING));
   //Serial.println("morning");
 }

// Now show the time
FastLED.show();

  


}








void loop(void)
{
///////////////////////////////////////////////////////////////////////////////////////////////////
// put your main code here, to run repeatedly:
   //client.set_callback(callback);
   if (!client.connected()) {
    reconnect();
  }
  
  client.loop();
  sendtemp();
  ////////////////////////////////////////////////////////////////////////////////////////////////////////
  //Serial.println("Loop Started");
  
  
  //Serial.print(second());
  // Serial.print("..");
  
// Change Pallet Colour if needed/ wanted
  if (digitalRead(palletchange) == LOW) {
    gCurrentPaletteNumber += 1;
    if (gCurrentPaletteNumber == 8){gCurrentPaletteNumber = 0;}
    //set the pallete
    Serial.print("changing pallete to:");     
    Serial.print(gCurrentPaletteNumber);
    if( gCurrentPaletteNumber == 0)  { gCurrentPalette = RainbowColors_p;         currentBlending = LINEARBLEND; } 
    if( gCurrentPaletteNumber == 1)  { gCurrentPalette = RainbowStripeColors_p;   currentBlending = LINEARBLEND; }    
    if( gCurrentPaletteNumber == 2)  { gCurrentPalette = CloudColors_p;           currentBlending = LINEARBLEND; }    
    if( gCurrentPaletteNumber == 3)  { gCurrentPalette = PartyColors_p;           currentBlending = LINEARBLEND; }  
    if( gCurrentPaletteNumber == 4)  { gCurrentPalette = ForestColors_p;           currentBlending = LINEARBLEND; } 
    if( gCurrentPaletteNumber == 5)  { gCurrentPalette = LavaColors_p;           currentBlending = LINEARBLEND; } 
    if( gCurrentPaletteNumber == 6)  { gCurrentPalette = OceanColors_p;           currentBlending = LINEARBLEND; }  
    if( gCurrentPaletteNumber == 7)  { gCurrentPalette = CRGBPalette16(CRGB::Cornsilk);           currentBlending = NOBLEND; } 
    displaytime();
    delay(2000); //debounce
    }
// Add Glitter

// Show Time and reset as needed
 if (now() - lastDisplayTime >= 30){ //update time every 30 seconds 
    displaytime();
    lastDisplayTime = now();
 }
   if ((hour()==23) && (minute()==1)) {
    updateDate(); // actual need to call this only once per day not every minute 23:01
   }

}		  





