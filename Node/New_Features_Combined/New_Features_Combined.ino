//-----Libraries--------------------------------------------------
#include <SPI.h>       //SPI communication
#include <SD.h>        //sd card functionality
#include <SPIFlash.h>  //Flash library for the Moteino Mega flash_CS pin is D23 on this board, Open_Irr is configured with a 4 MBit (0.5 Megabyte) Flash chip.
// Note: if other SPI devices are present, ensure their CS pins are pulled up or set HIGH
#include <Wire.h>               //I2C functions for rtc
#include <OneWire.h>            //OneWire protocol for DS18B20
#include <DallasTemperature.h>  //To ease DS18b20 use
#include "time.h"               //for time elements
#include "RTClib.h"
#include <EEPROM.h>              //Built in EEPROM routines
#include <DS3232RTC.h>           //Precision (generic) RTC library for DS3232 rtc module
#include <Streaming.h>           //C++ style output with << operator, maybe not needed with new rtc code?
#include <LowPower.h>            //Low power functionality
#include <avr/sleep.h>           //Sleep Functions
#include "avr/io.h"              //Register set and bit values for Atmel chips
#include "avr/interrupt.h"       //Interrupt functionality
#include "avr/wdt.h"             //Watchdog timer
#include <CD74HC4067.h>          //For controlling Multiplexor
#include <RH_RF95.h>             //RadioHead library for the RF95 radio transceiver
#include <RHReliableDatagram.h>  //Additional Radiohead library for ease of transmission
#include <RadioString.h>         //Library to ease sending and receiving of large strings
#include <ArduinoJson.h>         //Json file format use
#include <ArduinoJson.hpp>       //Json file format use
#include <math.h>                //Mathmatical functions

//-----Assign Pins-------------------------------------------------
#define LED 15         //LED pin on Moteino Mega board
#define DS18B20_pin 1  //Data line for Ds18b20, note all ds18b20 sensors are on a common data line / pullup resistor

/*
Plan to remove macros for in1, in2, etc. and replace with new int array
*/

int io_pins[] = {12, 13, 14, 3};

#define in1 12  //The four pins on the Moteino-Mega that can be used as an low-level output
#define in2 13
#define in3 14
#define in4 3

// ins = [12, 13, 14, 3]

#define MAX_PACKET_LEN (RH_RF95_MAX_MESSAGE_LEN - 1)  //250
#define MAX_STRING_LEN (MAX_PACKET_LEN * (2 ^ 8))     //64000 bytes, one character in string/packet = 1 byte

#define WM_path1 26            //Pin for path1 of WM reading, to switch polarity
#define WM_path2 27            //Pin for path2 of WM reading, to switch polarity
#define WM_analog_read_pin A1  //Analog Pin (A1) for reading the WM sensors
#define mux_enable 22          //Pin for EN (enable) of the CD7HC4067 multiplexor
//mux select pins for s0,s1,s2,s3 are respecively digital pins 18,19,20,21
#define pin_mBatt 30         //Digital pin to activate battery voltage measurement circuit
#define pin_battV 31         //Analog pin to read battery voltage (31 ~ A7)
#define ADC_REF_VOLTAGE 3.3  //The reference output of the moteino mega board
#define ADC_RESOLUTION 1024  //The analog digital converter in moteino mega is 10 bit resolution
#define ADC_MAXVALUE 1023    //1024 possibilities including 0 (0-1023) on 10 bit ADC

#define SD_CS 28  //Chip select (CS) pin on sd card module

#define FLASH_SS 23  //Slave select (SS) pin for the flash chip on moteino mega -> uses SPI

#define RTC_Interrupt 24  //(A0) for RTC_SQW for interrupts

#define NUM_SECONDS_IN_WEEK 7*24*60*60

//-----Declare Global Variables-------------------------------------------

char filename[] = "000_Data.txt";  //sd card file name, 000 to be replaced by IDnum

char irrigation_events_data[] = "irr_data.txt";
char permitted_events_data[] = "per_data.txt";
char den_events_data[] = "den_data.txt";

uint16_t expectedDeviceID = 0xEF30;  //expected flash manufacturer ID of Moteino mega
//////////////////////////////////////////
// flash(SPI_CS, MANUFACTURER_ID)
//
// MANUFACTURER_ID - OPTIONAL, 0x1F44 for adesto(ex atmel) 4mbit flash
//                             0xEF30 for windbond 4mbit flash
//                             0xEF40 for windbond 64mbit flash
//////////////////////////////////////////

char a[4];  //char array for itoa function
byte i;     //Global for-loop counter

uint8_t radioID;                     //Have to have a placeholder for radioID
char Data[RH_RF95_MAX_MESSAGE_LEN];  //Placeholder to be filled with data to transmit
char Data2[RH_RF95_MAX_MESSAGE_LEN];
uint8_t int_buff[RH_RF95_MAX_MESSAGE_LEN];  //integer type array buffer
uint8_t length_buff;                        //integer denoting the size of the buffer
uint8_t from_id;                            //integer specifying the radio id that a transmission is coming from

uint16_t TRANSMIT_TIMEOUT = 2000;  //Set the minimum retransmit timeout. If sendtoWait is waiting for an ack longer than this time (in milliseconds), it will retransmit the message. Defaults to 200ms.
uint8_t TRANSMIT_POWER = 20;       //Set transmission power, 0-20,  defaults to 14
float RADIO_FREQUENCY = 915.0;     //Set frequency of radio module
uint16_t ROUTINE_TIMEOUT = 2000;   //Set delay at the end of the routine, too low and some packets will be missed, too high and you may also miss packets
uint8_t RETRY_NUM = 3;             //times to send packets without receiving ACK

float battV;           //battery voltage
float lowBatt = 4.0;   //low-battery limit
bool battLow = false;  //tracks if battery voltage falls below specified threshold

byte secs;  //time and date values, RTClib
byte mins;
byte hrs;
byte dow;
byte days;
byte mnths;
int yrs;
byte alarm_1_Mins;
tmElements_t tm;

boolean isMenuOn = false;
int menuinput;                //user input to menu prompt
long menutimeout;             //length of time to wait for user input
int indata;                   //user input data
int input;                    //for conversion of indata in getdata()
int numincoming;              //To indicate how many bytes are coming by user input
int incoming[7];              //Arrays are 0 indexed so this is enough room for 1 Binary character (1byte|8bits)
char charInput[200]{ '\0' };  //for charinput function
char incomingChar[200]{ '\0' };

const byte numChars = 32;  //for reading in character user input

int WM_group1_mean;  //Integer to hold WM_group means. Group 1 to 4 means will be attached to a corresponding relay in pin, 1-4
int WM_group2_mean;
int WM_group3_mean;
int WM_group4_mean;
int wm_grace_window = 10;  //Define the tolerance window (+ or - ,in kpa) for removing an individual sensor from the calculation of the group mean. Only triggers if pdiff from raw mean is >= 20.
bool force_irr = false;
bool new_irr_event = false;  //set true to write eeprom.object to moteino eeprom, thereby saving the record of the event

char local_time_irr_update[numChars]{ '\0' };  //Empty character array to dump local time stamp from unix time

String data = "";
String header = "";
String WM_string = "";
String irrigation_prompt_string = "";

String temperature_string = "";
float Temp;

//Define Error Log Structure//
struct error_log_struct {  // was typedef struct {}error_log_struct; 7/27/2023

  bool write_log;
  char write_time[18]{ '\0' };

  struct sd_log {
    bool card_begin_failure;
    bool open_file_failure;

  } sd_struct;

  struct flash_log {

  } flash_struct;

  struct rtc_log {
    bool rtc_begin_failure;
    bool rtc_lostPower_failure;
  } rtc_struct;

  struct pwr_log {
    bool bat_low;
  } pwr_struct;

  struct wm_log {
    //Short sensor will be "s", 115 as a decimal
    //Open sensor will be "o", 111 as a decimal
    //Default sensor will be "d", 100 as a decimal
    //Positive value will be "p", 112 as a decimal
    bool mux_channel_error[16]{ false };
    uint8_t mux_channel_error_code[16]{ '\0' };
  } wm_struct;

  struct ds18b20_log {
    //Open sensor (reading -127) will be "o", 111 as a decimal
    //sensor reset during conversion (reading 85) will be "r", 114 as a decimal
    //Cause of error (reading -98) is unknown at this time and will be "u", 117 as a decimal
    bool ds_unit_error[16]{ false };
    uint8_t ds_unit_error_code[16]{ '\0' };
  } ds18b20_struct;

  struct irr_log {
    //Force of event due to error count will be "e", 101 as a decimal
    //Force of event due to outlier count will be "o", 111 as a decimal
    //Prevention of a forced event due to timeframe (minimum time between not elapsed) will be "t", 116 as a decimal
    //Prevention of a forced event due to permission window will be "p", 112 as a decimal
    //Prevention of a forced event as raw mean is higher than set threshold will be "h", 104 as a decimal
    uint8_t forced_group_code[4]{ '\0' };
    bool forced_group[4]{ false };
  } irr_struct;

  struct radio_log {
    bool manager_init_failure;
    bool packet_failure;  //holder as currently we cannot tell if the packet failed, that prompt is given by library code...
  } radio_struct;
};

error_log_struct update_error_log;  //globally declared extra to reset the eeprom_object.error_log elements easily

const size_t capacity = 5 * JSON_ARRAY_SIZE(4) + 2 * JSON_OBJECT_SIZE(4) + 4 * JSON_OBJECT_SIZE(16) + 100;  // From assessment tool https://arduinojson.org/v6/assistant/
ArduinoJson::StaticJsonDocument<capacity> jsonBuffer;                                                       // Hopefully declaring once here is OK


//EEPROM structure method for storing and retrieving values from eeprom: relevant for variables defined in the menu
// Note that each EEPROM position can save only one byte of information, i.e. 8-bit numbers 0-255 and leading values (001) ARE NOT INTERPRETABLE
// but more than one position can be accessed using eeprom.put and eeprom.get with a defined structure
// this also enables saving different data types together
// Note that you CANNOT store/update pointers in EEPROM
//ATMEL EEPROM LIFESPAN ~ 100,000 read/writes
//ATMEL FLASH LIFESPAN ~ 10,000 read/writes

int eeprom_address = 0;

struct eeprom_struct {
  uint8_t nodeID;                       //nodeID for radio networking
  uint8_t IDnum;                        //numeric board identifier 0-255
  uint8_t gatewayID;                    //gatewayID for radio networking
  char projectID[numChars] = { '\0' };  //Project identifier

  boolean firstTime = true;  //flag for first time for writing to sdcard module

  boolean is_water_manager_on;  //flag to initiate drought stress management routine

  int group_irr_thresholds[4]{};   //as an array, for defining water threshold for groups 1 to 4, 0 to -255, integer type
  long min_time_btwn_irr[4]{ 0 };  //Array of type long for defining a minimum time between irrigation events, user enters value of minutes and timing must be called in milliseconds (minutes * 60 * 1000)
  long irr_period[4]{ 0 };         //Array of type long for defining the duration of an irrigation events

  boolean include_resistance;  //for specifying if resistance is to be included in written data string. true=1 false=0

  boolean calibration_resistor_present;  //Is a calibration resistor present?
  uint8_t cal_resistor_loc;              //Channel location of the calibration resistor between the Multiplexors
  float cal_resistor_val;                //Value of the calibration resistor installed
  int fixed_resistor_val;                //The value of the fixed resistor attached in series to the sensor and ground (voltage divider circuit), an unchanged schematic will be 10,000 ohms
  uint8_t num_WM;                        //Define the number of watermark sensors present

  uint8_t WM_group1[16]{ 0 };  //Array to hold mux channel location of WM sensors to average.
  uint8_t WM_group2[16]{ 0 };
  uint8_t WM_group3[16]{ 0 };
  uint8_t WM_group4[16]{ 0 };

  uint8_t num_ds18b20;         //Define the number of ds18b20 temperature sensors present
  uint8_t ds18b20_sensor0[8];  //Define locations to store ds18b20_sensor_addresses
  uint8_t ds18b20_sensor1[8];
  uint8_t ds18b20_sensor2[8];
  uint8_t ds18b20_sensor3[8];
  uint8_t ds18b20_sensor4[8];
  uint8_t ds18b20_sensor5[8];
  uint8_t ds18b20_sensor6[8];
  uint8_t ds18b20_sensor7[8];
  uint8_t ds18b20_sensor8[8];
  uint8_t ds18b20_sensor9[8];
  uint8_t ds18b20_sensor10[8];
  uint8_t ds18b20_sensor11[8];
  uint8_t ds18b20_sensor12[8];
  uint8_t ds18b20_sensor13[8];
  uint8_t ds18b20_sensor14[8];
  uint8_t ds18b20_sensor15[8];

  uint8_t ALARM_1_Interval = 1;  //Set the interval for alarm 1 (wake and run routine), default is 1

  uint8_t n_channels_wm_group1;  //Integer to hold # of channels utilized in each WM_group mean
  uint8_t n_channels_wm_group2;
  uint8_t n_channels_wm_group3;
  uint8_t n_channels_wm_group4;

  bool demo_mode;  //controls trouble shooting demo loop or the real loop

  bool toggle_radio;  //can turn off radio transmission when there is no gateway utilized

  uint32_t last_irr_unix_time[4]{};  // to store the unix time of the last irrigation event for each group.

  bool run_notes;  //to control printing of extraneous runtime data during the loop.

  bool latchingValve;

  error_log_struct error_log;  //nested array element with proper structure for error_log

  // Initally all -1, if an event is upcoming for group i, the 
  int current_events[4];

  int num_to_update = 0;

  // 2D array with match fields, matchFields[0][i] are the match fields for hours, matchFields[1][i] the match fields for minutes, and matchFields[2][i] are the match fields for seconds
  // had to change uint16_t to int to avoid type errors with assignment using indata
  uint8_t matchFields[3][8];    //arbitrarily length 24? how many match fields are reasonable to expect?
  uint8_t numFields[3]; // Stores the number of fields for seconds, minutes, and hours respectively

  uint16_t num_events = 0;

  int events_queue[50];
  int events_queue_size = 0;

  //valve valves[10];
  int current_valve_id = 0;
  int num_valves = 0;
  int valve_group_id_lookup[4];
};

eeprom_struct eeprom_object = {};  //Declare an object with the eeprom_struct structure, access objects as eeprom_object."element of struct without quotes"

unsigned long group_millis[4];

enum event_state {
  IDLE,
  WAITING,
  IRRIGATING
};

event_state curr_state;

// new states: IDLE, WAITING, IRRIGATING, RADIO, 

States group_states[4];

bool group_is_done[4];

//-----Initialize-----------------------------------------------------
RTC_DS3231 rtc;

CD74HC4067 mux_1(18, 19, 20, 21);  //connect s0,s1,s2,s3 select pins on mux to specified digital pins
CD74HC4067 mux_2(18, 19, 20, 21);  //The same pins are connected to mux 2 for reading Watermark200ss

OneWire oneWire(DS18B20_pin);         //The Data pin for the DS18b20 temp sensors
DallasTemperature sensors(&oneWire);  //Pass OneWire reference to Dallas Temperature

DeviceAddress DS18B20_Address;  //Array to hold sensor addresses

RH_RF95 driver(4, 2);
RadioString manager(driver, radioID);  //Set up the radio manager, with RadioString library

File myfile;  //make sd card file object

//FlashTools flash;                           //initialize flash memory

//-----Sleep Functions----------------------------------------------

//Power saving functions referenced from ArduinoSoilH2O <https://github.com/ArduinoSoilH2O>

//Trying new rtclib update...
void setRTCInterrupt() {
  sei();                    //turn on Global Interrupt Enable
  PCMSK0 |= (1 << PCINT0);  //set A0/D24/Chippin 37 as PCINT
  PCICR |= (1 << PCIE0);    //enable interrupts on vector 0
}
void clearRTCInterrupt() {
  PCICR &= (1 << PCIE0);
}

ISR(PCINT0_vect) {
  sleep_disable();
  clearRTCInterrupt();
}

// Radio events, static events that need to be shceduled
// Avoid scheduling certain events at the same time

// Might be able to track state of valve, when we pulse and irrigation event not ever, can't schedule another event

// Find times to next pulse for each of the non latching valves that are also irrigating: t1, t2, etc.
// Find the smallest


// If the smallest is within 200ms, then exit current function and pulse quickly
// void pulse_non_latching_valves(){
//   // Loop through all the active valves

//   // See if valve is non latching

//   // If time now is less than x ms from next pulse, pulse

// }

// Stores the correct valve for group i (value is the index of the valve in valves)
// int valveLookupTable[4];

// One option - 
// Another option - file stuff


// Initially timeEvaluationConsideration
struct event{
  event_state state;
  unsigned long event_millis;
  int event_id;
  //bool groups[4]; // Makes sense to be here since if we have an array in eeprom, it's more difficult to update indices when removing
  int group;
  // Could use pointers or references instead
  bool recurring;              //false = singular
  // bool permit;                 //false = deny
  uint8_t event_type;           //0=measurement, 1=irrigation, 2=permit window, 3=deny window....
  
  //uint16_t matchFields[24];    //arbitrarily length 24? how many match fields are reasonable to expect?

  DateTime* span[2];  //DateTime classes for start and end dates/times for a particular event

  event(int group, bool recurring, uint8_t event_type, DateTime* span[2]): group(group), recurring(recurring), event_type(event_type){
    this->span[0] = span[0];
    this->span[1] = span[1]; 
  }

  event(): group(-1), recurring(false), event_type(-1){}

  void print(){
    Serial.print(F("Group: "));
    Serial.println(group);
    Serial.print(F("Recurring: "));
    if (recurring){
      Serial.println("Y");
    }
    else{
      Serial.println("N");
    }
    Serial.print(F("Event Type: "));
    Serial.println(event_type);
    if (recurring){
      print_recurring_event_span();
    }
    else{
      print_singular_event_span();
    }
    Serial.println();
    Serial.println();
  }

  void print_singular_event_span(){
    Serial.println(F("Start"));
    Serial.print(F("Year: "));
    Serial.println(span[0]->year());
    Serial.print(F("Month: "));
    Serial.println(span[0]->month());
    Serial.print(F("Day: "));
    Serial.println(span[0]->day());
    Serial.print(F("Hour: "));
    Serial.println(span[0]->hour());
    Serial.print(F("Minute: "));
    Serial.println(span[0]->minute());
    Serial.print(F("Second: "));
    Serial.println(span[0]->second());
    
    Serial.println(F("End"));
    Serial.print(F("Year: "));
    Serial.println(span[1]->year());
    Serial.print(F("Month: "));
    Serial.println(span[1]->month());
    Serial.print(F("Day: "));
    Serial.println(span[1]->year());
    Serial.print(F("Hour: "));
    Serial.println(span[1]->hour());
    Serial.print(F("Minute: "));
    Serial.println(span[1]->minute());
    Serial.print(F("Second: "));
    Serial.println(span[1]->second());
  }

  void print_recurring_event_span(){
    Serial.println(F("Start"));
    Serial.print(F("Day: "));
    Serial.println(span[0]->dayOfTheWeek());
    Serial.print(F("Hour: "));
    Serial.println(span[0]->hour());
    Serial.print(F("Minute: "));
    Serial.println(span[0]->minute());
    Serial.print(F("Second: "));
    Serial.println(span[0]->second());
    
    Serial.println(F("End"));
    Serial.print(F("Day: "));
    Serial.println(span[1]->dayOfTheWeek());
    Serial.print(F("Hour: "));
    Serial.println(span[1]->hour());
    Serial.print(F("Minute: "));
    Serial.println(span[1]->minute());
    Serial.print(F("Second: "));
    Serial.println(span[1]->second());
  }

};

event* events[500];




int global_num_events = 0;

void set_alarms(int interval_seconds){
  sei();
  wdt_disable();                         // turn off watchdog timer From ArduinoSoilH2O
  rtc.disable32K();                      // Turn off 32kHz output
  pinMode(RTC_Interrupt, INPUT_PULLUP);  // Making it so, that the alarm will trigger an interrupt

  //Schedule an alarm
  attachInterrupt(digitalPinToInterrupt(RTC_Interrupt), on_alarm, FALLING);
  rtc.clearAlarm(1);
  rtc.clearAlarm(2);
  rtc.writeSqwPinMode(DS3231_OFF);  //stop oscillating signals at sqw pin
  rtc.disableAlarm(2);              //remove if using alarm2...
  if (!rtc.setAlarm1(
        rtc.now() + TimeSpan(interval_seconds), DS3231_A1_Second  // i.e. tells it to match seconds to value in timespan + now...
        )) {
    Serial.println(F("Error, alarm wasn't set!"));
  } else {
    Serial.println(F("Alarm is set!"));
  }
}

void on_alarm(){
  Serial.println(F("Alarm occured"));
}

void Low_Power_Sleep() {
  Serial.println(F("Going to sleep..."));
  // Serial.print(F("Current Measurement Interval: "));
  // Serial.print(eeprom_object.ALARM_1_Interval);
  // Serial.print(F("  Minutes. Next measurement to occur at:  "));
  // DateTime now = rtc.now();                           //needed to get unix time in next line
  // uint32_t current_unix_epoch_time = now.unixtime();  //get current unix epoch time
  // local_time(current_unix_epoch_time + (eeprom_object.ALARM_1_Interval * 60));
  // Serial.print(local_time_irr_update);
  // Serial.println();

  Serial.end();  //disable serial communication
  delay(10);
  digitalWrite(15, LOW);  //Turn off the LED pin (15 on moteino mega)
  delay(10);
  driver.sleep();  //Put radio to sleep
  delay(10);
  analogComp_off();  //turn off analog comparator
  delay(10);
  ADC_off();  //turn off ADC
  delay(10);
  JTAG_off();  //disable On-Chip Debug system
  delay(10);
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);  //Power down completely on sleep
  delay(10);
  sleep_enable();
  delay(10);
  setRTCInterrupt();  //trying new rtclib update
  delay(100);
  sleep_mode();  //Puts MEGA to sleep

  //This happens after wake up Interrupt-----
  ADC_on();
  Serial.begin(9600);
  SPI.begin();
}

//------------- Power Savers ---------------------

void analogComp_off() {  //Turn off analog comparator
  ACSR &= ~(1 << 7);
}

void analogComp_on() {  //Turn on analog comparator
  ACSR |= (1 << 7);
}

void ADC_off() {  //Turn off ADC  --  this saves about 113 uA
  ADCSRA &= ~(1 << 7);
}

void ADC_on() {  //Turn on ADC
  ADCSRA |= (1 << 7);
}

void JTAG_off() {  //We don't ever need to turn it on for this application  //Must be executed twice within 4 clock cycles to disable JTAG!
  cli();
  MCUCR |= (1 << 7);
  MCUCR |= (1 << 7);
  sei();
}

// void check_for_irrigation(){
//   DateTime now = rtc.now();
//   // get current time, look at day, hour, second, minute, second
//   // compare to intervals
//   uint16_t day = now.day();
//   uint16_t hour = now.hour();
//   uint16_t minute = now.minute();
//   uint16_t second = now.second();

//   // Can get epoch time, seconds from Jan 1, 1970, work modulo number of seconds in a week

//   // Can convert day, hour, minute, second to seconds since start of week (Monday 12 AM?)
//   // second + minute*60 + hour*60*60 + day*24*60*60

//   // loop through the array/vector of time windows. Look at the start seconds and end seconds

//   // if seconds_since_start_of_week >= time_windows[]
  
//   // IF want interval from one week to spill over to the next, split the interval, go from start - MOnday 12:00 AM and then Monday 12:00 AM to end.
  
  
  
//   // Format: YYYY MM DD HH MM SS
//   // Read in as a string, split on spaces, convert each piece to int32
//   // create 2 DateTime objects
//   // Constructor: DateTime(YYYY, MM, DD, HH, MM, SS, MS)
//   // uint32_t current_unix_epoch_time = now.unixtime();
// }

//-----Get User input as integer-----------------------------------------------
void get_integer_input() {
  menutimeout = millis() + 60000;  // time to wait for user to input something, was 10 secs
  int sign = 1;                    // for handling negatives
  indata = 0;                      // initialize
  while (millis() < menutimeout)   // wait for user to input something
  {
    if (Serial.available() > 0)  // something came in to serial buffer
    {
      delay(100);                         // give time for everything to come in
      numincoming = Serial.available();   // number of incoming bytes (1 byte = 1 HEX (ASCII) character or 8bits of binary)
      for (i = 1; i <= numincoming; i++)  // read in everything
      {
        incoming[i] = Serial.read();                 // read from buffer
        if (incoming[i] == 13 || incoming[i] == 10)  // ignore carriage return & line feed
        {
        } else if (incoming[i] == '-') {                      //if a minus is read in
          sign = -1;                                          //update sign to neagative 1
        } else if (incoming[i] >= '0' && incoming[i] <= '9')  // otherwise
        {
          input = incoming[i] - '0';            // convert ASCII value to ??numerical equivalent?? decimal.
          indata = indata * 10 + input * sign;  // assemble to get total value if sequence of numbers
        }
      }
      break;  // exit before menutimeout
    }
  }
  Serial.println(indata);  //return entered value for visual user feedback
  delay(10);
}

//-----Get User Input for Character variables etc.-----------------------------------

void charinput() {
  memset(charInput, 0, sizeof(charInput));
  delay(50);
  long timeout;
  timeout = millis() + 60000;  //length of time to wait for user input, was 10sec
  byte numincoming;            //for # of bytes incoming

  while (millis() < timeout) {
    if (Serial.available() > 0) {
      delay(100);
      numincoming = Serial.available();
      for (byte i = 0; i <= numincoming; i++) {
        incomingChar[i] = Serial.read();
        if (incomingChar[i] == 13 || incomingChar[i] == 10) {
        } else {
          charInput[i] = incomingChar[i];
        }
      }
      charInput[numincoming] = 0;
      break;
    }
  }
  Serial.println(charInput);
}

void updateEEPROM() {

  eeprom_address = 0;                         //clear eeprom_address
  EEPROM.put(eeprom_address, eeprom_object);  //update chip EEPROM if there are any changes from what was saved...
  eeprom_address = 0;                         //clear eeprom_address
  Serial.println(F("EEPROM updated"));
}



//-----Store data on micro sd card----------------------------------

void writeFileSD(const char *path, const char *message) {


  Serial.print(F("writeFileSD path: "));  //limited to 13 characters including extension
  Serial.println(path);

  File file = SD.open(path, FILE_WRITE);  // This works when the name of the error log saved was changed.... we might be running into filename length restrictions here, but not sure why not on the esp32...
  delay(10);
  if (!file) {
    Serial.println(F("- failed to open file for writing"));
    return;
  }
  if (file.println(message)) {
    Serial.println(F("- file written"));
  } else {
    Serial.println(F("- write failed"));
  }
  file.close();
}

void latchingValveModuleReturnToIdle() {
  //default/idle condition to save on current consumption
 
  //##SINGLE##
  // digitalWrite(in1, LOW);  //Pin states during IDLE state
  // digitalWrite(in2, LOW);
  // digitalWrite(in3, LOW);
  // digitalWrite(in4, LOW);
  // Serial.println(F("Default/Idle condition to save on current consumption."));
 
  //Multiple WORKS
  for (int i = 0; i < 4; i++){
    digitalWrite(io_pins[i], LOW);  //Pin states during IDLE state
  }
  Serial.println(F("Default/Idle condition to save on current consumption."));
}
has context menu

void setup() {
  Serial.begin(9600);

  Wire.begin();  //enable I2C bus for rtc
  SPI.begin();

  //-----Pin settings-----

  pinMode(SD_CS, OUTPUT);  //CS pin for sd card

  /*
  The new program uses the io_pins array as opposed to in1, in2, etc.
  */

  for (int i = 0; i < 4; i++){
    pinMode(io_pins[i], OUTPUT);
  }

  latchingValveModuleReturnToLow();

  pinMode(WM_path1, OUTPUT);       //WM Sensor Vs or GND
  pinMode(mux_enable, OUTPUT);     //enable or disable the multiplexor, the EN pin on multiplexor
  pinMode(WM_path2, OUTPUT);       //WM Sensor Vs or GND
  digitalWrite(mux_enable, HIGH);  //Disable the mux on setup

  pinMode(pin_mBatt, OUTPUT);
  digitalWrite(pin_mBatt, LOW);  //Leave the battery voltage circuit open to avoid battery drain

  //-----

  battV = calcbattV();  // Get board battery level on startup

  //----- check chip EEPROM for stored data and place into "eeprom_object" with the structure of "eeprom_struct" declared earlier

  EEPROM.get(eeprom_address, eeprom_object);                                //eeprom_address may be redundant if only writing one eeprom object (i.e. it would always begin at position 0)
  int datasize_group1 = sizeof(eeprom_object.WM_group1) / sizeof(uint8_t);  //Hold the element length of the WM_group arrays, needs to be after eeprom_objects are read.
  int datasize_group2 = sizeof(eeprom_object.WM_group2) / sizeof(uint8_t);
  int datasize_group3 = sizeof(eeprom_object.WM_group3) / sizeof(uint8_t);
  int datasize_group4 = sizeof(eeprom_object.WM_group4) / sizeof(uint8_t);

  //----- Check SD card
  if (!SD.begin(SD_CS)) {
    Serial.println(F("SD card not present or card failure."));
    eeprom_object.error_log.sd_struct.card_begin_failure = true;
    eeprom_object.error_log.write_log = true;
  }

  //Flash chip routine here instead of sdcard?
  /*

  */


  //-----Set initial radio settings
  delay(10);
  driver.setFrequency(915.0);
  delay(10);
  driver.setTxPower(20, false);
  delay(10);
  manager.setThisAddress(eeprom_object.nodeID);  //Set the current address for radio communication in the manager
  delay(10);
  manager.setTimeout(2000);  //set timeout period where if an ACK not recieved it will retransmit message Default is 200ms, this will vary based on transmission packet length
  delay(10);


  //-----RTC setup-----
  if (!rtc.begin()) {
    Serial.println(F("Couldn't find RTC, routine hang up"));
    eeprom_object.error_log.rtc_struct.rtc_begin_failure = true; // saved
    eeprom_object.error_log.write_log = true;
    while (1)
      ;
  }

  if (rtc.lostPower()) {
    Serial.println(F("RTC lost power."));
    //Note that this might need changed if lostPower = true after a sleep cycle. a conditional test of Oscillator Stop Flag Bit7 in the status register. see this post  https://forum.arduino.cc/t/rtclib-reset-time-power-interrupted/639578/15
    eeprom_object.error_log.rtc_struct.rtc_lostPower_failure = true;
    eeprom_object.error_log.write_log = true;
  }

  read_events_data();
  
  Serial.println("Initialization Completed"); //saved

  
  setRTCInterrupt();
  set_alarms(8);

  // Ready to incorporate into main menu modulo some small bugs
  //events_menu();
  // The number of reads/writes on eeprom is limited, so might want to think of other ways to store information like number of events
  // The realtime clock might have some memory
  //updateEEPROM();

  menu();
  // events menu in open_irr, continue on valve_menu and this file
}

void loop() {
  Serial.println(F("Woke up and checking for upcoming irrigation events"));
  // Read in current windows for permit, deny, and irrigation
  read_events_data();
  // // If within 8 seconds of irrigation event, begin irrigation
  check_for_measurement_events();
  events_loop();
  // // // If rtc.now() of next irrigation event - rtc.now() < 8, then stay on, otherwise sleep
  // // open_valve(1); // saved
  // // open_valve(2);
  // // open_valve(3);
  // // open_valve(4);
  // //Serial.println(F("No irrigation events occuring in the near future, going back to sleep"));
  // Write current windows for permit, deny and irrigation
  //events_menu();

  write_events_data();
  write_valve_data();
  set_alarm_1_interval();
  updateEEPROM();
  Low_Power_Sleep();
}

void write_data_to_sd(){
  write_events_data();
  write_valve_data();
}

void read_data_from_sd(){
  read_valve_data();
  
}



//-----Menu Routine----------------------------------------------------
void menu() {

  if (Serial.available() > 0) {
    Serial.read();  //clear serial input buffer
  }

  itoa(eeprom_object.IDnum, a, 10);  //convert IDnum to character array

  if (eeprom_object.IDnum < 10) {  // for naming filename
    filename[0] = '0';             // put into filename[] array
    filename[1] = '0';
    filename[2] = a[0];
  } else if (eeprom_object.IDnum < 100) {
    filename[0] = '0';  // put into filename[] array
    filename[1] = a[0];
    filename[2] = a[1];
  } else {
    filename[0] = a[0];  // put into filename[] array
    filename[1] = a[1];
    filename[2] = a[2];
  }
  //print out board information-----
  Serial.println();
  Serial.println(F("Open_Irr: soil water tension management system"));
  Serial.print(F("Project ID:  "));

  for (uint8_t x = 0; x < numChars; x++) {
    if (eeprom_object.projectID[x] != '\0') {
      Serial.print(eeprom_object.projectID[x]);
    } else {
      break;  //testing
    }
  }
  Serial.println();

  Serial.print(F("Board ID: "));
  Serial.println(eeprom_object.IDnum);
  Serial.print(F("Node ID: "));
  Serial.println(eeprom_object.nodeID);
  Serial.print(F("Gatway ID:  "));
  Serial.println(eeprom_object.gatewayID);

  Serial.print(F("Data file name: "));
  Serial.println(filename);

  readRTC();
  Serial.print(F("Current date: "));
  Serial.print(mnths);
  Serial.print('-');
  Serial.print(days);
  Serial.print('-');
  Serial.println(yrs);

  Serial.print(F("Current time:  "));
  Serial.print(hrs);
  Serial.print(':');
  if (mins < 10) {
    Serial.print('0');
  }
  Serial.print(mins);
  Serial.print(':');
  if (secs < 10) {
    Serial.print('0');
  }
  Serial.println(secs);

  Serial.print(F("Current Battery Voltage:  "));
  battV == calcbattV();
  delay(10);
  Serial.print(battV);
  Serial.println(F("V"));

  Serial.print(F("Current Measurement Interval: "));
  Serial.print(eeprom_object.ALARM_1_Interval);
  Serial.print(F("  Minutes. Next measurement to occur at:  "));
  DateTime now = rtc.now();                           //needed to get unix time in next line
  uint32_t current_unix_epoch_time = now.unixtime();  //get current unix epoch time
  local_time(current_unix_epoch_time + (eeprom_object.ALARM_1_Interval * 60));
  Serial.print(local_time_irr_update);
  Serial.println();

  Serial.print(F("Number of WaterMark sensors currently installed:  "));
  Serial.println(eeprom_object.num_WM);
  Serial.print(F("Include Resistance Values in output:  "));
  Serial.println(eeprom_object.include_resistance);
  Serial.println(F("Current Sensor Grouping for Means:  "));
  Serial.print(F("Group1: "));
  for (int i = 0; i < 16; i++) {
    if (eeprom_object.WM_group1[i] >= 0 && eeprom_object.WM_group1[i] != 255) {
      Serial.print(eeprom_object.WM_group1[i]);
      Serial.print(F("  "));
    }
  }
  Serial.println();
  Serial.print(F("Group2: "));
  for (int i = 0; i < 16; i++) {
    if (eeprom_object.WM_group2[i] >= 0 && eeprom_object.WM_group2[i] != 255) {
      Serial.print(eeprom_object.WM_group2[i]);
      Serial.print(F("  "));
    }
  }
  Serial.println();
  Serial.print(F("Group3: "));
  for (int i = 0; i < 16; i++) {
    if (eeprom_object.WM_group3[i] >= 0 && eeprom_object.WM_group3[i] != 255) {
      Serial.print(eeprom_object.WM_group3[i]);
      Serial.print(F("  "));
    }
  }
  Serial.println();
  Serial.print(F("Group4: "));
  for (int i = 0; i < 16; i++) {
    if (eeprom_object.WM_group4[i] >= 0 && eeprom_object.WM_group4[i] != 255) {
      Serial.print(eeprom_object.WM_group4[i]);
      Serial.print(F("  "));
    }
  }
  Serial.println();

  Serial.print(F("Number of temperature sensors connected:  "));
  Serial.println(eeprom_object.num_ds18b20);

  Serial.print(F("Water Manager Status: "));
  Serial.println(eeprom_object.is_water_manager_on);
  Serial.println();

  for (int q = 0; q < 4; q++) {
    Serial.print(F("Group "));
    Serial.println(q + 1);

    Serial.print(F("Matric potential threshold for irrigation events: "));
    Serial.println(eeprom_object.group_irr_thresholds[q]);

    Serial.print(F("Time of last irrigation event: "));
    local_time(eeprom_object.last_irr_unix_time[q]);  //convert unix timestamp to local time (stored in global var), then print it.
    Serial.println(local_time_irr_update);

    Serial.print(F("Minimum time between irrigation events (in minutes): "));
    Serial.println(eeprom_object.min_time_btwn_irr[q]);

    // Probably add the unit
    Serial.print(F("Duration of irrigation events: "));
    Serial.println(eeprom_object.irr_period[q]);

    Serial.println();
  }

  Serial.print(F("Radio Transmission is:  "));

  if (eeprom_object.toggle_radio) {
    Serial.println(F("ON."));
  } else {
    Serial.println(F("OFF."));
  }

  Serial.print(F("Troubleshooting/Demo mode is: "));
  if (eeprom_object.demo_mode) {
    Serial.println(F("ON."));
  } else {
    Serial.println(F("OFF."));
  }

  Serial.println(F("Menu Actions:"));                                                   // Define menu Actions--------------------------------------------
  Serial.println(F("   c  <--  Set clock"));                                            // "99" Set RTC clock
  Serial.println(F("   i  <--  Set ID numbers"));                                       // "105" set IDnum, nodeID, gatewayID
  Serial.println(F("   a  <--  Set Alarm (measurement) Interval"));                     // "97" Set the alarm intervals for the RTC
  Serial.println(F("   g  <--  Toggle Radio Transmissions (ON/OFF)"));                  // "103" Enable or disable LoRa Radio transmisison
  Serial.println(F("   h  <--  Toggle Troubleshooting (continuous) mode"));             // "104" Enter Troubleshooting continuous loop
  Serial.println(F("   b  <--  Identify connected ds18b20 sensors"));                   // "98" menu routine for iteratively connecting temperature sensors and saving them to eeprom
  Serial.println(F("   t  <--  Test measurements"));                                    // "116" take test measurements from sensors -> not currently used
  Serial.println(F("   s  <--  Switch Water Manager"));                                 // "115" Toggle/Switch Water Manager routine
  Serial.println(F("   w  <--  Define water threshold values and times"));              // "119" Define threshold to trigger an event
  Serial.println(F("   n  <--  Specify number of Watermark sensors and Mean groups"));  // "110" Set number of WM sensors installed & specify groups based on channel position of multiplexor? Could also write funciton to subtract 1 from numbers specified so that actual terminal numbers can be used.
  Serial.println(F("   o  <--  Specification of resistors for WM circuit"));            // "111" Define resistor information
  Serial.println(F("   p  <--  Set output HIGH on pinouts 1 to 4"));                    // "112" For priming/testing irrigation equipment
  //  Serial.println(F("   r  <--  Download range of data"));                         // "114" set beginning date to download, not functional
  Serial.println(F("   f  <--  Display microSD card information"));  // "102" Get sdcard information, non functional
  Serial.println(F("   d  <--  Download all data"));                 // "100" get all data to serial port
  Serial.println(F("   e  <--  Erase all data"));                    // "101" erase all data
  //  Serial.println(F("   m  <--  Repeat menu"));                                    // "109" repeat menu, not functional
  Serial.println(F("   x  <--  Exit"));  // "120" exit

  menutimeout = millis() + 60000;  //wait for user input, was 10 sec but 60 sec is better for new users
  while (millis() < menutimeout) {

    menuinput = 120;  //will default to ascii 120 "x"
    if (Serial.available() > 0) {
      menuinput = Serial.read();  //get user input
      while (Serial.available() > 0) {
        Serial.read();
      }
      break;
    }
  }

  switch (menuinput) {  //switch cases for menu input, Note the case# corresponds to input in ASCII format
    case 98:            //"b" for iterating connection of ds18b20 temperature sensors
      Serial.println(F("Iterative identification of ds18b20 temperature sensors."));
      get_integer_input();  //otherwise the first input is always 0?
      Serial.println(F("If you would like to continue, press 1. To return to main menu, press any other character."));
      get_integer_input();
      // do not specify int user_resp = indata; // for some reason it does not allow menu access, define the integer then change it.
      int user_resp;
      delay(100);
      user_resp = indata;
      delay(100);
      if (user_resp != 1) {
        menu();
        break;
      } else {
        Serial.println(F("Continuing with iterative identificaiton of ds18b20 temperature sensors..."));
        delay(1000);
        Serial.println(F("How many ds18b20 are being connected?"));
        get_integer_input();
        eeprom_object.num_ds18b20 = indata;

        if (eeprom_object.num_ds18b20 == 0) {
          Serial.println(F("As 0 external temperature sensors are being connected, temperature correction will be made using the Real Time Clock inside the environmental enclosure."));
          menu();
          break;
        }

        Serial.print(F("User must complete identification of "));
        Serial.print(eeprom_object.num_ds18b20);
        Serial.println(F(" ds18b20 temperature sensors."));
        Serial.println(F("Prepare to plug in sensors..."));
        delay(2000);
        Identify_1WireDevices();
      }
      menu();
      break;

    case 99:  // "c" for set clock-----------------------------------------------

      Serial.println(F("Set clock:  "));
      get_integer_input();  //otherwise the first input is always 0?
      Serial.print(F("  input month: "));
      get_integer_input();
      mnths = indata;
      Serial.print(F("  input day:    "));
      get_integer_input();
      days = indata;
      Serial.print(F("  input year:   "));
      get_integer_input();
      yrs = indata;
      Serial.print(F("  input hour:   "));
      get_integer_input();
      hrs = indata;
      Serial.print(F("  input minute: "));
      get_integer_input();
      mins = indata;
      Serial.print(F(" input second: "));
      get_integer_input();
      secs = indata;

      rtc.adjust(DateTime(yrs, mnths, days, hrs, mins, secs));
      delay(50);

      menu();
      break;

    case 112:               // "p" for prime pumps--------------------------------
      get_integer_input();  //otherwise the first input is always 0?
      Serial.flush();
      Serial.println(F("Are you sure you want to cycle the pumps?"));
      Serial.println(F("Make sure the lines are oriented where outflow is desired!"));
      Serial.println(F("Type YES to confirm priming of pumps"));

      charinput();
      if (charInput[0]) {
        char answer[4]{ 0 };
        byte i = 0;
        for (int i = 0; i < 3; i++) {
          if (charInput[i] != 0) {
            answer[i] = charInput[i];
          }
        }
        Serial.print(F("Answer: "));
        Serial.println(answer);
        if (strcmp(answer, "YES") == 0) {
          Serial.println(F("Priming Pumps..."));

          digitalWrite(in1, HIGH);  //provide power to relay/switch on respective pin, relays are configured active when HIGH. pumps should be normally open & circuit closed when powered
          delay(10000);
          digitalWrite(in1, LOW);  //power off
          delay(2000);
          digitalWrite(in2, HIGH);
          delay(10000);
          digitalWrite(in2, LOW);
          delay(2000);
          digitalWrite(in3, HIGH);
          delay(10000);
          digitalWrite(in3, LOW);
          delay(2000);
          digitalWrite(in4, HIGH);
          delay(10000);
          digitalWrite(in4, LOW);
          Serial.println(F("Pump priming routine completed."));
        } else {
          Serial.println(F("Answer was not YES, pump priming failed to initialize."));
        }
      }
      menu();
      break;

    case 102:  // "f" for checking files on sdcard. Non functional
      Serial.println(F("Getting sdcard information..."));
      sdCheck();
      menu();
      break;

    case 103:  // "g" for enabling or disabling Radio Transmission.
      Serial.println(F("Would you like to enable radio transmisison?"));
      get_integer_input();  //otherwise first is always 0?
      Serial.println(F("Type 1 to enable radio transmissions or 0 to disable radio transmissions."));
      get_integer_input();
      eeprom_object.toggle_radio = indata;

      if (indata != 1) {
        Serial.println(F("Radio transmissions DISABLED."));
        driver.sleep();  //Puts radio to sleep.
      } else {
        Serial.println(F("Radio transmissions ENABLED."));
        RH_RF95 driver(4, 2);
        RHReliableDatagram manager(driver, radioID);  //Set up the radio manager
        if (!driver.init()) {
          Serial.println(F("Radio initialization failed."));
        }
        if (driver.init()) {
          Serial.println(F("Radio initilization successful."));
        }
      }
      menu();
      break;

    case 104:  // "h" for enabling or disabling troubleshooting/demo continuous loop.
      Serial.println(F("Would you like to enter troublesooting mode?"));
      Serial.println(F("Troubleshooting mode does not save data to sdcard but DOES send radio transmissions."));
      Serial.println();
      Serial.println(F("You may also toggle the printing of extra runtime notes to help with troubleshooting."));
      Serial.println(F("This can be done in Troubleshooting mode or during normal operation."));
      get_integer_input();  //otherwise first is always 0?
      delay(2000);

      Serial.println(F("Would you like to enable runtime notes?"));
      Serial.println(F("Enter 1 to enable runtime notes or 0 to disable runtime notes."));
      Serial.print(F("Current runtime notes Setting:  "));
      if (eeprom_object.run_notes) {
        Serial.println(F("ON."));
      } else {
        Serial.println(F("OFF."));
      }
      get_integer_input();

      if (indata == 1 || indata == 0) {
        eeprom_object.run_notes = indata;
      } else {
        Serial.println(F("Invalid Entry, returning to main menu."));
        menu();
        break;
      }
      delay(2000);

      Serial.println(F("If you would like to enable troubleshooting mode enter 1, otherwise enter 0 to enable regular operation."));
      Serial.println(F("Enter any other key to return to the main menu."));
      get_integer_input();

      if (indata == 1 || indata == 0) {
        eeprom_object.demo_mode = indata;
      } else {
        menu();
        break;
      }

      if (eeprom_object.demo_mode) {  //if demo_mode is true (1)...
        Serial.println(F("Troubleshooting mode enabled."));
        delay(1000);
      } else {
        Serial.println(F("Regular operation enabled."));
        delay(1000);
      }
      menu();
      break;

    case 105:  // "i" for set ID numbers-----------------------------------------

      Serial.println(F("Set network ID numbers (ProjectID, BoardID, NodeID, GatewayID):"));  // set ProjectID, BoardIDnum, nodeID, and gatewayID numbers
      get_integer_input();                                                                   //otherwise the first input is always 0?


      Serial.print(F(" Project ID:     "));  // get projectID up to numChars length
      Serial.flush();
      charinput();
      if (charInput[0]) {
        byte i = 0;
        for (int i = 0; i < numChars; i++) {
          if (charInput[i] != 0 || charInput[i] != '\0') {
            eeprom_object.projectID[i] = charInput[i];
          } else {
            eeprom_object.projectID[i] = '\0';
            break;
          }
        }
      }


      Serial.print(F(" Board ID:     "));  // get BoardID
      Serial.flush();
      get_integer_input();  // decode user input
      eeprom_object.IDnum = indata;



      Serial.print(F(" Node ID:  "));  // get nodeID
      Serial.flush();
      get_integer_input();  // decode user input
      eeprom_object.nodeID = indata;


      Serial.print((" Gateway ID:  "));  // get GatewayID
      Serial.flush();
      get_integer_input();  // decode user input
      eeprom_object.gatewayID = indata;

      manager.setThisAddress(eeprom_object.nodeID);  // Set Radio Address

      menu();  // go back to menu
      break;

    case 97:  // "a" for Setting RTC Alarms for measurement interval
      Serial.println(F("Define measurement interval in minutes."));
      Serial.flush();
      get_integer_input();  //otherwise the first input is always 0?
      Serial.println();
      Serial.print(F("Current Measurement Interval (Minutes):  "));
      Serial.println(eeprom_object.ALARM_1_Interval);
      Serial.println();
      get_integer_input();
      eeprom_object.ALARM_1_Interval = indata;
      Serial.print(F("Measurement Interval set to:  "));
      Serial.println(eeprom_object.ALARM_1_Interval);
      delay(20);
      menu();
      break;

    case 115:  // "s" for switch water_manager----------------------------------
      Serial.println(F("Switch Water Manager: "));
      get_integer_input();  // otherwise the first input is always 0?
      Serial.println();
      Serial.println(F("Enable Water Manager?"));
      Serial.print(F("Current Status: "));
      Serial.println(eeprom_object.is_water_manager_on);  // returns 0 for false and 1 for true
      get_integer_input();
      eeprom_object.is_water_manager_on = indata;
      delay(20);
      menu();
      break;

    case 116:  // "t" for test measurements-------------------------------------
      Serial.println(F("Test measurements:"));
      get_integer_input();  //otherwise the first input is always 0?
      Serial.println();
      test_measurements();
      delay(10);
      menu();
      break;

    case 119:                             // "w" for setting water threshold levels and min time between irrigation events and reporting of resistance values
      get_integer_input();                //otherwise the first input is always 0?
      water_management_group_settings();  //void function

      delay(20);
      Serial.println(F("Do you want to include raw resistance values (for all sensors/groups) in the data string? Type 1 for true, Type 0 for false"));  // specify if resistance is desired in data string. true = 1 false = 0
      Serial.println();
      Serial.print(F("Current Value: "));
      Serial.print(eeprom_object.include_resistance);
      get_integer_input();
      eeprom_object.include_resistance = indata;
      menu();
      break;

    case 100:  // "d" for Download all data in sd card----------------------------
      Serial.println(F("Download all data:"));
      delay(100);

      myfile = SD.open(filename);  // open file

      if (myfile) {

        while (myfile.available()) {  // read file and print to Serial COM port, Note this will be slow with alot of data due to chip limitations. A desktop with a chip reader is nearly instantaneous.
          Serial.write(myfile.read());
        }
        myfile.close();

      } else {
        Serial.println(F("Error opening file"));
        // eeprom_object.error_log->sd_struct.open_file_failure = true;
        eeprom_object.error_log.sd_struct.open_file_failure = true;
      }
      delay(10000);  //Give time for user to copy data...
      menu();
      break;

    case 101:  // "e" for erase data on sd card-------------------------------------------
      Serial.println(F("Erase data on sd card..."));
      Serial.print(F("Currently Writing to: "));
      Serial.println(filename);
      Serial.print(F("Do you want to delete: "));
      Serial.print(filename);
      Serial.println(F("? Type YES to confirm deletion of this file."));

      get_integer_input();  //Otherwise first input is always 0?
      Serial.flush();
      charinput();
      if (charInput[0]) {
        char erase[4]{ 0 };
        byte i = 0;
        for (int i = 0; i < 3; i++) {
          if (charInput[i] != 0) {
            erase[i] = charInput[i];
          }
        }
        Serial.print(F("erase:  "));
        Serial.println(erase);
        if (strcmp(erase, "YES") == 0) {
          SD.remove(filename);
          Serial.println(F("Data File deleted"));
          eeprom_object.firstTime = true;  //Return firsttime to true so header is printed when first writing to file
        } else {
          Serial.println(F("If condition not satisfied"));
        }
      }
      menu();
      break;


    case 111:               //"o" for calibration / fixed resistor settings----------------------------
      get_integer_input();  //Otherwise first input is always 0?
      Serial.flush();       //unneeded?
      Serial.println(F("Define calibration and fixed resistor settings..."));
      Serial.println(F("Is a calibration resistor present?, 1 = true OR 0 = false"));
      Serial.print(F("Current value:  "));
      Serial.println(eeprom_object.calibration_resistor_present);
      get_integer_input();
      eeprom_object.calibration_resistor_present = indata;

      Serial.println(F("Muliplexor channel location of the calibration resistor. 0 to 15"));
      Serial.print(F("Current value:  "));
      Serial.println(eeprom_object.cal_resistor_loc);
      get_integer_input();
      eeprom_object.cal_resistor_loc = indata;

      Serial.println(F("Value of the calibration resistor in ohms. A float"));
      Serial.print(F("Current value:  "));
      Serial.println(eeprom_object.cal_resistor_val);
      get_integer_input();
      eeprom_object.cal_resistor_val = indata;

      Serial.println(F("The value of the FIXED resistor in ohms. A float"));
      Serial.print(F("Current value:  "));
      Serial.println(eeprom_object.fixed_resistor_val);
      get_integer_input();
      eeprom_object.fixed_resistor_val = indata;

      menu();
      break;

    case 110:  //"n" for sepecifying number of WM sensors & grouping | 11/18/2021 untested, not sure if will work
      {
        get_integer_input();  //otherwise the first input is always 0?
        Serial.println(F("Specify number of WaterMark sensors and averaging instructions."));
        Serial.println(F("If you would like to continue, type 1. Press any other key to return to main menu."));
        get_integer_input();
        if (indata != 1) {
          delay(1000);
          Serial.println(F("Returning to main menu."));
          menu();
          break;
        } else {
          Serial.println(F("Clearing Existing Grouping Data."));
          for (int z = 0; z < 16; z++) {
            eeprom_object.WM_group1[z] = -1;
            eeprom_object.WM_group2[z] = -1;
            eeprom_object.WM_group3[z] = -1;
            eeprom_object.WM_group4[z] = -1;
          }
        }
        delay(1000);
        Serial.println(F("Proceed with WaterMark sensor specification."));
        Serial.println(F("Number of WaterMark sensors. Must be installed in sequential order 1-16"));
        Serial.print(F("Current value:  "));
        Serial.println(eeprom_object.num_WM);
        get_integer_input();
        eeprom_object.num_WM = indata;

        Serial.println(F("Determine averaging instructions for the WaterMark sensors."));
        Serial.println(F("Four groups of watermark sensors can be defined based on their channel position of the Multiplexor."));
        Serial.println(F("Specify 0 for no averaging routine."));
        Serial.println(F("Note that the water management routine toggles relays based on group averages."));

        Serial.print(F("Number of channels to average, Group1: "));  //This works great! repeat for each group 1-4
        get_integer_input();
        int num_sensors = indata;
        Serial.print(F("Number sensors: "));
        Serial.println(num_sensors);
        int num_sensors_group1 = num_sensors;
        eeprom_object.n_channels_wm_group1 = num_sensors;

        //Attempt a less memory intensive method------
        for (int i = 0; i < num_sensors; i++) {
          if (num_sensors == 0) {
            break;  //If 0 sensors are selected, break out of for loop
          }
          int sensor_num = i + 1;
          Serial.print(F("Specify sensor number "));
          Serial.print(sensor_num);
          Serial.println(F("  in group1: "));
          get_integer_input();
          eeprom_object.WM_group1[i] = indata;
        }

        Serial.print(F("Number of channels to average, Group2: "));
        get_integer_input();
        num_sensors = indata;
        Serial.print(F("Number sensors: "));
        Serial.println(num_sensors);
        int num_sensors_group2 = num_sensors;
        eeprom_object.n_channels_wm_group2 = num_sensors;
        for (int i = 0; i < num_sensors; i++) {
          if (num_sensors == 0) {
            break;
          }
          int sensor_num = i + 1;
          Serial.print(F("Specify sensor number "));
          Serial.print(sensor_num);
          Serial.println(F("  in group2: "));
          get_integer_input();
          eeprom_object.WM_group2[i] = indata;
        }

        Serial.print(F("Number of channels to average, Group3: "));
        get_integer_input();
        num_sensors = indata;
        Serial.print(F("Number sensors: "));
        Serial.println(num_sensors);
        int num_sensors_group3 = num_sensors;
        eeprom_object.n_channels_wm_group3 = num_sensors;
        for (int i = 0; i < num_sensors; i++) {
          if (num_sensors == 0) {
            break;
          }
          int sensor_num = i + 1;
          Serial.print(F("Specify sensor number "));
          Serial.print(sensor_num);
          Serial.println(F("  in group3: "));
          get_integer_input();
          eeprom_object.WM_group3[i] = indata;
        }

        Serial.print(F("Number of channels to average, Group4: "));
        get_integer_input();
        num_sensors = indata;
        Serial.print(F("Number sensors: "));
        Serial.println(num_sensors);
        int num_sensors_group4 = num_sensors;
        eeprom_object.n_channels_wm_group4 = num_sensors;
        for (int i = 0; i < num_sensors; i++) {
          if (num_sensors == 0) {
            break;
          }
          int sensor_num = i + 1;
          Serial.print(F("Specify sensor number "));
          Serial.print(sensor_num);
          Serial.println(F("  in group4: "));
          get_integer_input();
          eeprom_object.WM_group4[i] = indata;
        }

        Serial.println(F("Current Grouping Settings."));
        Serial.print(F("Group1: "));
        for (int i = 0; i < num_sensors_group1; i++) {
          Serial.print(eeprom_object.WM_group1[i]);
          Serial.print(F("  "));
        }
        Serial.println();
        Serial.print(F("Group2: "));
        for (int i = 0; i < num_sensors_group2; i++) {
          Serial.print(eeprom_object.WM_group2[i]);
          Serial.print(F("  "));
        }
        Serial.println();
        Serial.print(F("Group3: "));
        for (int i = 0; i < num_sensors_group3; i++) {
          Serial.print(eeprom_object.WM_group3[i]);
          Serial.print(F("  "));
        }
        Serial.println();
        Serial.print(F("Group4: "));
        for (int i = 0; i < num_sensors_group4; i++) {
          Serial.print(eeprom_object.WM_group4[i]);
          Serial.print(F("  "));
        }
        menu();
        break;
      }
    case 120:  //"x" for Exit ---------------------------------------------------
      Serial.println(F("Exit"));
      Serial.println();
      delay(10);
      break;


    default:  //Define default case, exit menu if no valid user input
      Serial.println(F("Exit"));
      Serial.println();
      delay(10);
      break;
  }
  eeprom_address = 0;                         //clear eeprom_address
  EEPROM.put(eeprom_address, eeprom_object);  //store new settings (eeprom_object with structure eeprom_struct) to chip EEPROM
  eeprom_address = 0;                         //clear eeprom_address
  Set_ALARM_1_Interval();
  delay(100);
}

//-----Get User input as integer-----------------------------------------------
void get_integer_input() {
  menutimeout = millis() + 60000;  // time to wait for user to input something, was 10 secs
  int sign = 1;                    // for handling negatives
  indata = 0;                      // initialize
  while (millis() < menutimeout)   // wait for user to input something
  {
    if (Serial.available() > 0)  // something came in to serial buffer
    {
      delay(100);                         // give time for everything to come in
      numincoming = Serial.available();   // number of incoming bytes (1 byte = 1 HEX (ASCII) character or 8bits of binary)
      for (i = 1; i <= numincoming; i++)  // read in everything
      {
        incoming[i] = Serial.read();                 // read from buffer
        if (incoming[i] == 13 || incoming[i] == 10)  // ignore carriage return & line feed
        {
        } else if (incoming[i] == '-') {                      //if a minus is read in
          sign = -1;                                          //update sign to neagative 1
        } else if (incoming[i] >= '0' && incoming[i] <= '9')  // otherwise
        {
          input = incoming[i] - '0';            // convert ASCII value to ??numerical equivalent?? decimal.
          indata = indata * 10 + input * sign;  // assemble to get total value if sequence of numbers
        }
      }
      break;  // exit before menutimeout
    }
  }
  Serial.println(indata);  //return entered value for visual user feedback
  delay(10);
}

void events_loop(){
  // state is initally IDLE
  curr_state = IDLE;

  reset_events_queue();
  find_upcoming_events();

  // How to handle pulse type vavles with this loop????
  while (eeprom_object.events_queue_size > 0){
    // Timing of the pulse length, not every x ms
    // Check to make sure the valve is closed
    // Maybe attach another component that allows verififcant (set one of 4 output pins as input to verify valve state)
    //maintain_valves_open();
    for (int i = 0; i < eeprom_object.events_queue_size; i++){
      // Create a new function that finds an event by id/reference number and returns the pointer and input into this
      handle_event(get_event_by_id(eeprom_object.events_queue[i]));
    }
    // subfunctions will update num_events and we will keep track of events to remove
    // eventsQueue will be updated after the loop
    // If a function is non blocking it will move on to next iteration of loop
    check_for_singular_events();
    check_for_recurring_events();

    // Could try to error bounds using ms for radio or other precise events
    // Could use testcase and engineer some edge cases and regular cases and see whether the function works
    // Could also be measurement events

  }

  // Standard event on all valves 
  // Start one valve and can start another at same time and later time (e.g. one at noon and another 12:15)
  // Measuring events testcases (measuring event during irrigation period)
  // Radio( dummy function that prints to serial terminal that radio is occuring) transmissions during irrigation
  // Device is on and has to change something, 

  // Could use eeprom OR flash memory
  // RTC_DATA_ARR int events[50]
  // DS3231 RTC

  // More mini examples for that
  // Look into lookup tables and pulse type signaling(one signal to change state or wait for second state to change back for latching)
  // Valve type logic
  
  // Look into state diagrams


  // Start rtc, testcases, start working on state table
  
  //while (num_queue_events > 0):
    // go through events queue and if event is occuring now, call functions to handle based on event_type
    

    // radio functions and some other events may block, but not for long
    // for irrigation, do it nonblocking


}

// Populates eventsQueue with upcoming events
void find_upcoming_events(){
  // Get the current time
  DateTime now = rtc.now();
  
  for (int i = 0; i < eeprom_object.num_events; i++){
    if (events[i]->recurring){
      uint32_t current_unix_epoch_time = now.unixtime();  //get current unix epoch time
      uint32_t num_seconds_in_week = 604800;
      uint32_t shift = 259200;
      uint32_t num_seconds_since_week_start = (current_unix_epoch_time - shift) % num_seconds_in_week;

      uint32_t beginning_seconds_since_week_start = ( events[i]->span[0]->unixtime() - shift) % num_seconds_in_week;
      uint32_t ending_seconds_since_week_start = ( events[i]->span[1]->unixtime() - shift) % num_seconds_in_week;
      if (num_seconds_since_week_start >= beginnging_seconds_since_week_start &&  num_seconds_since_week_start <= ending_seconds_since_week_start){
        // add to events queue
        // might want to add something to current time to find events very shortly in the future to ensure they don't get skipped
        // so probably current_time + some_small_offset
        eeprom_object.events_queue[events_queue_size] = *(events[i]->event_id);
        eeprom_object.events_queue_size++;
      }
    }
    else{
      // *(events[i]->span[0]) 
      // span is a DateTime array, and we have <= comparators we can compare 2 datetime objects
      // Making sure that the time now is in the range of the span
      // With arrow notation, we can access data elements of an objects using it's pointer
      if (now >= *(events[i]->span[0]) && now <= *(events[i]->span[1])){
        eeprom_object.events_queue[events_queue_size] = *(events[i]->event_id);
        eeprom_object.events_queue_size++;
      }
    }
  }
}

void reset_events_queue(){
  for (int i = 0; i < 50; i++){
    eeprom_object.events_queue[i] = -1;
  }
  eeprom_object.events_queue_size = 0;
}

event* get_event_by_id(int id){
  for (int i = 0; i < eeprom_object.num_events; i++){
    if (events[i]->event_id == id){
      return event;
    }
  }
}

// example client on microcontroller, while still connected, stuck in client loop, still process time or other events or no?
// Consider 

// What states can I change to based on current state

void handle_event(event* e){
  switch(e->event_type):
    case 0:
      // Check for radio events, can check state within state
      // example: taking measurement and checking event time
      perform_scheduled_irrigation(e);
    case 4:
      //radio_event(e);

}


//To be placed in menu() -> maybe rename to eventScheduleMenu?
void events_menu() {
  Serial.println(F("Open_Irr Event Scheduler Menu..."));
  Serial.println();

  Serial.println(F("Make a selection."));
  Serial.println(F("1    <-     View Current Schedule"));
  Serial.println(F("2    <-     Schedule New Event"));
  Serial.println(F("3    <-     Remove Event From Schedule"));
  Serial.println(F("4    <-     Remove All Events From Schedule"));
  Serial.println(F("5    <-     Clear Match Fields"));
  Serial.println(F("6    <-     Emergency Clear SD"));

  get_integer_input();  //from existing code using global int "indata"

  delay(100);

  if (indata == 1 || indata == 3) {
    Serial.println(F("Scheduled Events: "));
    //print from timeEvaluation scheduled event Array (DateTime objects) with array indicies
    print_all_events();

    Serial.println(F("Measurement Match Fields: "));
    print_current_match_fields();

    //will also need to print from the oldEvent global instance to show the matchFields for measurement
    // TODO
    //print_measurement_interval_details();
    if (indata == 3) {
      Serial.println(F("Enter the index number of scheduled event to remove."));
      get_integer_input();
      remove_event(indata);
    }
  }
  else if (indata == 2) {
    event_scheduler_menu();
  }
  else if (indata == 4){
    remove_all_events();
  }
  else if (indata == 5){
    clear_match_fields();
  }
  else if (indata == 5){
    clear_sd();
  }
}

void event_scheduler_menu(){
  Serial.println(F("Scheduling New Event..."));
  Serial.println();

  Serial.println(F("Select Event Type."));
  Serial.println(F("0     <-     Sensor Measurement"));
  Serial.println(F("1     <-     Timer-based Irrigation"));
  Serial.println(F("2     <-     Irrigation Permit window"));
  Serial.println(F("3     <-     Irrigation Deny window"));

  //Placeholder for other event types to schedule... Radio transmissions etc. possible...

  get_integer_input();

  int choice = indata;
  if (choice == 0) {
    measurement_event_scheduler_menu();
  }
  else if (choice == 1) {
    timer_based_scheduler_menu();
  }
  else if (choice == 2) {
    permit_deny_event_scheduler_menu();
  }
  // else if (choice == 3){
  //   deny_window_scheduler_menu();
  // }
  //Exit scope of permit/deny window (event type 2)
}

void valve_menu(){
  Serial.println(F("Valve Configuration Menu."));
  Serial.println(F("0     <-     Create a new valve configuration"));
  Serial.println(F("1     <-     Delete an existing valve"));

  get_integer_input();

  int choice = indata;

  if (choice == 0){
    new_valve_creation_menu();
  }
  
}

void valve_creation_menu(){

  // Select valve, give users option to print out header file, option - my valve isn't listed, append new valve
  // Once user specifies new valves, store only new valves in txt file on SD card like with events and predefined valves will be within the header file
  int valve_id, power, num_valve_fittings, inrush_current_amps, holding_current_amps, duty_cycle_type, duty_cycle_ratio, min_pressure_psi, max_pressure_psi, resistance_ohms;
  string model, link;
  bool idle_state_is_closed, return_to_state_logic_exists, has_three_way_wiring; 

  char response[20]{ 0 };
  //byte i = 0;

  Serial.println(F("Enter the valve model:"));
  while (Serial.available() == 0){};
  String model = Serial.readString();

  Serial.println(F("Enter the valve link:"));
  while (Serial.available() == 0){};
  String link = Serial.readString();

  Serial.println(F("Enter the valve power:"));
  get_integer_input();
  power = indata;

  Serial.println(F("Enter the number of valve fittings:"));
  get_integer_input();
  num_valve_fittings = indata;

  Serial.println(F("Indicate whether the valve is closed in the idle state (enter 1 for true and 0 for false):"));
  get_integer_input();
  if (indata == 0){
    idle_state_is_closed = false;
  }
  else{
    idle_state_is_closed = true;
  }

  Serial.println(F("Enter whether the valve returns to regular logic on power loss (enter 1 for true and 0 for false):"));
  get_integer_input();
  if (indata == 0){
    return_to_state_logic_exists = false;
  }
  else{
    return_to_state_logic_exists = true;
  }

  Serial.println(F("Enter the inrush current in Amps:"));
  get_integer_input();
  inrush_current_amps = indata;

  Serial.println(F("Enter the holding current in Amps:"));
  get_integer_input();
  holding_current_amps = indata;

  Serial.println(F("Enter the duty cycle type:"));
  get_integer_input();
  duty_cycle_type = indata;

  Serial.println(F("Enter the duty cycle ratio:"));
  get_integer_input();
  duty_cycle_ratio = indata;

  Serial.println(F("Enter whether there is three way wiring:(Enter 0 for no or 1 for yes)"));
  if (indata == 0){
    has_three_way_wiring = false;
  }
  else{
    has_three_way_wiring = true;
  }

  Serial.println(F("Enter the minimum pressure in PSI:"));
  get_integer_input();
  min_pressure_psi = indata;

  Serial.println(F("Enter the maximum pressure in PSI:"));
  get_integer_input();
  max_pressure_psi = indata;

  Serial.println(F("Enter the resistance in Ohms:"));
  get_integer_input();
  resistance_ohms = indata;

  create_new_valve()
}

void create_new_valve(string model, string link, int power, int num_valve_fittings, bool idle_state_is_closed, bool return_to_state_logic_exists, int inrush_current_amps, int holding_current_amps, int duty_cycle_type, int duty_cycle_ratio, bool wiring_three_way, int min_pressure_psi, int max_pressure_psi, int resistance_ohms){
  eeprom_object.valves[eeprom_object.num_valves] = new valve(eeprom_object.current_valve_id, model, link, power, num_valve_fittings, idle_state_is_closed, return_to_state_logic_exists, inrush_current_amps, holding_current_amsp, duty_cycle_type, duty_cycle_ratio, wiring_three_way, min_pressure_psi, max_pressure_psi, resistance_ohms);
  eeprom_objet.num_valves++;
  eeprom_object.current_valve_id++;
}

void remove_valve(int index){
  delete eeprom_object.valves[index];
  for (int i = index; i < eeprom_object.num_valves - 1; i++){
    eeprom_object.valves[i] = eeprom_object.valves[i+1];
  }
  eeprom_object.valves[eeprom_object.num_valves-1] = nullptr;
  eeprom_object.num_valves--;
}

void remove_all_valves(){
  while (eeprom_object.num_valves > 0){
    remove_valve(eeprom_object.num_valves-1);
  }
  delete[] eeprom_object.valves;
  //SD.remove("events.txt");
}

int evaluateValve(int id){
  /*
  if valve type is normally open:
    hold valve high to close 
  */
}

// bool check_overlap(event_window){
//   // Loop through window vector and check if seconds_from_start_of_week of event_window is between start and end of something in the vector
//   // events array is in eeprom object
//   for (int i = 0; i < eeprom_object.num_events; i++){
//     if (event_window.seconds_interval[0] < eeprom_object.events[i].seconds_interval[1] && event_window.seconds_interval[1] > eeprom_object.events[i].seconds_interval[0]){
//       return true;
//     }
//   }
//   return false;
// }

// void wakeup_routine(){
//   check_for_events();

// }

// 



void measurement_event_scheduler_menu(){
  //Schedule Measurement Event (scheduling the sensor reading interval)
  Serial.println(F("Measurement type events are recurring by default."));

  //To schedule a Measurement, a Datetime struct must be evaluated, so we need to choose a matchField

  Serial.println(F("Select a DateTime Match Field Element for Scheduling Measurements."));
  Serial.println(F("Measurements will occur when DateTime elements pulled from the DS3231 RTC match the scheduled event time."));
  Serial.println(F("For example, to schedule measurements to occur every 15 minutes one would: "));
  Serial.println(F("  1) Select Mintues for the match field."));
  Serial.println(F("  2) Specify 4 match fields."));
  Serial.println(F("  3) Enter match fields one at a time (0,15,30,45)."));
  Serial.println(F("This results in measurements taken when the current time of the RTC has minute values of 0, 15, 30, and 45."));

  Serial.println();
  Serial.println(F("0     <-     Seconds"));
  Serial.println(F("1     <-     Minutes"));
  Serial.println(F("2     <-     Hours"));
  get_integer_input();

  //print_current_match_fields();

  int selectedMatchField = indata;  //0,1,2

  if (indata > 2 || indata < 0) {
    Serial.println(F("Invalid Entry."));
    //menu();
  }

  //user feedback
  if (selectedMatchField == 0) {
    Serial.println(F("Match Seconds Selected."));
  }
  else if (selectedMatchField == 1) {
    Serial.println(F("Match Crap Selected."));
  }
  else if (selectedMatchField == 2) { // saved
    Serial.println(F("Match Hours Selected."));
  }

  Serial.println(F("Enter Number of Match Field Entries to evaluate, 0 to 24."));
  get_integer_input();
  int num_match_fields = indata;

  //arbitrary limit?
  if (num_match_fields > 24 || num_match_fields == 0) {
    Serial.print(F("Invalid Entry."));
    //menu();
  }

  //enter integers as asked
  for (int i = 0; i < num_match_fields; i++) {
    Serial.print(F("Enter Value of Match Field Entry "));
    Serial.println(i + 1);
    get_integer_input();
    eeprom_object.matchFields[selectedMatchField][i] = indata;  //pass in the integer to match on


    // //check existing matchFields for updated values (to not write to eeprom needlessly)
    // if (newEvent.matchFields[i] != oldEvent.matchFields[i]) {
    //   //MatchFields have been updated
    //   updateMeasurementInterval = true;                   //set the global flag true
    //   oldEvent.matchFields[i] = newEvent.matchFields[i];  //pass the newEvent matchFields to the oldEvent matchFields
    // }
    // else {
    //   //No changes to matchFields
    //   //No save needed
    // }

    // TODO
    //Save the changes
    // if (updateMeasurementInterval) {
    //   //save oldEvent.matchFields to eeprom

    //   //Have to add that element to the eeprom struct
    //   eeprom_object.measurementMatchFields[i] = oldEvent.matchFields[i];
    // }
  }
  eeprom_object.numFields[selectedMatchField] = num_match_fields;

  Serial.println(F("Measurement Event Scheduling Completed."));

  //All matchFields Set.
  //Will need to check eeprom_object.measurementMatchFields[i] in timeEvaluation() function
  //Exit this submenu to exit to EEPROM.put(eeprom_address, eeprom_object); in the main menu() function.
}

void timer_based_scheduler_menu(){
  //Timer-based Irrigation Event
  Serial.println(F("Timer-based Irrigation Event Selected."));
  Serial.println();
  Serial.println(F("Timer-based Irrigation Events are scheduled as recurring events every week, unless a singular event is specified."));
  Serial.println(F("To schedule a singular event, enter 0, any other entry defaults to a recurring weekly event."));

  get_integer_input();

  if (indata == 0) {
    singular_event_scheduling_menu(1);
  }
  else {
    recurring_event_scheduler_menu(1);
    // if (indata < 60 && indata >= 0) {
    //   minute = indata;
    // }
    // else {
    //   Serial.println(F("Invalid Entry."));
    //   //menu();
    // }

    // Serial.println(F("Enter Irrigation Event Duration in Minutes"));
    // Serial.println();
    // get_integer_input();

    // if (indata > 0) {
    //   eventDurationMinutes = indata;
    //   //Pass the irrigation event duration somewhere to recall ->  this is currently (type long) eeprom_object.irr_period[4], one for each group...

    //   //may need refactored to store differently
    // }
  }

  Serial.println(F("Timer-based Irrigation Event Scheduling Completed."));
}

void permit_deny_event_scheduler_menu(){
    //Define irrigation permit/deny window
    int event_type;
    Serial.println(F("Irrigation Permit/Deny Windows Selected."));
    Serial.println();

    Serial.println(F("To define a permit window, enter 1, any other entry will result in a deny window."));
    get_integer_input();

    if (indata == 1) {
      Serial.println(F("Permit window selected."));
      event_type = 2;
    }
    else {
      Serial.println(F("Deny window selected."));
      event_type = 3;
    }
    Serial.println();

    Serial.println(F("Is the irrigation event permit/deny window recurring (regularly occuring at set times weekly) or singular?"));
    Serial.println(F("To schedule a singular permit/deny window, enter 0, any other entry defaults to a recurring weekly permit/deny window."));
    get_integer_input();

    if (indata == 0) {
      singular_event_scheduling_menu(event_type);
    }
    else {
      recurring_event_scheduler_menu(event_type);
    }
    Serial.println(F("Recurring Permit/Deny Window Addition Completed."));
}

void singular_event_scheduling_menu(int event_type){
  //Singular Event Schedule: configure start and end dateTime(s), Append the file storing DateTimes for evaluation
  Serial.println(F("Define a new singular event window."));
  Serial.println();
  
  Serial.println(F("Define Start and Stop dates for the new singular event window."));
  Serial.println();

  // May have to look into changing type from int to more specific type
  int years[2];
  int months[2];
  int days[2];
  int hours[2];
  int minutes[2];
  int seconds[2];

  for (int i = 0; i < 2; i++) {
    if (i == 0) {
      Serial.println(F("Select Start date."));
    }
    else {
      Serial.println(F("Select End date."));
    }

    Serial.println(F("Enter Year."));
    get_integer_input();
    years[i] = indata;

    Serial.println(F("Enter Month."));
    get_integer_input();
    months[i] = indata;

    Serial.println(F("Enter Day of Month."));
    get_integer_input();
    days[i] = indata;

    Serial.println(F("Enter Hour of Day, considering a 24 hour clock (0 to 23). "));
    get_integer_input();
    hours[i] = indata;

    Serial.println(F("Enter Minute of Hour (0 to 59)."));
    get_integer_input();
    minutes[i] = indata;

    Serial.println(F("Enter Second of Minute (0 to 59)."));
    get_integer_input();
    seconds[i] = indata; //saved
  }

  DateTime* span[2];
  span[0] = new DateTime(years[0], months[0], days[0], hours[0], minutes[0], seconds[0]);
  span[1] = new DateTime(years[1], months[1], days[1], hours[1], minutes[1], seconds[1]);

  // For debugging
  // Serial.println(F("Start Date:"));
  // Serial.println(span[0]->year());
  // Serial.println(span[0]->month());
  // Serial.println(span[0]->day());
  // Serial.println(span[0]->hour());
  // Serial.println(span[0]->minute());
  // Serial.println(span[0]->second());


  Serial.println(F("How many irrigation zones should consider this new event?"));
  Serial.println();
  get_integer_input();
  int numZones = indata;

  if (numZones >= 5 || numZones <= 0) {
    Serial.println(F("Invalid Entry."));
    //menu();
    return;
  }

  // fixed size for now, but might want to change later
  int zones[500];

  for (int i = 0; i < numZones; i++) {
    Serial.print(F("Enter irrigation zone receiving this new singular event."));
    get_integer_input();
    zones[i] = indata;
    //perhaps store an integer array in eeprom for each irrigation zone containing the index number of DateTime scheduling objects stored in a file?.
    //saved
  }

  for (int i = 0; i < numZones; i++){
    schedule_new_event(zones[i], false, event_type, span);
  }

  Serial.println(F("Singular window addition(s) completed."));
}

void recurring_event_scheduler_menu(int event_type){
  uint8_t day, hour, minute, valve;
  uint16_t eventDurationMinutes;
  //create a new DateTime instance and populate with user provided information, fill the remaining information with the current values of the RTC

  //store that event in the timeEvaluation file

  //poll user for: day of week, hour of day, minute of hour
  //note that year, month, and second do not need to be provided by user and are not used...
  // seconds can be initialized to 0
  Serial.println(F("Define new Recurring Event."));
  Serial.println();
  Serial.println(F("Choose Event Day of Week."));
  Serial.println();
  Serial.println(F("0     <-     Sunday"));
  Serial.println(F("1     <-     Monday"));
  Serial.println(F("2     <-     Tuesday"));
  Serial.println(F("3     <-     Wednesday"));
  Serial.println(F("4     <-     Thursday"));
  Serial.println(F("5     <-     Friday"));
  Serial.println(F("6     <-     Saturday"));

  get_integer_input();

  if (indata < 7 && indata >= 0) {
    day = indata;
  }
  else {
    Serial.println(F("Invalid Entry."));
    //menu();
  }

  Serial.println(F("Enter Irrigation Event hour of day, considering a 24 hour clock (0 to 23)."));
  Serial.println();
  get_integer_input();

  if (indata < 24 && indata >= 0) {
    hour = indata;
  }
  else {
    Serial.println(F("Invalid Entry."));
    //menu();
  }

  Serial.println(F("Enter Event minute of hour (0 to 59)."));
  Serial.println();

  get_integer_input();

  Serial.println(F("Define Start and Stop dates for the new recurring event."));
  Serial.println();
  for (int i = 0; i < 2; i++) {
    if (i == 0) {
      Serial.println(F("Select Start date."));
    } else {
      Serial.println(F("Select End date."));
    }
    Serial.println(F("Enter Year.")); //saved
    get_integer_input();
    //newEvent.eventStartStop[i].year() = indata;

    Serial.println(F("Enter Month."));
    get_integer_input();
    //newEvent.eventStartStop[i].month() = indata;

    Serial.println(F("Enter Day of Month."));
    get_integer_input();
    //newEvent.eventStartStop[i].day() = indata;
  }

  Serial.println(F("How many irrigation zones should receive this new recurring event?"));
  Serial.println();
  get_integer_input();

  int numZones = indata;
  if (numZones < 5 && numZones > 0) {

  } else {
    Serial.println(F("Invalid Entry."));
    //menu();
  }

  for (int i = 0; i < numZones; i++) {
    Serial.println(F("Enter irrigation zone receiving this new recurring event."));
    get_integer_input();
    //perhaps store an integer array in eeprom for each irrigation zone containing the index number of DateTime scheduling objects stored in a file?.
  }

  //For later Evaluation....
  //calculate the number of seconds elapsed since the beginning of the week
  //the DateTime object will still be used to calculate the seconds elapsed since the beginning of that week (implying we can determine the unix time at the end of the week)
  //evaluate this number
}

// Functions for managing events
void schedule_new_event(int group, bool recurring, int event_type, DateTime* span[2]){
  event new_event(group, recurring, event_type, span);
  events[eeprom_object.num_events] = &new_event;
  eeprom_object.num_events++;
  Serial.println(eeprom_object.num_events);
  //global_num_events;
  print_all_events();
}

void remove_event(int index){
  delete events[index];
  for (int i = index; i < eeprom_object.num_events; i++){
    events[i] = events[i+1];
  }
  events[eeprom_object.num_events-1] = nullptr;
  eeprom_object.num_events--;
}

void remove_all_events(){
  while (eeprom_object.num_events > 0){
    remove_event(eeprom_object.num_events-1);
  }
  SD.remove("events.txt");
}

void read_events_data(){
  Serial.println(F("Reading in events data from file."));
  StaticJsonDocument<10000> eventsLog;
  File events_file = SD.open("events.txt", FILE_READ);
  char buf[10000];
  events_file.read(buf, 10000);
  // Serial.println(buf);
  DeserializationError error = deserializeJson(eventsLog, buf);

  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }
  //JsonArray eventsArray = eventsLog["eventsArray"].as<JsonArray>(); //saved

  int i = 0;
  for (JsonObject o: eventsLog["eventsArray"].as<JsonArray>()){
    events[i] = new event();
    events[i]->group = o["group"];
    events[i]->recurring = o["recurring"];
    events[i]->event_type = o["event_type"];
    uint32_t start = o["start_span_seconds"];
    uint32_t end = o["end_span_seconds"];
    events[i]->span[0] = new DateTime(start);
    events[i]->span[1] = new DateTime(end);
    i++;
  }
  // for (int i = 0; i < eeprom_object.num_events; i++){
  //   //Serial.println(eventsLog[i]["group"]); //saved
  //   events[i] = new event();
  //   events[i]->group = eventsArray[i]["group"];
  //   events[i]->recurring = eventsArray[i]["recurring"];
  //   events[i]->event_type = eventsArray[i]["event_type"];
  //   // event["start_span_seconds"] = events[i]->span[0]->unixtime();
  //   // event["end_span_seconds"] = events[i]->span[1]->unixtime();
  // }
}

void write_events_data(){
  StaticJsonDocument<10000> eventsLog;
  JsonArray eventsArray = eventsLog.createNestedArray("eventsArray");
  for (int i = 0; i < eeprom_object.num_events; i++){
    events[i]->print();
    JsonObject event = eventsArray.createNestedObject();
    event["group"] = events[i]->group;
    event["recurring"] = events[i]->recurring;
    event["event_type"] = events[i]->event_type;
    event["start_span_seconds"] = events[i]->span[0]->unixtime();
    event["end_span_seconds"] = events[i]->span[1]->unixtime();
  }
  char json_array[10000];  // char array large enough
  Serial.print(F("Saving new Event as Json..."));
  // Serial.println();
  serializeJson(eventsLog, json_array);  //copy the info in the buffer to the array to use writeFile below
  //serializeJson(eventsLog, Serial);
  // //Serial.println(s);
  //writeFileSD("events.txt", json_array);  //filename limit of 13 chars
  
  SD.remove("events.txt");
  writeFileSD("events.txt", json_array);
  //eventsArray.printTo(Serial);
  //serializeJsonPretty(eventsLog, Serial);
  myfile = SD.open("events.txt", FILE_READ);
  while (myfile.available()) {  // read file and print to Serial COM port, Note this will be slow with alot of data due to chip limitations. A desktop with a chip reader is nearly instantaneous.
    Serial.write(myfile.read());
  }
  myfile.close();
}

void print_all_events(){
  Serial.print(F("There are currently "));
  Serial.print(eeprom_object.num_events);
  Serial.println(F(" events."));
  Serial.println(F("Printing all events."));
  for (int i = 0; i < eeprom_object.num_events; i++){
    events[i]->print();
  }
}

void print_events_by_group(int group){
  for (int i = 0; i < eeprom_object.num_events; i++){
    if (events[i]->group == group){
      events[i]->print();
    }
  }
}

// If an upcoming event is found, return the integer index within the events array, otherwise return -1
void check_for_singular_events(){
  // Get the current time
  DateTime now = rtc.now();                           //needed to get unix time in next line
  // uint32_t current_unix_epoch_time = now.unixtime();  //get current unix epoch time
  Timespan t(8);
  for (int i = 0; i < eeprom_object.num_events; i++){
    if (now + 8 < events[i]->span[1] && now + 8 > events[i]->span[0]){

    }
  }
  
  // Serial.println(current_unix_epoch_time);

  // uint32_t num_seconds_in_week = 604800;
  // uint32_t shift = 259200;
  // uint32_t num_seconds_since_week_start = (current_unix_epoch_time - shift) % num_seconds_in_week;
  // Serial.println(num_seconds_since_week_start);

  // // Loop through the irrigation windows for each group
  // for (int i = 0; i < 4; i++){
  //   for (int j = 0; j < eeprom_object.num_irrigation_events[i]; j++){
  //   if (num_seconds_since_week_start + 8  >= eeprom_object.irrigation_events[i][j].seconds_interval[0] && num_seconds_since_week_start <= eeprom_object.irrigation_events[i][j].seconds_interval[1]){
  //     // Proceed with irrigation
  //     Serial.print(F("Irrigation indicated for group"));
  //     Serial.println(i);
  //     // call test_nonblocking_irrigation or it's equivalent in the node code
  //   }
  //   }
  //}
  
  // See if the current time + 8 lies within a window, since windows are disjoin, it can only be in at most one
  // Windows should be disjoint, it doesn't make sense to have two overlapping windows for the same group, only one thing at once - either irrigating or not
}

void check_for_recurring_events(){
  // Get the current time
  DateTime now = rtc.now();                           //needed to get unix time in next line
  // uint32_t current_unix_epoch_time = now.unixtime();  //get current unix epoch time
  // Serial.println(current_unix_epoch_time);

  // uint32_t num_seconds_in_week = 604800;
  // uint32_t shift = 259200;
  // uint32_t num_seconds_since_week_start = (current_unix_epoch_time - shift) % num_seconds_in_week;
  // Serial.println(num_seconds_since_week_start);

  // // Loop through the irrigation windows for each group
  // for (int i = 0; i < 4; i++){
  //   for (int j = 0; j < eeprom_object.num_irrigation_events[i]; j++){
  //   if (num_seconds_since_week_start + 8  >= eeprom_object.irrigation_events[i][j].seconds_interval[0] && num_seconds_since_week_start <= eeprom_object.irrigation_events[i][j].seconds_interval[1]){
  //     // Proceed with irrigation
  //     Serial.print(F("Irrigation indicated for group"));
  //     Serial.println(i);
  //     // call test_nonblocking_irrigation or it's equivalent in the node code
  //   }
  //   }
  //}
  
  // See if the current time + 8 lies within a window, since windows are disjoin, it can only be in at most one
  // Windows should be disjoint, it doesn't make sense to have two overlapping windows for the same group, only one thing at once - either irrigating or not
}

bool check_for_measurement_events(){
  //Get the current hours, minute, seconds
  //Loop through the elements of the match array and see if anything matches
  Serial.println(F("Checking for upcoming measurement events."));
  DateTime* times[8];
  DateTime currentTime = rtc.now();
  for (int i = 0; i < 8; i++){
    times[i] = new DateTime(currentTime.unixtime() + i);
    //Serial.println(times[i]->timestamp());
  }

  for (int i = 0; i < 8; i++){
    if (check_match_fields(*times[i])){
      // The next measurement occurs in i < 8 seconds, wait that long and then begin measurement
      delay(i*1000);
      //test_nonblocking_irrigation();
      if (check_match_fields(*times[i])){
        Serial.print(F("Match found in "));
        Serial.print(i);
        Serial.println(F(" seconds."));
      }
    }
  }

  // for (int i = 0; i < 3; i++){ //saved
  //   // Might not want to loop up to 24, instead just loop to number of match fields for that specific time unit
  //   for (int j = 0; j < 24; j++){
  //     if (i == 0){
  //       if (matchFields[i][j] == hour){
  //         return true;
  //       }
  //     }
  //     else if (i == 1){
  //       if (matchFields[i][j] == minute){
  //         return true;
  //       }
  //     }
  //     else if (i == 2){
  //       if (matchFields[i][j] == second){ //saved
  //         return true;
  //       }
  //     }
  //   }
  // }
  return false;
}

//Checks if the DateTime matches any of the match fields
bool check_match_fields(DateTime d){
  int currentFields[3];
  currentFields[0] = d.second();
  currentFields[1] = d.minute();
  currentFields[2] = d.hour();

  for (int i = 0; i < 3; i++){
    for (int j = 0; j < eeprom_object.numFields[i]; j++){
      if (eeprom_object.matchFields[i][j] == currentFields[i]){
        Serial.println("Found match, showing comaprison");
        Serial.println(eeprom_object.matchFields[i][j]);
        Serial.println(currentFields[i]);
        return true; // saved
      }
    }
  }
}

void print_current_match_fields(){
  for (int i = 0; i < 3; i++){
    print_match_fields_by_unit_of_time(i);
  }
}

void clear_match_fields(){
  for (int i = 0; i < 3; i++){
    eeprom_object.numFields[i] = 0;
  }
}

// unit of time can be 0, 1, 2 - seconds, minutes, hours
void print_match_fields_by_unit_of_time(int unit_of_time){
  if (unit_of_time == 0){
    Serial.println(F("Printing Match Fields for the Seconds"));
  }
  else if (unit_of_time == 1){
    Serial.println(F("Printing Match Fields for the Minutes"));
  }
  else if (unit_of_time == 2){
    Serial.println(F("Printing Match Fields for the Hours"));
  }
  for (int i = 0; i < eeprom_object.numFields[unit_of_time]; i++){
    Serial.print(eeprom_object.matchFields[unit_of_time][i]);
    Serial.print(F(" "));
  }
  Serial.println("");
}

void clear_sd(){
  SD.remove("events.txt");
}

// Given a pointer to a scheduled irrigation event, this function performs nonblocking irrigation for the specified groups
void perform_scheduled_irrigation(event* e){
  Datetime now = rtc.now();
  if (e->state == IDLE){
    for (int i = 0; i < 4; i++){
      if (groups[i]){
        open_valve(i);
      }
    }



    //e->event_millis = millis();
    // global for each group
    //opened_miliis = millius();
    // have pin_states and change state 
    // when the timer is up, set the pin low and return to default state for each of the valves
    // helper function to pull default state of valve
    // normally closed vs normally open: closed swithc connection made, singal going through,
    // the valve is the barrier, different components control the valve such as sensros or switches
    
    // close swithc is closing circuit, closing switch opens valves
    // switch is normally open (the circuit is normally open) in this case
    // if (groups[0]){
    //   // say x is the pulse time

    //   // On some solenoids, it permits irrigation and some it stops
    //   digitalWrite(in1, HIGH);
    //   // add some valve logic 
    //   group_states[0] = IRRIGATING;
    // }
    // if (groups[1]){
    //   digitalWrite(in1, HIGH);
    //   group_states[1] = IRRIGATING;
    // }
    // if (groups[2]){
    //   digitalWrite(in1, HIGH);
    //   group_states[2] = IRRIGATING;
    // }
    // if (groups[3]){
    //   digitalWrite(in1, HIGH);
    //   group_states[3] = IRRIGATING;
    // }
  }
  else if (e->state == IRRIGATING && now >= e->span[1]){
    // The event is over
    digitalWrite(in1, LOW);
    digitalWrite(in2, LOW);
    digitalWrite(in3, LOW);
    digitalWrite(in4, LOW);

    uint32_t current_unixtime = rtc.now().unixtime();  //reset the time of last irrigation event for that group
    eeprom_object.last_irr_unix_time[WM_group_num - 1] = rtc.now().unixtime();  //reset the time of last irrigation event for that group

    for (int i = 0; i < 4; i++){
      if (groups[i]){
        eeprom_object.last_irr_unix_time[i] = current_unxtime;
        new_irr_event = true;  //set boolean true to update eeprom

        // Maybe change function name to print_local_time
        local_time(eeprom_object.last_irr_unix_time[i]);
        delay(10);

        // Don't know if still want to do this for scheduled events
        // irrigation_prompt_string += 'G';
        // irrigation_prompt_string += i;  //Amend relevant data to string for storage/transmission
        // irrigation_prompt_string += ',';


        // irrigation_prompt_string += WM_group_mean;
        // irrigation_prompt_string += ',';

        // for (int i = 0; i <= numChars; i++) {
        //   if (local_time_irr_update[i] != '\0') {
        //     irrigation_prompt_string += local_time_irr_update[i];  //The global variable keeping track of last irr event
        //   } else {
        //     break;
        //   }
        // }
        // irrigation_prompt_string += ',';
      }
    }  
              
              
              
  }
  
  
}

// Uses valve logic to open valve for specific group
void open_valve(int group){
  // Retreive valve based on group

  // Call open, close normally open latching, non latching based on type
}

// Maybe add another piece of hardware that solely pulses the valves. This piece of hardware can be activated by this function or something
void maintain_valves_open(){
  for (int i = 0; i < 4; i++){
    if (!eeprom_object.valves[i].latching){
      // see if duty cyclce time has passed and need to send anotehr pulse
    }
  }
}


void irrigate(){
  bool test_mode = true;
  for (int i = 0; i < 4; i++){
    group_is_done[i] = false;
  }

  // Group 1 and 4 will have means below the threshold and have times that guarantee irrgiation

  while (!group_is_done[0] || !group_is_done[1] || !group_is_done[2] || !group_is_done[3]){
    WM_irrigation_prompt(1, eeprom_object.group_irr_thresholds[0] - 5, eeprom_object.group_irr_thresholds[0], eeprom_object.last_irr_unix_time[0] - eeprom_object.min_time_btwn_irr[0] * 60, false);
    WM_irrigation_prompt(2, WM_group2_mean, eeprom_object.group_irr_thresholds[1], eeprom_object.last_irr_unix_time[1], false);
    WM_irrigation_prompt(3, WM_group3_mean, eeprom_object.group_irr_thresholds[2], eeprom_object.last_irr_unix_time[2], false);
    WM_irrigation_prompt(4, eeprom_object.group_irr_thresholds[0] - 2, eeprom_object.group_irr_thresholds[3], eeprom_object.last_irr_unix_time[3] - eeprom_object.min_time_btwn_irr[3] * 60, false);
  }

  Serial.println(F("Irrigation done"));
}

//New prompt for the 4 threshold groups of sensors.
uint32_t WM_irrigation_prompt(int WM_group_num, int WM_group_mean, int WM_group_water_threshold, uint32_t last_irr_time_for_group, bool test_mode) {
  if (WM_group_num >= 1 && WM_group_num <= 4){
    if (group_states[WM_group_num-1] == IRRIGATING){
      // The irrigation time has passed
      if (millis() - group_millis[WM_group_num - 1] >= eeprom_object.irr_period[WM_group_num - 1] * 1000){
            Serial.print(F("Group "));
            delay(50);
            Serial.print(WM_group_num);
            delay(50);
            Serial.println(F(" done irrigating."));
            delay(50);
            Serial.print(F("Irr duration in ms: "));
            delay(50);
            long irr_duration_ms = eeprom_object.irr_period[WM_group_num - 1] * 1000;
            Serial.println(irr_duration_ms);
            delay(50);
            if (!test_mode){
              if (latchingValve){
                
              }
              else{
                // Could try 
                if (WM_group_num == 1){
                  digitalWrite(in1, LOW);                                //open the respective relay pin, removing power to the pump
                }
                else if (WM_group_num == 2){
                  digitalWrite(in2, LOW);                                //open the respective relay pin, removing power to the pump
                }
                else if (WM_group_num == 3){
                  digitalWrite(in3, LOW);                                //open the respective relay pin, removing power to the pump
                }
                else{
                  digitalWrite(in4, LOW);                                //open the respective relay pin, removing power to the pump
                }
              }
              
              eeprom_object.last_irr_unix_time[WM_group_num - 1] = rtc.now().unixtime();  //reset the time of last irrigation event for that group
              new_irr_event = true;  //set boolean true to update eeprom

              // Maybe change function name to print_local_time
              local_time(eeprom_object.last_irr_unix_time[0]);
              delay(10);

              irrigation_prompt_string += 'G';
              irrigation_prompt_string += WM_group_num;  //Amend relevant data to string for storage/transmission
              irrigation_prompt_string += ',';
              irrigation_prompt_string += WM_group_mean;
              irrigation_prompt_string += ',';

              for (int i = 0; i <= numChars; i++) {
                if (local_time_irr_update[i] != '\0') {
                  irrigation_prompt_string += local_time_irr_update[i];  //The global variable keeping track of last irr event
                } else {
                  break;
                }
              }
              irrigation_prompt_string += ',';
            }
            else{
              Serial.println("CURRENTLY IN TEST MODE: pipe would have closed now.");
              delay(50);
            }
            group_states[WM_group_num-1] = IDLE;
            group_is_done[WM_group_num-1] = true;

      }
    }
    else {
      if (there is an event){
        start irrigation //saved
      }
      // if sensors indicate the need for a watering event (for each group threshold)-----
      if (WM_group_mean < eeprom_object.group_irr_thresholds[WM_group_num-1]) {
        if (group_states[WM_group_num-1] == IDLE){
          Serial.print(F("Need for watering event indicated for sensor group: "));
          delay(50);
          Serial.print(WM_group_num);
          delay(50);
          Serial.print(F("  with a mean of: "));
          delay(50);
          Serial.println(WM_group_mean);
          delay(50);
        }
        DateTime now = rtc.now();
        uint32_t current_unix_epoch_time = now.unixtime();
                                                                      //get current unix epoch time, THIS IS IN SECONDS!!!
        if (current_unix_epoch_time - last_irr_time_for_group >= (eeprom_object.min_time_btwn_irr[WM_group_num - 1] * 60)) {  //IF the time since last irrigation event is greater than or equal to the minnimum time between irrigation events (minutes*60=seconds) Do not use (minutes*60*1000 = milliseconds) as UNIX time is represented as seconds.
          // 2022/03/22 Note that this will need changed in future if separate timing differences are specified for each group-----
          Serial.println(F("The minimum time since last irrigation event has been exceeded. Proceed with irrigation"));
          delay(50);
          if (test_mode){
            Serial.println(F("CURRENTLY IN TEST MODE: pipe would have opened now."));
            delay(50);
          }
          else{
            if (latchingValve){

            }
            else{
              if (WM_group_num == 1){
                digitalWrite(in1, HIGH);                                   //provide power to pump on relay on respective pin
              }
              else if (WM_group_num == 2){
                digitalWrite(in2, HIGH);                                   //provide power to pump on relay on respective pin
              }
              else if (WM_group_num == 3){
                digitalWrite(in3, HIGH);                                   //provide power to pump on relay on respective pin
              }
              else {
                digitalWrite(in4, HIGH);                                   //provide power to pump on relay on respective pin
              }
            }

            
            Serial.println(F("Pipe opened"));
            delay(100);
            delay(50);
          }
          group_millis[WM_group_num - 1] = millis();
          group_states[WM_group_num-1] = IRRIGATING;
        }
        //add condition to not overwater??
        //irr_count ++;
        //like incrementing irr_count for throwing flag if X events take place in Y time?
        //Then do something? or send flag to gateway?
        else {
          Serial.print(F("Minimum Time between irrigation events not reached for Group: "));  //declare that the minimum time between irrigations has not elapsed for specified group
          delay(50);
          Serial.print(WM_group_num);
          delay(50);
          Serial.print(F("  with a mean of: "));
          delay(50);
          Serial.println(WM_group_mean);
          delay(50);
          group_states[WM_group_num-1] = WAITING;

          if (!test_mode){
            //Report the group #, group mean, and a unix timestamp of the last irrigation event to the irrigation_prompt_string that gets saved etc.
            irrigation_prompt_string += 'G';
            irrigation_prompt_string += WM_group_num;
            irrigation_prompt_string += ',';
            irrigation_prompt_string += WM_group_mean;
            irrigation_prompt_string += ',';
            //different here than in case above
            //if minimum time between irrigations has not been exceeded, return the time of last irrigation event for the group
            local_time(eeprom_object.last_irr_unix_time[WM_group_num]);
            delay(10);

            for (int i = 0; i <= numChars; i++) {
              if (local_time_irr_update[i] != '\0') {
                irrigation_prompt_string += local_time_irr_update[i];  //The global variable keeping track of last irr event
              } else {
                break;
              }
            }
            irrigation_prompt_string += ',';
          }
          group_is_done[WM_group_num-1] = true;          
        }
      }
      else {
        Serial.print(F("Group: "));
        delay(50);
        Serial.print(WM_group_num);
        delay(50);
        Serial.print(F("  Mean: "));
        delay(50);
        Serial.print(WM_group_mean);
        delay(50);
        Serial.print(F(", Threshold water content of "));
        delay(50);
        Serial.print(eeprom_object.group_irr_thresholds[0]);
        delay(50);
        Serial.println(F("  has not been exceeded."));
        delay(50);
        if (!test_mode){
          //New print routine to add to the irrigation_prompt_string even when water threshold has not been reached
          //and min time has not elapsed
          irrigation_prompt_string += 'G';           //This is looped through for each group
          irrigation_prompt_string += WM_group_num;  //Amend relevant data to string for storage/transmission
          irrigation_prompt_string += ',';
          irrigation_prompt_string += WM_group_mean;
          irrigation_prompt_string += ',';

          local_time(eeprom_object.last_irr_unix_time[WM_group_num-1]);
          delay(10);

          for (int i = 0; i <= numChars; i++) {
            if (local_time_irr_update[i] != '\0') {
              irrigation_prompt_string += local_time_irr_update[i];  //The global variable keeping track of last irr event
            } else {
              break;
            }
          }

          irrigation_prompt_string += ',';
        }
        group_is_done[WM_group_num-1] = true;
      }
    }
  }
  else {
    Serial.println(F("Undefined Group number..."));
    delay(50);
  }  
}

//[10:51 AM] Bierer, Andrew - REE-ARS
//template for thought process
void somethingWorkingPlease(){
 
  //Read from SD card and have json file in a dynamically allocated buffer to pull from using dot access
    //one sub function
 
  //Read our large structure from EEPROM (can be done before SD card i dont think it matters)
    //one sub function
 
//one function
  //Check if it is time to do something
    //Iteritively deal with each "event-reference #" in the SD card file
      //->Checking the clock and using the "timeEvaluation" function with matchfields.
        //For ANY  of the event-references in the sd-card file, is it time to do something - or wait to do something that is scheduled before next wake?
          //No -> go to short sleep
          //Yes -> Okay, which # is this? (lets call it "activeEvent") read eeprom information for
 
          // int equal to active Event
 
 
 
         /* struct valveEventSchedule{
 uint8_t outputValve[4]; //Array to hold valve number
 uint8_t valveEventReference[40]; //Array to hold the event reference(s) - a static declaration of 10 references to consider? would be hard to do this dynamically with a limited and fixed eeprom size.
//output pin being set high
} eventReference;
*/
 
//One function
        //Evaluate the sd-card event-reference # (that it is time to do something) against the stored eeprom event-reference
          //For valves
            //i = 0-4 for valves
            //For valve event-references (ten array positions for each valve)
              //if j == activeEvent
                // Do the something that is being scheduled
                  //sub-function to do that something...
}
 
