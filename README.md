# TempAndHumidity2
New version with graphical bars, touch, and switching from inside to outside weather.

Need top create a secrets.h file with this variables set:

// WiFi constants
const char* ssid     = "YourHotspot";
const char* password = "YourPWD";

// API constants
const char* api_endpoint = "https://api.openweathermap.org/data/2.5/weather?id=NUMERICAL_ID_FOR_YOU_CITY&units=imperial&appid=YOUR_APP_ID_FOR_OPENWEATHER";
