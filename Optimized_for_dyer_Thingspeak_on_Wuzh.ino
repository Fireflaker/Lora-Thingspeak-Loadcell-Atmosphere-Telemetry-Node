/*
  Note to self:
  Got:
  SCD40: T, H, CO2;
  SHT40: T, H;  (better)
  BMP: T, Pressure;
  AHT20: T,H   (ok, can ditch data)
  ADS: Gas, Ammonia, Alc, Motion
  Total: 13 channels

  Merge temp 10
  Merge H: 8

  When gas sensors off:
  Total = 10
  Imterpolate motion onto a channel, total = 9

  Lastly:
  interpoate all
  have 2x8 = 16 effective
  Page 1, 2 for each, isolate using spatial differance
  13 total channel plus rssi = 14
  leaves 2 full and 6 paged channels; one of these paged channels shall be optimized for motion, and 3 optimized for gas sensors off
  1. CO2 full,
  2. SHT40 Temp full
  3. SHT40 Humi Optimized with motion (motion on => flip data to negative)
  4,5. SCD40 the co2 sensor: T, H optimized with -Gas, -ammonia.
  6. BMP Pressure optimized with -Alc
  7. AHT mix T and -H
  8. BMP T mix with -RSSI

  No. Use two channels and two keys


  TIMESCHEDULE:

  Lora individually max once per 5 seconds
  Thingspeak every 10 seconds
  Serial as fast as possible

*/

// use millis. No delay anywhere.
const unsigned long eventTime_read_co2 = 99999;
const unsigned long eventTime_read_heated_sensors = 99999;  //must be bigger than heatingDuration
const unsigned long eventTime_read_fast_sensors = 100;

const unsigned long eventTime_push_serial_usb = 100;
const unsigned long eventTime_push_serial_lora = 10000;
const unsigned long eventTime_push_thingSpeak = 15000;

const unsigned long eventTime_watchDog = 300000;//5 minutes
const unsigned long previousTime_watchDog = 0;

unsigned long previousTime_read_co2 = 0;
unsigned long previouTime_read_heated_sensors = 0;
unsigned long previousTime_read_fast_sensors = 0;

unsigned long previousTime_push_serial_usb = 0;
unsigned long previousTime_push_serial_lora = 0;
unsigned long previousTime_push_thingSpeak = 0;

const unsigned long heatingDuration = 5000;  // 5 seconds
unsigned long heatingStartTime = 0;
int8_t heatingProgress = 0;  // states. 0 not heating, 1 heating, 10 ready to read.

// END millis

// init telemetry values

float weight1 = 888; // Weight from the first HX711 module
float weight2 = 888; // Weight from the second HX711 module

float SCDt = 888;
float SCDh = 888;
int16_t SCDco2 = 888;

float SHTt = 888;
float SHTh = 888;

float BMPt = 888;
float BMPpres = 888;

float AHTt = 888;
float AHTh = 888;

int16_t ADSgas = 888;
int16_t ADSammonia = 888;
int16_t ADSalc = 888;
int16_t ADSmotion = 888;

int16_t rssi = 888;

int watchdogHit = 0;
long counter = 0;  // 19 digits int
// END init telemetry

#include "esp_wifi.h"

#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_SHT4x.h>
#include <HX711.h>
#include "SparkFun_SCD4x_Arduino_Library.h"

#include "WiFi.h"
// #include "secrets.h"   //all spelled here. no need seperate page
#include "ThingSpeak.h"  // always include thingspeak header file after other header files and custom macros

#define AHT20_ADDR 0x38   // AHT20 I2C address
#define BMP280_ADDR 0x76  // BMP280 I2C address
#define SHT40_ADDR 0x44   // SHT40 I2C address

#define DOUT_PIN 7         // HX711 data pin
#define SCK_PIN 16          // HX711 clock pin
#define DOUT_PIN2 6         // Second HX711 data pin
#define SCK_PIN2 15          // Second HX711 clock pin
#define ADS1115_ADDR 0x48  // ADS1115 I2C address

#define MOS_PIN_GAS 47      // pins mosfet n low draw
#define MOS_PIN_AMMONIA 48  // pins mosfet n high draw
#define MOS_PIN_ALC 38      // pins mosfet n mid draw
// https://www.atomic14.com/2023/11/21/esp32-s3-pins.html

HX711 scale;
HX711 scale2; // Second scale

SCD4x Co2Sensor;
Adafruit_AHTX0 aht20;
Adafruit_BMP280 bmp280;
Adafruit_SHT4x sht4;
Adafruit_ADS1115 ads;

// WiFi and thingspeak settings
const char *ssid = "pards";
const char *pass = "nah";
int keyIndex = 0;                          // your network key Index number (needed only for WEP)
unsigned long myChannelNumber1 = 2398560;  // change the later messages too
const char *myWriteAPIKey1 = "A5U0PU8DRIHS1OCT";
const char *myReadAPIKey1 = "Y7TG41H194A9B06Z";

unsigned long myChannelNumber2 = 000;  // change the later messages too
const char *myWriteAPIKey2 = "xxx";
const char *myReadAPIKey2 = "xxx";
// const char* server = "api.thingspeak.com";
// int port = 80; //const, but cant

// END WiFi and thingspeak settings

WiFiClient client;

void setup() {

  // Set custom MAC address
  uint8_t newMac[] = {0x02, 0xD1, 0xE2, 0xB2, 0xA0, 0x02};
  esp_wifi_set_mac(WIFI_IF_STA, newMac); // This function is part of the ESP-IDF, might need adjustments for Arduino

  // Start WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin("pards", ""); // Assuming open network, so no password is provided

  pinMode(MOS_PIN_GAS, OUTPUT);
  pinMode(MOS_PIN_AMMONIA, OUTPUT);
  pinMode(MOS_PIN_ALC, OUTPUT);

  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, 5, 4);

  Serial.println("Connecting to WiFi...");

  //  while (WiFi.status() != WL_CONNECTED) {
  //    delay(500);
  //    Serial.print(".");
  //  }

  delay(500);
  Serial.print(".");
  delay(500);
  Serial.print(".");
  delay(500);
  Serial.print(".");
  delay(500);
  Serial.print(".");

  Serial.println("WiFi hopefully connected - non blocking");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  WiFi.mode(WIFI_STA);
  ThingSpeak.begin(client);  // Initialize ThingSpeak
  Wire.begin();

  if (!aht20.begin()) {
    Serial.println("Could not find AHT20 sensor, check wiring!");
  }

  if (!bmp280.begin()) {
    Serial.println("Could not find BMP280 sensor, check wiring!");
  }

  if (!sht4.begin()) {
    Serial.println("Could not find SHT4x sensor, check wiring!");
  }

  if (!Co2Sensor.begin()) {
    Serial.println(F("CO2 Sensor not detected. Please check wiring. Freezing..."));
  }

  scale.begin(DOUT_PIN, SCK_PIN);
  scale2.begin(DOUT_PIN2, SCK_PIN2);

  ads.begin(ADS1115_ADDR);

  // Connect to WiFi
  connectToWiFi();

  Serial.println("check status at https://api.thingspeak.com/channels/2397030/status");

  //preheat(millis());
  //delay(4500);  // wait for co2 sensor first datapoint
}

void loop() {

  unsigned long currentTime = millis();  // shared for all timed events millis, must update frequently

  if (currentTime - previousTime_read_co2 >= eventTime_read_co2) {
    // Your code for read_co2 event here

    if (Co2Sensor.readMeasurement()) {
      Serial.println();
      SCDco2 = Co2Sensor.getCO2();
      SCDt = Co2Sensor.getTemperature();
      SCDh = Co2Sensor.getHumidity();
      Serial.print(F("CO2(ppm): "));
      Serial.print(SCDco2);
      Serial.print(", Temperature(C): ");
      Serial.print(SCDt, 1);
      Serial.print(", Humidity(%RH): ");
      Serial.println(SCDh, 1);
    } else {
      Serial.println("Error: CO2 read too fast");
    }

    previousTime_read_co2 = currentTime;
  }

  //  if (currentTime - previouTime_read_heated_sensors >= eventTime_read_heated_sensors || heatingProgress > 0) {
  //    // Your code for read_heated_sensors event here
  //
  //    preheat(currentTime);
  //    if (heatingProgress == 10) {
  //      // Read and print ADS1115 ADC values
  //      ADSgas = ads.readADC_SingleEnded(0);
  //      ADSammonia = ads.readADC_SingleEnded(1);
  //      ADSalc = ads.readADC_SingleEnded(2);
  //      ADSmotion = ads.readADC_SingleEnded(3);
  //
  //      Serial.print("ADS1115 - A0 CO & Combustible Gas: ");
  //      Serial.print(ADSgas);
  //      Serial.print(", A1 MQ135 ammonia & sulfide: ");
  //      Serial.print(ADSammonia);
  //      Serial.print(", A2 MQ3 alcohol: ");
  //      Serial.print(ADSalc);
  //      Serial.print(", A3 motion: ");
  //      Serial.println(ADSmotion);
  //
  //      cooldown();
  //    }


  //    previouTime_read_heated_sensors = currentTime;  // wait next init heat and read sequence
  //  }

  if (currentTime - previousTime_read_fast_sensors >= eventTime_read_fast_sensors) {
    // Your code for read_fast_sensors event here
    rssi = WiFi.RSSI();
    counter++;

    // Read and print AHT20 sensor data
    sensors_event_t humidity, temp;
    aht20.getEvent(&humidity, &temp);
    AHTt = temp.temperature;
    AHTh = humidity.relative_humidity;

    // Read and print BMP280 sensor data
    BMPt = bmp280.readTemperature();
    BMPpres = bmp280.readPressure() / 100.0F;

    //    // Read and print SHT4x sensor data
    //    sensors_event_t humiditySHT4x, tempSHT4x;
    //    sht4.getEvent(&humiditySHT4x, &tempSHT4x);
    //
    //    SHTt = tempSHT4x.temperature;
    //    SHTh = humiditySHT4x.relative_humidity;

    weight1 = scale.get_units(5); // Adjust the number of samples as needed
    weight2 = scale2.get_units(5); // Adjust the number of samples as needed

    // Remember to also print these new values or send them wherever needed
    Serial.print("Weight1: ");
    Serial.println(weight1, 2);
    Serial.print("Weight2: ");
    Serial.println(weight2, 2);

    Serial.print("AHT20 - Temperature: ");
    Serial.print(AHTt);
    Serial.print(", Humidity: ");
    Serial.print(AHTh);
    Serial.println("%, ");

    Serial.print("BMP280 - Temperature: ");
    Serial.print(BMPt);
    Serial.print(", Pressure: ");
    Serial.print(BMPpres);
    Serial.println("hPa, ");

    //    Serial.print("SHT4x - Temperature: ");
    //    Serial.print(SHTt);
    //    Serial.print(", Humidity: ");
    //    Serial.print(SHTh);
    //    Serial.println("%, ");

    previousTime_read_fast_sensors = currentTime;
  }

  if (currentTime - previousTime_push_serial_usb >= eventTime_push_serial_usb) {
    // Your code for push_serial_usb event here

    Serial.print(weight1);
    Serial.print(", ");
    Serial.print(weight2);
    Serial.print(", ");

    //    Serial.print(SHTt);
    //    Serial.print(", ");
    //    Serial.print(SHTh);
    //    Serial.print(", ");
    Serial.print(BMPt);
    Serial.print(", ");
    Serial.print(BMPpres);
    Serial.print(", ");
    Serial.print(AHTt);
    Serial.print(", ");
    Serial.print(AHTh);
    Serial.print(", ");
    Serial.print(rssi);
    Serial.print(", ");
    Serial.println(counter);


    previousTime_push_serial_usb = currentTime;
  }

  if (currentTime - previousTime_push_serial_lora >= eventTime_push_serial_lora) {
    // Your code for push_serial_lora event here
    Serial2.print("ADS1115 - A0: ");
    Serial2.print(weight1);
    Serial2.print(", A1: ");
    Serial2.print(weight2);
    Serial2.print(", A2: ");
    Serial2.print(ADSalc);
    Serial2.print(", A3: ");
    Serial2.println(ADSmotion);

    Serial2.print("AHT20 - Temp: ");
    Serial2.print(AHTt);
    Serial2.print(", Hum: ");
    Serial2.print(AHTh);
    Serial2.print("%, ");


    Serial2.print("BMP280 - Temp: ");
    Serial2.print(BMPt);
    Serial2.print(", Prs: ");
    Serial2.print(BMPpres);
    Serial2.print("hPa, ");

    Serial2.print("SHT4x - Temp: ");
    Serial2.print(SHTt);
    Serial2.print(", Hum: ");
    Serial2.print(SHTh);
    Serial2.print("%, ");
    previousTime_push_serial_lora = currentTime;
  }

  if (currentTime - previousTime_push_thingSpeak >= eventTime_push_thingSpeak) {
    // Your code for push_thingSpeak event here
    // Connect to WiFi and send telemetry to Rainmaker
    if (WiFi.status() == WL_CONNECTED) {
      sendTelemetry();
    }

    client.stop();
    previousTime_push_thingSpeak = currentTime;
  }

  if (currentTime - previousTime_watchDog >= eventTime_watchDog) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Watchdog: WIFI NOT CONNECTED!");
      Serial.println("Watchdog Hit = "+watchdogHit);

      connectToWiFi();
      watchdogHit++;
    }
    if (watchdogHit > 3) {
      ESP.restart ();
    }
  }

}

void connectToWiFi() {
  // Connect or reconnect to WiFi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.begin(ssid, pass);  // Connect to WPA/WPA2 network. Change this line if using open or WEP network
      Serial.print("Trying Connect - non blocking");
      delay(2000);
    }
    Serial.println("\n Hopefully Connected.");
  }
}

/////////////////////////////////////////////////////////////////////////////////
// WIFI//
/////////////////////////////////////////////////////////////////////////////////
// Initialize our values
String myStatus1 = "Main defult";
String myStatus2 = "Sub defult";
// bool flipper = false;

void sendTelemetry() {

  // MAIN:
  //  set the fields with the values
  ThingSpeak.setField(1, weight1);
  ThingSpeak.setField(2, weight2);
  ThingSpeak.setField(3, (weight1 + weight2));
  ThingSpeak.setField(4, SHTt);
  ThingSpeak.setField(5, SHTh);
  ThingSpeak.setField(6, BMPt);
  ThingSpeak.setField(7, BMPpres);
  ThingSpeak.setField(8, rssi);

  // figure out the status message
  // if (number1 > number2) {
  //   myStatus = String("field1 is greater than field2");
  // } else if (number1 < number2) {
  //   myStatus = String("field1 is less than field2");
  // } else {
  //   myStatus = String("field1 equals field2");
  // }

  // set the status
  ThingSpeak.setStatus(myStatus1);

  // write to the ThingSpeak channel
  int MainCode = ThingSpeak.writeFields(myChannelNumber1, myWriteAPIKey1);
  if (MainCode == 200) {
    Serial.println("Main Channel update successful.");
  } else {
    Serial.println("Problem updating Main channel. HTTP error code " + String(MainCode));
  }

  delay(0);  // Wait 0 seconds to update the next channel

  // SUB:
  //  set the fields with the values
  //  ThingSpeak.setField(1, AHTt);
  //  ThingSpeak.setField(2, AHTh);
  //  ThingSpeak.setField(3, ADSgas);
  //  ThingSpeak.setField(4, ADSammonia);
  //  ThingSpeak.setField(5, ADSalc);
  //  ThingSpeak.setField(6, ADSmotion);
  //  ThingSpeak.setField(7, counter);
  //  ThingSpeak.setField(8, rssi);
  //
  //  // myStatus = String("field1 equals field2");
  //
  //  // set the status
  //  ThingSpeak.setStatus(myStatus2);
  //
  //  // write to the ThingSpeak channel
  //  int SubCode = ThingSpeak.writeFields(myChannelNumber2, myWriteAPIKey2);
  //  if (SubCode == 200) {
  //    Serial.println("Sub Channel update successful.");
  //  } else {
  //    Serial.println("Problem updating Sub channel. HTTP error code " + String(SubCode));
  //  }

  delay(0);
}

//void preheat(unsigned long currentTime2) {
//  if (heatingProgress == 0) {
//    Serial.println("Starting heating sensors");
//    digitalWrite(MOS_PIN_AMMONIA, HIGH);
//    digitalWrite(MOS_PIN_ALC, HIGH);
//    digitalWrite(MOS_PIN_GAS, HIGH);
//    heatingProgress = 1;
//    heatingStartTime = currentTime2;
//  } else if (currentTime2 - heatingStartTime >= heatingDuration) {  //optimize by currentTime2 MANDITORY to avoid OVERFLOW
//    Serial.println("Heating sensors complete");
//    heatingProgress = 10;  //READY TO READ
//    digitalWrite(MOS_PIN_AMMONIA, LOW);
//    digitalWrite(MOS_PIN_ALC, LOW);
//    digitalWrite(MOS_PIN_GAS, LOW);
//  }
//}

//void cooldown() {
//  Serial.println("cooling sensors");
//  digitalWrite(MOS_PIN_AMMONIA, LOW);
//  digitalWrite(MOS_PIN_ALC, LOW);
//  digitalWrite(MOS_PIN_GAS, LOW);
//  heatingProgress = 0;
//}
