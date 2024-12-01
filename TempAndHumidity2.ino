#include <Arduino.h>
#include <Wire.h>
#include <floatToString.h>
#include <SPI.h>
#include "Adafruit_GFX.h"
#include "Adafruit_HX8357.h"
//#include <Adafruit_AHTX0.h>
#include <Adafruit_SHTC3.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
// If using the rev 1 with STMPE resistive touch screen controller uncomment this line:
//#include <Adafruit_STMPE610.h>
// If using the rev 2 with TSC2007, uncomment this line:
#include <Adafruit_TSC2007.h>
#include "secrets.h"

#ifdef ESP8266
   #define STMPE_CS 16
   #define TFT_CS   0
   #define TFT_DC   15
   #define SD_CS    2
#elif defined(ESP32) && !defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S2) && !defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3)
   #define STMPE_CS 32
   #define TFT_CS   15
   #define TFT_DC   33
   #define SD_CS    14
#elif defined(TEENSYDUINO)
   #define TFT_DC   10
   #define TFT_CS   4
   #define STMPE_CS 3
   #define SD_CS    8
#elif defined(ARDUINO_STM32_FEATHER)
   #define TFT_DC   PB4
   #define TFT_CS   PA15
   #define STMPE_CS PC7
   #define SD_CS    PC5
#elif defined(ARDUINO_NRF52832_FEATHER)  /* BSP 0.6.5 and higher! */
   #define TFT_DC   11
   #define TFT_CS   31
   #define STMPE_CS 30
   #define SD_CS    27
#elif defined(ARDUINO_MAX32620FTHR) || defined(ARDUINO_MAX32630FTHR)
   #define TFT_DC   P5_4
   #define TFT_CS   P5_3
   #define STMPE_CS P3_3
   #define SD_CS    P3_2
#else
    // Anything else, defaults!
   #define STMPE_CS 6
   #define TFT_CS   9
   #define TFT_DC   10
   #define SD_CS    5
#endif

#define TFT_RST -1

#if defined(_ADAFRUIT_STMPE610H_)
  Adafruit_STMPE610 ts = Adafruit_STMPE610(STMPE_CS);
#elif defined(_ADAFRUIT_TSC2007_H)
  // If you're using the TSC2007 there is no CS pin needed, so instead its an IRQ!
  #define TSC_IRQ STMPE_CS
  Adafruit_TSC2007 ts = Adafruit_TSC2007();             // newer rev 2 touch contoller
#else
  #error("You must have either STMPE or TSC2007 headers included!")
#endif

// This is calibration data for the raw touch data to the screen coordinates
// For STMPE811/STMPE610
#define STMPE_TS_MINX 3800
#define STMPE_TS_MAXX 100
#define STMPE_TS_MINY 100
#define STMPE_TS_MAXY 3750
// For TSC2007
#define TSC_TS_MINX 300
#define TSC_TS_MAXX 3800
#define TSC_TS_MINY 185
#define TSC_TS_MAXY 3700
// we will assign the calibration values on init
int16_t min_x, max_x, min_y, max_y;


bool inside = false;

// DEFINE THESE IN s"ecrets.h"
/*
// WiFi constants
const char* ssid     = "booga";
const char* password = "wooga";

// API constants
const char* api_endpoint = "https://api.openweathermap.org/data/2.5/weather?id=YOUR_CITY_ID&units=imperial&appid=YOUR_API_KEY";
*/

#define TFT_RST -1
const unsigned long update_millis = 60 * 1000; // 1 minute 
unsigned long lastUpdateMillis = 0;
char sDisp[10];


Adafruit_HX8357 tft = Adafruit_HX8357(TFT_CS, TFT_DC, TFT_RST);

int16_t width = 0;
int16_t height = 0;
#define BLUE_MAX 64
#define CYAN_MAX 68
#define GREEN_MAX 76
#define YELLOW_MAX 85

const uint16_t thermal_bottom = 50.0;
const uint16_t thermal_top = 100.0;

const uint16_t winter_bottom = 10.0;
const uint16_t winter_top = 60.0;

const uint16_t bar_pad = 10;
const uint16_t inner_bar_pad = 2;

Adafruit_SHTC3 sht;

bool setupTouch() {

  Serial.println("HX8357D Featherwing touch test!"); 
  
#if defined(_ADAFRUIT_STMPE610H_)
  if (!ts.begin()) {
    Serial.println("Couldn't start STMPE touchscreen controller");
    return false
  }
  min_x = STMPE_TS_MINX; max_x = STMPE_TS_MAXX;
  min_y = STMPE_TS_MINY; max_y = STMPE_TS_MAXY;
#else
  if (! ts.begin(0x48, &Wire)) {
    Serial.println("Couldn't start TSC2007 touchscreen controller");
    return false;
  }
  min_x = TSC_TS_MINX; max_x = TSC_TS_MAXX;
  min_y = TSC_TS_MINY; max_y = TSC_TS_MAXY;
  pinMode(TSC_IRQ, INPUT);
#endif

  Serial.println("Touchscreen started");
  return true;
 
}

void setupDisplay() {
  tft.begin();

  // read diagnostics (optional but can help debug problems)
  uint8_t x = tft.readcommand8(HX8357_RDPOWMODE);
  Serial.print("Display Power Mode: 0x"); Serial.println(x, HEX);
  x = tft.readcommand8(HX8357_RDMADCTL);
  Serial.print("MADCTL Mode: 0x"); Serial.println(x, HEX);
  x = tft.readcommand8(HX8357_RDCOLMOD);
  Serial.print("Pixel Format: 0x"); Serial.println(x, HEX);
  x = tft.readcommand8(HX8357_RDDIM);
  Serial.print("Image Format: 0x"); Serial.println(x, HEX);
  x = tft.readcommand8(HX8357_RDDSDR);
  Serial.print("Self Diagnostic: 0x"); Serial.println(x, HEX); 
  
  Serial.println(F("Benchmark                Time (microseconds)"));

  tft.setRotation(1);

  width = tft.width();
  height = tft.height();
}

void setupWiFi() {
  // We start by connecting to a WiFi network
  displayMessage(String("Connecting to ") + String(ssid));
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
      delay(500);
  }

  displayMessage(String("WiFi connected to IP address: ") + WiFi.localIP().toString());
  delay(1000);
}

void setup() {
  Serial.begin(115200);
  Serial.println("STARTING SENSOR...");

  //if (!aht.begin()) {
  if (!sht.begin()) {  
    Serial.println("Couldn't find sensor!");
    while (1);
  }

  setupDisplay();
  setupWiFi();
  if(!setupTouch()) {
    displayMessage("Touchscreen not working, defaulting to outside weather only");
    inside = false;
  }
}

float CtoF(float cTemp) {
  return cTemp * 1.8 + 32;
}

int16_t getTempShowLength(float temp, int16_t outside_len) {
  uint16_t bottom = thermal_bottom;
  uint16_t top = thermal_top;

  if(!inside && temp <= thermal_bottom) {
    bottom = winter_bottom;
    top = winter_top;
  } 
  
  if(temp < bottom) {
    temp = bottom;
  }

  if(temp > top) {
    temp = top;
  }

  return ((temp - bottom) / (top - bottom)) * outside_len;
}

int16_t getHumidShowLength(float humid, int16_t outside_len) {

  return (humid / 100.0) * outside_len;
}

uint16_t getThermalColor(float temp) {
  if(temp <= BLUE_MAX) {
    return HX8357_BLUE;
  }

  if(temp <= CYAN_MAX) {
    return HX8357_CYAN;
  }

  if(temp <= GREEN_MAX) {
    return HX8357_GREEN;
  }

   if(temp <= YELLOW_MAX) {
    return HX8357_YELLOW;
  }

  return HX8357_RED;  
}

void drawThermometer(float temp) {
  int16_t start_x = bar_pad;
  int16_t start_y = height * 0.2 + inner_bar_pad;
  int16_t start_w = width - (2 * bar_pad);
  int16_t start_h = height * 0.2 - 4;

  // Outside
  tft.drawRect(start_x, start_y, start_w, start_h, HX8357_WHITE); 
  // Inside
  tft.fillRect(start_x + inner_bar_pad, start_y + inner_bar_pad,  getTempShowLength(temp, start_w - (2 * inner_bar_pad)), start_h - (2 * inner_bar_pad), getThermalColor(temp));
}

void drawHygrometer(float hum) {
  int16_t start_x = bar_pad;
  int16_t start_y = height * 0.8 + inner_bar_pad;
  int16_t start_w = width - (2 * bar_pad);
  int16_t start_h = height * 0.2 - 4;

  // Outside
  tft.drawRect(start_x, start_y, start_w, start_h, HX8357_WHITE); 
  // Inside
  tft.fillRect(start_x + inner_bar_pad, start_y + inner_bar_pad,  getHumidShowLength(hum, start_w - (2 * inner_bar_pad)), start_h - (2 * inner_bar_pad), HX8357_BLUE);

}

void displayMessage(String message) {
  tft.fillScreen(HX8357_BLACK);
  tft.setCursor(10, 0);
  tft.setTextColor(HX8357_WHITE);
  tft.setTextSize(3);
  tft.print(message);
}

void drawTempAndHumidity(float temp, float hum) {
  tft.fillScreen(HX8357_BLACK);
  tft.setCursor(10, 0);
  tft.setTextColor(HX8357_WHITE);  
  tft.setTextSize(5);
  tft.print("TEMP:  ");
  floatToString(temp, sDisp, sizeof(sDisp), 0);
  tft.print(sDisp);
  tft.print("F");
  
  tft.setTextSize(3);
  if(inside) {
    tft.print(" inside");
  } else {
    tft.print(" outside");
  }
  tft.setTextSize(5);

  drawThermometer(temp);
  tft.setCursor(10,height * 0.60);
  tft.print("HUMID: ");
  floatToString(hum, sDisp, sizeof(sDisp), 0);
  tft.print(sDisp);
  tft.print("%");

  tft.setTextSize(3);
  if(inside) {
    tft.print(" inside");
  } else {
    tft.print(" outside");
  }

  tft.setTextSize(5);

  drawHygrometer(hum);
}

bool updateInsideWeather(float *temp, float *hum) {
  Serial.println("Updating inside weather");
  sensors_event_t eHumidity, eTemp;
  //aht.getEvent(&eHumidity, &eTemp);
  sht.getEvent(&eHumidity, &eTemp);
  *hum =   eHumidity.relative_humidity;
  *temp = CtoF(eTemp.temperature); // Sensor is C

  Serial.print("  Temp:");
  Serial.println(*temp);
  Serial.print("  Hum:");
  Serial.println(*hum);
  return true;
}

bool updateOutsideWeather(float *temp, float *hum) {
  int err = 0;
  //WiFiClient wclient;
  HTTPClient http;
  
  // Send request
  //http.useHTTP10(true);
  http.begin(api_endpoint);
 
  err = http.GET();

  if(err !=200) {
    Serial.print("HTTP Error Code returned: ");
    Serial.println(err);
    return false;
  } else {
    String response = http.getString();
  
    if(response.length() < 20) {
      Serial.print("HTTP Response too short");
      return false;
    } else {
      //Setup JSON Object
      // From JSON assistant:  620+398 = 1018  I'll add some fudge to this
      const size_t capacity = 2048;
      DynamicJsonDocument doc(capacity);

      // Extract values
      DeserializationError error = deserializeJson(doc, response);
      
      if (error) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        return false;
      } else {
        *temp = doc["main"]["temp"].as<float>();
        *hum = doc["main"]["humidity"].as<float>();
      }
    }
  }

  return true;
}

bool updateTempAndHumidity(float *temp, float *hum) {
  if(inside) {
    return updateInsideWeather(temp,  hum);
  }

  return updateOutsideWeather(temp,  hum);
}

void showError() {
  displayMessage("ERROR UPDATING TEMP AND HUMIDITY");
}

bool checkTouch() {
#if defined(TSC_IRQ)
  if (digitalRead(TSC_IRQ)) {
    // IRQ pin is high, nothing to read!
    return false;
  }
#endif

  TS_Point p = ts.getPoint();

  Serial.print("X = "); Serial.print(p.x);
  Serial.print("\tY = "); Serial.print(p.y);
  Serial.print("\tPressure = "); Serial.print(p.z);
  if (((p.x == 0) && (p.y == 0)) || (p.z < 10)) {
    return false; // no pressure, no touch
  }

  return true;
}

void loop() {
  float fTemp = 0, fHum = 0;
  bool updateNow = false;
  unsigned long curMillis = millis();
  
  if(checkTouch()) {
    displayMessage("Switching weather source");
    Serial.println("Touch detected");
    inside = !inside;
    delay(1000); // Prevent fl;ipping back and forth
    updateNow = true;
  }

  if(updateNow == true
    || lastUpdateMillis == 0
    || curMillis < lastUpdateMillis 
    || (curMillis - lastUpdateMillis) > update_millis) {
    
    lastUpdateMillis = curMillis;

    if(updateTempAndHumidity(&fTemp, &fHum)) {
      Serial.print("Temp: "); 
      Serial.print(fTemp); 
      Serial.print(" C");
      Serial.print("\t\t");
      Serial.print("Humidity: "); 
      Serial.print(fHum); 
      Serial.println(" \%");

      drawTempAndHumidity(fTemp, fHum);
    } else {
      showError();
    }
  }
}
