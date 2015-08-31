// ESP8266 Cat monitoring project, currently active, currently awaiting implementation of EEPROM storage of daily totals. 
// Once EEPROM is implemented, the project will be opened to the public in the form of an instructable!

#include "Wire.h"
#include <ESP8266WiFi.h>

#define DS3231_I2C_ADDRESS 0x68

// Enter ssid and password
const char* ssid     = "MySSID";
const char* password = "MYPASSWORD";

// Thingspeak APIs
String dataAPI    = "9VYDLLNFFJ3B7EKY";
String twitterAPI = "F2KJHYSNFYNFDP7E";

// Data and clock lines for water bowl ADC
int DT1  = 14;
int CLK1 = 2;

// Data and clock lines for food bowl ADC
int DT2  = 13;
int CLK2 = 12;

// Slope, y-incercept for food bowl callibration line
float M1 = 0.00027979048;
float Y1 = -2761.88;

// Slope, y-incercept for water bowl callibration line
float M2 = 0.00227597495;
float Y2 = -19542.65;

// Previous minute number from RTC
int previousMin = 0;

// Previous minute's readings for food and water mass
float previousFood  = 0;
float previousWater = 0;

// Running totals of food, water mass changes for the day
float dailyFood  = 0;
float dailyWater = 0;

// Frequencies of food, water mass changes for the day
int freqFood  = 0;
int freqWater = 0;

// Temporary food and water values too count up food and water in one visit before
// uploading to Thingspeak
float tempFood  = 0;
float tempWater = 0;

// Total time spent in litter box and frequency for the day
int dailyLitterBoxTime = 0;
int freqLitterBox      = 0;

// Start time for cat first sensed in litter box
int startSec = 0;
int startMin = 0;

// Last time for cat sensed in litter box
int lastSec = 0;
int lastMin = 0;

// Flag is true if cat as already been detected in litter box
boolean catAlreadyInBox = false;

// Flags to designate if tooLow tweet has been sent once for the day
boolean foodTooLowSent  = false;
boolean waterTooLowSent = false;

// Flag is set true at t >= 12:00:00 such that end of day functions are taken care of later at t >= 23:00:00
// where the flag is set true until t >= 00:00:00
boolean endOfDayCompleted = false;

// Number of different phrases for each type of empty or refill twitter status
int numEF = 11;
int numEW = 7;
int numRF = 4;
int numRW = 4;

// Open WiFi client
WiFiClient client;

//*********************************************************************************************************************

void setup() 
{
 
  // check to see if broadcasting ssid now
  WiFi.mode(WIFI_STA);
  
  // Set up I2C for RTC 
  Wire.pins(4, 5);
  Wire.begin();
  
  // Set the RTC time once at programming:
  // seconds, minutes, hours
  //setDS3231time(00,51,23);
  
  // Initialize data, clock lines for food and water ADC
  pinMode(DT1, INPUT);
  pinMode(CLK1, OUTPUT);
  pinMode(DT2, INPUT);
  pinMode(CLK2, OUTPUT);
  
  // Initialize previous readings for food and water mass
  previousFood = readFood();
  previousWater = readWater();
  
  // PIR output pin set as input
  pinMode(16, INPUT);
  
  // Pushbutton input to pause program when moving bowls or cleaning litterbox.
  // remember that this pin must be pulled low when programming
  pinMode(0, INPUT);
  
  // Seed random number generator by feeding it random input from ADC
  randomSeed(analogRead(A0));
}

//**********************************************************************************************
//**********************************************************************************************
void loop() {
  
  // Loop is to halt program when push button is pushed and pin 0 is pulled low
  // This is used to allow user time to move water and food bowl and litterbox
  // without triggering any readings.
  while(digitalRead(0) == LOW)
    delay(5000);
  
  // Read RTC values for time
  byte second, minute, hour;
  readDS3231time(&second, &minute, &hour);
  
   // If end of day (t >= 23:00:00), tweet end of day tweet and reset daily values
  if(endOfDayCompleted == false && hour >= 23)
    {
      // If daily totals are zero, meaning unit was just powered on, don't send end of day tweet
      // change flags for next day.
      if(dailyFood == 0 && dailyWater == 0 && dailyLitterBoxTime == 0)
        {
         endOfDayCompleted = true;
         foodTooLowSent    = false;
         waterTooLowSent   = false;  
        }
   else
     {
      endOfDayTweet(dailyFood, freqFood, dailyWater, freqWater, dailyLitterBoxTime, freqLitterBox);
      dailyFood  = 0.0;
      freqFood   = 0;
      dailyWater = 0.0;
      freqWater  = 0;
      dailyLitterBoxTime = 0;
      freqLitterBox     = 0;
      endOfDayCompleted = true;
      foodTooLowSent    = false;
      waterTooLowSent   = false;
     }
    }
    
  // Reset end of day flag after midnight
  if(hour >= 0 && hour <= 12 && endOfDayCompleted == true)
    endOfDayCompleted = false;
 
  // Reset flag values
  int litterVisit = 0;
  float foodVisit  = 0;
  float waterVisit = 0;
  
  // Check if cat is in litter box, each time loop is executed
  if(inBox())
   {
    if(catAlreadyInBox == false)
     {
      startSec = second;
      startMin = minute;
      lastSec  = second;
      lastMin  = minute;
      catAlreadyInBox = true;
     }
    else
     {
      lastSec = second;
      lastMin = minute;
     }
   }
  
  // Wait between 1-2 minutes after last sensing cat in box then calculate time in
  // and update total time and frequency
  if(catAlreadyInBox == true && minute-lastMin > 1)
   {
    // Reset flag
    catAlreadyInBox = false;
    
    int totalTimeInBox = 0;
    
    if(lastMin >= startMin)
      totalTimeInBox = (lastMin-startMin)*60 + (lastSec-startSec);
    else
      totalTimeInBox = ( (60-startMin) + lastMin)*60 + (lastSec-startSec);
    
    // reject any times less than 20 seconds
    if(totalTimeInBox <= 20)
     {
      startSec = 0;
      startMin = 0;
      lastSec  = 0;
      lastMin  = 0; 
      totalTimeInBox = 0;
     }
    else
     {
      // Set value to send to Thingspeak
      litterVisit = totalTimeInBox;
      
      // update daily total litter box time and frequency
      dailyLitterBoxTime += totalTimeInBox;   
      freqLitterBox++;
    
      startSec = 0;
      startMin = 0;
      lastSec  = 0;
      lastMin  = 0;
      totalTimeInBox = 0;
    }
  }
    
  // If time to check food and water levels (every minute)
  if(minute != previousMin)
   {
    // Change value for previous minute to current value
    previousMin = minute;
   
    // Check food level, round to one decimal place
    float currentFood = readFood();
    //currentFood = round(currentFood*10)/10.0 + 0.00001;
    
    // If currentFood is greater than previous value by threshold, make sure isn't error
    if(currentFood - previousFood > 0.5)
     {
       for(int i = 0; i < 10; i++)
       {
         if(!(readFood() - previousFood > 0.5))
           {
             delay(100);
             currentFood = readFood();
             break;
           }
           delay(100);
       }
     }
     
    // If reading is < 0 equate to 0
    if(currentFood < 0.0)
      currentFood = 0.0;
     
    // If currentFood is too low send tooLow() tweet
    if(currentFood < 6.0 && foodTooLowSent == false)  
       tooLow(1, hour, minute);
   
    // If current value has increased a lot, food was refilled, send refilled tweet
    if(currentFood - previousFood > 50.0 && foodTooLowSent == true)
       refilled(1, hour, minute);
     
   
    // If cat has eaten food, add amount eaten to temporary food amount
    if(didCatEat(currentFood))
      tempFood += previousFood - currentFood;
   
    // If cat hasn't eaten, but has before this time, equate amount to foodVisit to send to Thingspeak,
    // reset temp value and increment daily food amount and frequency counter.
    else if(tempFood > 0)
     {
      // If current reading is more than previous reading by a threshold amount, discard reading as error. 
      if(currentFood - previousFood > 1.0)
      {
       tempFood = 0;
      } 
      
      else 
      {
      foodVisit = tempFood;
      dailyFood += tempFood;
      tempFood = 0;
      freqFood++;
      }
     }
     
    previousFood = currentFood;
   
    // Check water level, round to one decimal place
    float currentWater = readWater();
    //currentWater = round(currentWater*10)/10.0 + 0.00001;
    
    // If currentWater is greater than previous value by threshold, make sure isn't error
    if(currentWater - previousWater > 0.5)
     {
       for(int i = 0; i < 10; i++)
       {
         if(!(readWater() - previousWater > 0.5))
           {
             delay(100);
             currentWater = readWater();
             break;
           }
           delay(100);
       }
     }
    
    // If reading is < 0 equate to 0
    if(currentWater < 0.0)
      currentWater = 0.0;
     
    // If currentWater is too low send tooLow() tweet
    if(currentWater < 80.0 && waterTooLowSent == false)
       tooLow(2, hour, minute);
   
     
    // If current value has increased a lot, water was refilled, send refilled tweet
    if(currentWater - previousWater > 100.0 && waterTooLowSent == true)
      refilled(2, hour, minute);
   
    // If cat has drank water add value to temporary water amount
    if(didCatDrink(currentWater))
      tempWater += previousWater - currentWater;
   
    // If cat hasn't drank, but has before this time, equate smount to waterVisit to send to Thingspeak,
    // reset temp value and increment daily food amount and frequency counter.
    else if(tempWater > 0)
     {
      // If current reading is more than previous reading by a threshold amount, discard reading as error. 
      if(currentWater - previousWater > 2.0)
      {
       tempWater = 0;
      } 
      
      else
      {
      waterVisit = tempWater;
      dailyWater += tempWater;
      tempWater = 0;
      freqWater++;
      }
     }

    previousWater = currentWater;

  }// If !minute
  
  // update Thingspeak data if foodVisit, waterVisit, or litterVisit are above 0;
  if(foodVisit > 0 || waterVisit > 0 || litterVisit > 0)
    updateThingspeak(foodVisit, waterVisit, litterVisit);

  delay(10);
}
//*********************************************************************************************************************
//*********************************************************************************************************************

// RTC: Convert normal decimal numbers to binary coded decimal
byte decToBcd(byte b)
{
  return( (b/10*16) + (b%10) );
}

//*********************************************************************************************************************

// RTC: Convert binary coded decimal to normal decimal numbers
byte bcdToDec(byte b)
{
  return( (b/16*10) + (b%16) );
}

//*********************************************************************************************************************
  
// RTC: Sets time  
void setDS3231time(byte second, byte minute, byte hour)
{
  // sets time and date data to DS3231
  Wire.beginTransmission(DS3231_I2C_ADDRESS);
  Wire.write(0); // set next input to start at the seconds register
  Wire.write(decToBcd(second)); 
  Wire.write(decToBcd(minute));
  Wire.write(decToBcd(hour));
  Wire.endTransmission();
}

//*********************************************************************************************************************

// RTC: Reads time
void readDS3231time(byte *second, byte *minute, byte *hour)
{
  Wire.beginTransmission(DS3231_I2C_ADDRESS);
  Wire.write(0); // set DS3231 register pointer to 00h
  Wire.endTransmission();
  Wire.requestFrom(DS3231_I2C_ADDRESS, 7);
  // request seven bytes of data from DS3231 starting from register 00h
  *second = bcdToDec(Wire.read() & 0x7f);
  *minute = bcdToDec(Wire.read());
  *hour = bcdToDec(Wire.read() & 0x3f);
}

//*********************************************************************************************************************

// ADC: pulses ADC to get digital word that is proportional to mass
long readADC(int DT, int CLK)
{

   // Power up ADC
   digitalWrite(CLK, LOW);
        
   // wait for chip to be ready
   while(digitalRead(DT) == HIGH);
        
   byte data[3];

   // pulse the clock pin 24 times to read
   for (byte i = 3; i--;) 
        {
	for (char j = 8; j--;) 
            {
	    digitalWrite(CLK, HIGH);
	    bitWrite(data[i], j, digitalRead(DT));
	    digitalWrite(CLK, LOW);
	    }
	}
        
        // Pulse clock for correct channel and gain on ADC (Ch A, 128)
        digitalWrite(CLK, HIGH);
        digitalWrite(CLK, LOW);
        
        // Power down ADC
        digitalWrite(CLK, LOW);
	digitalWrite(CLK, HIGH);	
	
	data[2] ^= 0x80;

	return ((uint32_t) data[2] << 16) | ((uint32_t) data[1] << 8) | (uint32_t) data[0];
}

//*********************************************************************************************************************

// Returns current mass of food
float readFood()
{
  return (M1*readADC(DT1, CLK1) + Y1);
}

//*********************************************************************************************************************

// Returns current mass of water
float readWater()
{
  return (M2*readADC(DT2, CLK2) + Y2);
}

//*********************************************************************************************************************

// Returns true if cat has eaten, current value has changed above and below thresholds
boolean didCatEat(float currentFood)
{
 if(previousFood - currentFood > 1.0 && previousFood - currentFood < 60.0)
   return true;
 else return false;
}

//*********************************************************************************************************************

// Returns true if cat has drank, current value has changed above and below thresholds
boolean didCatDrink(float currentWater)
{
 if(previousWater - currentWater > 1.0 && previousWater - currentWater < 60.0)
   return true;
 else return false;
}

//*********************************************************************************************************************

// Returns true if cat is sensed in box
boolean inBox()
{
 return digitalRead(16) == true; 
}

//*********************************************************************************************************************

// Updates Thingspeak real time data feeds
void updateThingspeak(float food, float water, int t)
{
 
 String field1 = "";
 String field2 = "";
 String field3 = "";
 String field1data = "";
 String field2data = "";
 String field3data = "";
  
 if(food > 0)
  {
   field1 = "&field1=";
   field1data = (String)food;
  }
  
  if(water > 0)
  {
   field2 = "&field2=";
   field2data = (String)water;
  } 
  
  else if(t > 0)
  {
   field3 = "&field3=";
   field3data = (String)t;
  } 
  
   if (client.connect("184.106.153.149", 80))
   {
    client.print("GET /update?key=" + dataAPI + field1 + field1data + field2 + field2data + field3 + field3data + " HTTP/1.1\r\n");
    client.print("Host: api.thingspeak.com\r\n"); 
    client.print("Accept: */*\r\n"); 
    client.print("\r\n"); 
    client.flush();
    client.stop();
   }
   else
   {
    // Check connection ten times, in two minutes, if successful try again, else give up
    for(int i=0; i < 10; i++)
      {
        if(!client.connect("184.106.153.149", 80))
         {
          beginWiFi();
          delay(12000);
         }
        else break;
      }
    updateThingspeak(food, water, t);
   }
}

//*********************************************************************************************************************

// Connect WiFi
void beginWiFi()
{
 // Connect to wifi
WiFi.begin(ssid, password);

// Allow time to make connection
while (WiFi.status() != WL_CONNECTED) 
    delay(500); 
}

//*********************************************************************************************************************

// Tweets when food or water bowl is too low
void tooLow(int Bowl, int hour, int minute)
{
  // Set to "0" when minute or second are < 10 such that reads :07 instead of :7
  String minuteMSD = "";

  if(minute < 10)
    minuteMSD = "0";
  
  String message;
  if(Bowl == 1)
  {
    message = emptyFood(hour, minute);
    foodTooLowSent = true;
  }
  else if(Bowl == 2)
  {
    message = emptyWater(hour, minute);
    waterTooLowSent = true;
  }
  
  
  if (client.connect("184.106.153.149", 80))
   {
   client.print("GET /apps/thingtweet/1/statuses/update?key=" + twitterAPI + "&status=" + message + " HTTP/1.1\r\n");
   client.print("Host: api.thingspeak.com\r\n"); 
   client.print("Accept: */*\r\n"); 
   client.print("\r\n");
   client.flush();
   client.stop();
   }
   else
   {
    // Check connection ten times, if successful try again, else give up
    for(int i=0; i < 10; i++)
      {
        if(!client.connect("184.106.153.149", 80))
         {
          beginWiFi();
          delay(12000);
         }
        else break;
      }
    tooLow(Bowl, hour, minute);
   }
}   

//*********************************************************************************************************************

// Tweets when food or water bowl has been refilled
void refilled(int Bowl, int hour, int minute)
{
 
 String minuteMSD = "";
 
 if(minute < 10)
    minuteMSD = "0";

 String message;
  if(Bowl == 1) 
  {
    message = refilledFood(hour, minute);
    foodTooLowSent = false;
  }

  else if(Bowl == 2)
  {
    message = refilledWater(hour, minute);
    waterTooLowSent = false;
  }
  
  if (client.connect("184.106.153.149", 80))
   {
   client.print("GET /apps/thingtweet/1/statuses/update?key=" + twitterAPI + "&status=" + message + " HTTP/1.1\r\n");
   client.print("Host: api.thingspeak.com\r\n"); 
   client.print("Accept: */*\r\n"); 
   client.print("\r\n");
   client.flush();
   client.stop();
   }
   else
   {
    // Check connection ten times, if successful try again, else give up
    for(int i=0; i < 10; i++)
      {
        if(!client.connect("184.106.153.149", 80))
         {
          beginWiFi();
          delay(12000);
         }
        else break;
      }
    refilled(Bowl, hour, minute);
   }
}   

//*********************************************************************************************************************

// Tweets end of day results and resets daily totals
void endOfDayTweet(float dailyFood, int freqFood, float dailyWater, int freqWater, int dailyLitterBoxTime, int freqLitterBox)
{
   if (client.connect("184.106.153.149", 80))
   {
    client.print("GET /apps/thingtweet/1/statuses/update?key=" + twitterAPI + "&status=" + "Today I ate " + dailyFood + " g of food in "
               + freqFood + " visits, drank " + dailyWater + " ml of water in " + freqWater + " visits, and I pottied " 
               + freqLitterBox + " times for " + dailyLitterBoxTime + " seconds total!" + " HTTP/1.1\r\n");
    client.print("Host: api.thingspeak.com\r\n"); 
    client.print("Accept: */*\r\n"); 
    client.print("\r\n");
    client.flush();
    client.stop();
   }
   else
   {
    // Check connection ten times, if successful try again, else give up
    for(int i=0; i < 10; i++)
      {
        if(!client.connect("184.106.153.149", 80))
         {
          beginWiFi();
          delay(12000);
         }
        else break;
      }
    endOfDayTweet(dailyFood, freqFood, dailyWater, freqWater, dailyLitterBoxTime, freqLitterBox);
   }
   
   if (client.connect("184.106.153.149", 80))
   {
    client.print("GET /update?key=" + dataAPI + "&field4=" + (String)dailyFood + "&field5=" + (String)dailyWater + "&field6=" + (String)dailyLitterBoxTime + " HTTP/1.1\r\n");
    client.print("Host: api.thingspeak.com\r\n"); 
    client.print("Accept: */*\r\n"); 
    client.print("\r\n"); 
    client.flush();
    client.stop();
   }
   else
   {
    // Check connection ten times, if successful try again, else give up
    for(int i=0; i < 10; i++)
      {
        if(!client.connect("184.106.153.149", 80))
         {
          beginWiFi();
          delay(12000);
         }
        else break;
      }
    endOfDayTweet(dailyFood, freqFood, dailyWater, freqWater, dailyLitterBoxTime, freqLitterBox);
   } 
}

//*********************************************************************************************************************

String emptyFood(int hour, int minute)
{
   int r = random(1, numEF+1);

   String hourMSD = "";
   String minuteMSD = "";
   
   if(hour < 10)
     hourMSD = "0";
     
   if(minute < 10)
     minuteMSD = "0";

   switch(r)
   {

   case 1:
   return "It's " + (String)hourMSD + (String)hour + ":" + (String)minuteMSD + (String)minute + " and I need some food humans!";
   break;
   
   case 2: 
   return "It's " + (String)hourMSD + (String)hour + ":" + (String)minuteMSD + (String)minute + " and my food bowl is empty...";
   break;
   
   case 3:
   return "It's " + (String)hourMSD + (String)hour + ":" + (String)minuteMSD + (String)minute + " and my food bowl is running very low.";
   break;
   
   case 4:
   return "It's " + (String)hourMSD + (String)hour + ":" + (String)minuteMSD + (String)minute + " and I can see the bottom of my food bowl again.";
   break;
   
   case 5:
   return "I'm low on food. It's " + (String)hourMSD + (String)hour + ":" + (String)minuteMSD + (String)minute + ", better start scavenging!";
   break;
   
   case 6:
   return "Dear Diary, It's " + (String)hourMSD + (String)hour + ":" + (String)minuteMSD + (String)minute + " and I'm only a few kibbles away from starvation...";
   break;
   
   case 7:
   return "It's " + (String)hourMSD + (String)hour + ":" + (String)minuteMSD + (String)minute + " and I'm out of food again. Typical humans.";
   break;
   
   case 8:
   return "My food bowl is empty at " + (String)hourMSD + (String)hour + ":" + (String)minuteMSD + (String)minute + ". WHY HAVE YOU FORSAKEN ME!?";
   break;
   
   case 9:
   return "It's " + (String)hourMSD + (String)hour + ":" + (String)minuteMSD + (String)minute + " and I'm out of food again. Useless humans!";
   break;
   
   case 10:
   return "It's " + (String)hourMSD + (String)hour + ":" + (String)minuteMSD + (String)minute + " and I finally ate all of my kibbles. Now give me some wet food!";
   break;
   
   case 11:
   return "I'm out of food at " + (String)hourMSD + (String)hour + ":" + (String)minuteMSD + (String)minute + ". I'll forgive your oversight if you give me some wet food. <3";
   break;
   } // switch

}

//*********************************************************************************************************************

String emptyWater(int hour, int minute)
{
   int r = random(1, numEW+1);

   String hourMSD = "";
   String minuteMSD = "";
   
   if(hour < 10)
     hourMSD = "0";
     
   if(minute < 10)
     minuteMSD = "0";

   switch(r)
   {

   case 1:
   return "It's " + (String)hourMSD + (String)hour + ":" + (String)minuteMSD + (String)minute + " and I need some water humans!";
   break;
   
   case 2:
   return "It's " + (String)hourMSD + (String)hour + ":" + (String)minuteMSD + (String)minute + " and my water bowl is running low, time to drink from the toilet!";
   break;
   
   case 3:
   return "I'm low on water again, It's " + (String)hourMSD + (String)hour + ":" + (String)minuteMSD + (String)minute + ", and I am parched.";
   break;
   
   case 4:
   return "Dear Diary, It's " + (String)hourMSD + (String)hour + ":" + (String)minuteMSD + (String)minute + " and I'm only a few drops away from dehydration...";
   break;
   
   case 5:
   return "It's " + (String)hourMSD + (String)hour + ":" + (String)minuteMSD + (String)minute + " and I'm out of water again. Typical humans.";
   break;
   
   case 6:
   return "My water bowl is empty at " + (String)hourMSD + (String)hour + ":" + (String)minuteMSD + (String)minute + ". WHY HAVE YOU FORSAKEN ME!?";
   break;
   
   case 7:
   return "It's " + (String)hourMSD + (String)hour + ":" + (String)minuteMSD + (String)minute + " and I'm out of water again. Useless humans!";
   break;

   
   } // switch  
}

//*********************************************************************************************************************

String refilledFood(int hour, int minute)
{
   int r = random(1, numRF+1);

  String hourMSD = "";
   String minuteMSD = "";
   
   if(hour < 10)
     hourMSD = "0";
     
   if(minute < 10)
     minuteMSD = "0";

   switch(r)
   {

   case 1:
   return "Thanks for the food at " + (String)hourMSD + (String)hour + ":" + (String)minuteMSD + (String)minute + "!";
   break;
   
   case 2:
   return "It's " + (String)hourMSD + (String)hour + ":" + (String)minuteMSD + (String)minute + ", a little late, but now I won't starve.";
   break;

   case 3:
   return "It's " + (String)hourMSD + (String)hour + ":" + (String)minuteMSD + (String)minute + ", thanks for the chow!";
   break;
   
   case 4:
   return "It's " + (String)hourMSD + (String)hour + ":" + (String)minuteMSD + (String)minute + ", and the humans have fed me kibbles again.";
   break;

   } // switch
  
}

//*********************************************************************************************************************

String refilledWater(int hour, int minute)
{
   int r = random(1, numRW+1);

   String hourMSD = "";
   String minuteMSD = "";
   
   if(hour < 10)
     hourMSD = "0";
     
   if(minute < 10)
     minuteMSD = "0";

   switch(r)
   {

   case 1:
   return "Thanks for the water at " + (String)hourMSD + (String)hour + ":" + (String)minuteMSD + (String)minute + "!";
   break;
   
   case 2:
   return "It's " + (String)hourMSD + (String)hour + ":" + (String)minuteMSD + (String)minute + ", a little late, but now I won't dehydrate.";
   break;
   
   case 3:
   return "It's " + (String)hourMSD + (String)hour + ":" + (String)minuteMSD + (String)minute + ", and the water bowl is full again. I wish my food was wet too...";
   break;
   
   case 4:
   return "It's " + (String)hourMSD + (String)hour + ":" + (String)minuteMSD + (String)minute + " and water has appeared! Time to stick my beans in it.";
   break;
   
   } // switch 
  
}
