





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
 *              shifter.setPin(1, HIGH) or LOW*
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
#include <DallasTemperature.h> //on LostElements Git
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#include <FS.h>
#include <EEPROM.h>
#include "GradientPalettes.h"
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <PubSubClient.h> //on Lostelemnts Git

// Define all the constants

// Shifter pin Assignments
const int SER_Pin = 4;  //SER_IN
const int RCLK_Pin = 3; //L_CLOCK
const int SRCLK_Pin = 2; //Clock
const int NUM_REGISTERS = 3; //number of shift registers

// Display pin assignments
const int MINUTES = 1;
const int MTEN = 2; 
const int HALF = 3;
const int PAST = 4; 
const int THREE = 5;
const int THETIMEIS = 6;
const int TWENTY = 7;
const int TO = 8;
const int TWO = 9;
const int SIX = 19;
const int TWELVE = 11;
const int FIVE = 12;
const int SEVEN = 13;
const int OCLOCK = 14;
const int ONE = 15;
const int QUARTER = 16;
const int EIGHT = 17;
const int MFIVE = 18;
const int MORNING = 0;
const int ELEVEN = 20;
const int TEN = 21;
const int NINE = 22;
const int FOUR = 23;
const int AFTERNOON = 10;



int  hour=3, minute=38, second=00;
static unsigned long msTick =0;  // the number of Millisecond Ticks since we last 
// incremented the second counter
int  count;
int  selftestmode;          // 1 = in self test - flash display
int  DS1302Present=1;       // flag to indicate that the 1302 is there..    1 = present
char Display1=0, Display2=0, Display3=0, Led1=0, Led2=0, Led3=0, Led4=0;
int  OldHardware = 0;  // 1 = we are running on old hardwrae
int  BTNActive = 1;    // the sense of the button inputs (Changes based on hardware type)

                        


// buttons
int FWDButtonPin=5;
int REVButtonPin=7;

// 1302 RTC Constants
int DS1302IOPin=10;
int DS1302CEPin=8;
int DS1302CLKPin=9;




/* Create buffers */
char buf[50]; // time output string for debugging

//initaize shifter using the Shifter library
Shifter shifter(SER_Pin, RCLK_Pin, SRCLK_Pin, NUM_REGISTERS);

// create an object that talks to the RTC
DS1302 rtc(DS1302CEPin, DS1302IOPin, DS1302CLKPin);

void print_DS1302time()
{
  /* Get the current time and date from the chip */
  Time t = rtc.time();

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
  pinMode(DS1302IOPin, OUTPUT); 
  pinMode(DS1302CEPin, OUTPUT); 
  pinMode(DS1302CLKPin, OUTPUT); 

  

  Serial.begin(9600);   // setup the serial port to 9600 baud
  SWversion ();

  // test whether the DS1302 is there
  Serial.print("Verifying DS1302 ");
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

  digitalWrite(FWDButtonPin,HIGH);  // Turn on weak pullups
  digitalWrite(REVButtonPin,HIGH);  // Turn on weak pullups

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

  if (DS1302Present==1) {
    // Get the current time and date from the chip 
    Time t = rtc.time();
    second=t.sec;     
    minute=t.min;
    hour=t.hr; 
  }

  displaytime();        // display the current time






}







void selftest(void){
  // start by clearing the display to a known state
  shifter.clear();
  shifter.write(); 
  // now light each led set in turn
  shifter.setPin(THETIMEIS, HIGH);
  shifter.write();  
  delay(500); 
  shifter.setPin(THETIMEIS, LOW);
  shifter.setPin(MTEN,HIGH);
  shifter.write();   
  delay(500); 
  shifter.setPin(MTEN, LOW);
  shifter.setPin(HALF,HIGH);
  shifter.write();    
  delay(500); 
  shifter.setPin(HALF, LOW);
  shifter.setPin(TWENTY,HIGH);
  shifter.write();  
  delay(500); 
  shifter.setPin(TWENTY, LOW);
  shifter.setPin(QUARTER,HIGH);
  shifter.write(); 
  delay(500); 
  shifter.setPin(QUARTER, LOW);
  shifter.setPin(MFIVE,HIGH);
  shifter.write();   
  delay(500); 
  shifter.setPin(MFIVE, LOW);
  shifter.setPin(MINUTES,HIGH);
  shifter.write(); 
  delay(500); 
  shifter.setPin(MINUTES, LOW);
  shifter.setPin(PAST,HIGH);
  shifter.write();    
  delay(500); 
  shifter.setPin(PAST, LOW);
  shifter.setPin(TO,HIGH);
  shifter.write();      
  delay(500); 
  shifter.setPin(TO, LOW);
  shifter.setPin(ONE,HIGH);
  shifter.write();     
  delay(500); 
  shifter.setPin(ONE, LOW);
  shifter.setPin(TWO,HIGH);
  shifter.write();     
  delay(500); 
  shifter.setPin(TWO, LOW);
  shifter.setPin(THREE,HIGH);
  shifter.write();   
  delay(500); 
  shifter.setPin(THREE, LOW);
  shifter.setPin(FOUR,HIGH);
  shifter.write();    
  delay(500); 
  shifter.setPin(FOUR, LOW);
  shifter.setPin(FIVE,HIGH);
  shifter.write();   
  delay(500); 
  shifter.setPin(FIVE, LOW);
  shifter.setPin(SIX,HIGH);
  shifter.write();     
  delay(500); 
  shifter.setPin(SIX, LOW);
  shifter.setPin(SEVEN,HIGH);
  shifter.write();   
  delay(500); 
  shifter.setPin(SEVEN, LOW);
  shifter.setPin(EIGHT,HIGH);
  shifter.write();   
  delay(500); 
  shifter.setPin(EIGHT, LOW);
  shifter.setPin(NINE,HIGH);
  shifter.write();    
  delay(500); 
  shifter.setPin(NINE, LOW);
  shifter.setPin(TEN,HIGH);
  shifter.write();    
  delay(500); 
  shifter.setPin(TEN, LOW);
  shifter.setPin(ELEVEN,HIGH);
  shifter.write();  
  delay(500); 
  shifter.setPin(ELEVEN, LOW);
  shifter.setPin(TWELVE,HIGH);
  shifter.write();  
  delay(500); 
  shifter.setPin(TWELVE, LOW);
  shifter.setPin(OCLOCK,HIGH);
  shifter.write();  
  delay(500); 
  shifter.setPin(OCLOCK, LOW);
  shifter.setPin(MORNING,HIGH);
  shifter.write();    
  delay(500); 
  shifter.setPin(MORNING, LOW);
  shifter.setPin(AFTERNOON,HIGH);
  shifter.write(); 
  delay(500); 
  

  

}


void displaytime(void){

  // start by clearing the display to a known state
  shifter.clear();
  shifter.write();

  // Now, turn on the "It is" leds
  shifter.setPin(THETIMEIS, HIGH);

  // now we display the appropriate minute counter
  if ((minute>4) && (minute<10)) { 
    shifter.setPin(MFIVE, HIGH);
    shifter.setPin(MINUTES, HIGH); 
    Serial.print("Five Minutes ");
  } 
  if ((minute>9) && (minute<15)) { 
    shifter.setPin(MTEN, HIGH); 
    shifter.setPin(MINUTES, HIGH); 
    Serial.print("Ten Minutes ");
  }
  if ((minute>14) && (minute<20)) {
    shifter.setPin(QUARTER, HIGH); 
    Serial.print("Quarter ");
  }
  if ((minute>19) && (minute<25)) { 
    shifter.setPin(TWENTY, HIGH); 
    shifter.setPin(MINUTES, HIGH); 
    Serial.print("Twenty Minutes ");
  }
  if ((minute>24) && (minute<30)) { 
    shifter.setPin(TWENTY, HIGH); 
    shifter.setPin(MFIVE, HIGH); 
    shifter.setPin(MINUTES, HIGH);
    Serial.print("Twenty Five Minutes ");
  }  
  if ((minute>29) && (minute<35)) {
    shifter.setPin(HALF, HIGH);
    Serial.print("Half ");
  }
  if ((minute>34) && (minute<40)) { 
    shifter.setPin(TWENTY, HIGH); 
    shifter.setPin(MFIVE, HIGH); 
    shifter.setPin(MINUTES, HIGH);
    Serial.print("Twenty Five Minutes ");
  }  
  if ((minute>39) && (minute<45)) { 
    shifter.setPin(TWENTY, HIGH); 
    shifter.setPin(MINUTES, HIGH); 
    Serial.print("Twenty Minutes ");
  }
  if ((minute>44) && (minute<50)) {
    shifter.setPin(QUARTER, HIGH); 
    Serial.print("Quarter ");
  }
  if ((minute>49) && (minute<55)) { 
    shifter.setPin(MTEN, HIGH); 
    shifter.setPin(MINUTES, HIGH); 
    Serial.print("Ten Minutes ");
  } 
  if (minute>54) { 
    shifter.setPin(MFIVE, HIGH); 
    shifter.setPin(MINUTES, HIGH); 
    Serial.print("Five Minutes ");
  }



  if ((minute <5))
  {
    switch (hour) {
    case 1:
    case 13: 
      shifter.setPin(ONE, HIGH); 
      Serial.print("One ");
      break;
    case 2:
    case 14: 
      shifter.setPin(TWO, HIGH); 
      Serial.print("Two ");
      break;
    case 3: 
    case 15:
      shifter.setPin(THREE, HIGH); 
      Serial.print("Three ");
      break;
    case 4: 
    case 16:
      shifter.setPin(FOUR, HIGH); 
      Serial.print("Four ");
      break;
    case 5: 
    case 17:
      shifter.setPin(FIVE, HIGH); 
      Serial.print("Five ");
      break;
    case 6: 
    case 18:
      shifter.setPin(SIX, HIGH); 
      Serial.print("Six ");
      break;
    case 7: 
    case 19:
      shifter.setPin(SEVEN, HIGH); 
      Serial.print("Seven ");
      break;
    case 8: 
    case 20:
      shifter.setPin(EIGHT, HIGH); 
      Serial.print("Eight ");
      break;
    case 9: 
    case 21:
      shifter.setPin(NINE, HIGH); 
      Serial.print("Nine ");
      break;
    case 10:
    case 22: 
      shifter.setPin(TEN, HIGH); 
      Serial.print("Ten ");
      break;
    case 11:
    case 23: 
      shifter.setPin(ELEVEN, HIGH); 
      Serial.print("Eleven ");
      break;
    case 0:
    case 12: 
      shifter.setPin(TWELVE, HIGH); 
      Serial.print("Twelve ");
      break;
    }
    shifter.setPin(OCLOCK, HIGH);
    Serial.println("O'Clock");
  }
  else
    if ((minute < 35) && (minute >4))
    {
      shifter.setPin(PAST, HIGH);
      Serial.print("Past ");
      switch (hour) {
      case 1:
      case 13: 
        shifter.setPin(ONE, HIGH); 
        Serial.println("One ");
        break;
      case 2: 
      case 14:
        shifter.setPin(TWO, HIGH); 
        Serial.println("Two ");
        break;
      case 3: 
      case 15:
        shifter.setPin(THREE, HIGH); 
        Serial.println("Three ");
        break;
      case 4: 
      case 16:
        shifter.setPin(FOUR, HIGH); 
        Serial.println("Four ");
        break;
      case 5: 
      case 17:
        shifter.setPin(FIVE, HIGH); 
        Serial.println("Five ");
        break;
      case 6: 
      case 18:
        shifter.setPin(SIX, HIGH); 
        Serial.println("Six ");
        break;
      case 7: 
      case 19:
        shifter.setPin(SEVEN, HIGH); 
        Serial.println("Seven ");
        break;
      case 8: 
      case 20:
        shifter.setPin(EIGHT, HIGH); 
        Serial.println("Eight ");
        break;
      case 9: 
      case 21:
        shifter.setPin(NINE, HIGH); 
        Serial.println("Nine ");
        break;
      case 10:
      case 22: 
        shifter.setPin(TEN, HIGH); 
        Serial.println("Ten ");
        break;
      case 11:
      case 23: 
        shifter.setPin(ELEVEN, HIGH); 
        Serial.println("Eleven ");
        break;
      case 0:
      case 12: 
        shifter.setPin(TWELVE, HIGH); 
        Serial.println("Twelve ");
        break;
      }
    }
    else
    {
      // if we are greater than 34 minutes past the hour then display
      // the next hour, as we will be displaying a 'to' sign
      shifter.setPin(TO, HIGH);
      Serial.print("To ");
      switch (hour) {
      case 1: 
      case 13:
        shifter.setPin(TWO, HIGH); 
        Serial.println("Two ");
        break;
      case 14:
      case 2: 
        shifter.setPin(THREE, HIGH); 
        Serial.println("Three ");
        break;
      case 15:
      case 3: 
        shifter.setPin(FOUR, HIGH); 
        Serial.println("Four ");
        break;
      case 4: 
      case 16:
        shifter.setPin(FIVE, HIGH); 
        Serial.println("Five ");
        break;
      case 5: 
      case 17:
        shifter.setPin(SIX, HIGH); 
        Serial.println("Six ");
        break;
      case 6: 
      case 18:
        shifter.setPin(SEVEN, HIGH); 
        Serial.println("Seven ");
        break;
      case 7: 
      case 19:
        shifter.setPin(EIGHT, HIGH); 
        Serial.println("Eight ");
        break;
      case 8: 
      case 20:
        shifter.setPin(NINE, HIGH); 
        Serial.println("Nine ");
        break;
      case 9: 
      case 21:
        shifter.setPin(TEN, HIGH); 
        Serial.println("Ten ");
        break;
      case 10: 
      case 22:
        shifter.setPin(ELEVEN, HIGH); 
        Serial.println("Eleven ");
        break;
      case 11: 
      case 23:
        shifter.setPin(TWELVE, HIGH); 
        Serial.println("Twelve ");
        break;
      case 0:
      case 12: 
        shifter.setPin(ONE, HIGH); 
        Serial.println("One ");
        break;
      }
    }

// Now set the AM or PM
 if (hour >= 12 && hour <= 23){
     shifter.setPin(AFTERNOON, HIGH);
     Serial.println("afternoon");
     Serial.println(hour);
 } 
 else{
   shifter.setPin(MORNING,HIGH);
   Serial.println("morning");
 }

// Now show the time
shifter.write();

  


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

  //Serial.println("Loop Started");
  
  // heart of the timer - keep looking at the millisecond timer on the Arduino
  // and increment the seconds counter every 1000 ms
  if ( millis() - msTick >999) {
    msTick=millis();
    second++;
    // Flash the onboard Pin13 Led so we know something is hapening!
    digitalWrite(13,HIGH);
    delay(50);
    digitalWrite(13,LOW);  
    delay(50);
    digitalWrite(13,HIGH);
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





