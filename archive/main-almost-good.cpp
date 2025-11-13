#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <time.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFiManager.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// --- CONFIGURATION ---
char gmtOffset[6] = "3600";
char latitude[10] = "47.65";
char longitude[10] = "9.18";

// Display settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C

// Time settings
const char* NTP_SERVER = "pool.ntp.org";
long GMT_OFFSET_SEC = 3600;
const int DAYLIGHT_OFFSET_SEC = 0;

// Weather settings
String weatherApiUrl = "http://api.open-meteo.com/v1/forecast?latitude=47.65&longitude=9.18&current_weather=true&daily=temperature_2m_max,temperature_2m_min";

// Slideshow settings
const int VIEW_CHANGE_INTERVAL_MS = 5000;

// --- GLOBAL VARIABLES ---
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
enum View { CLOCK_VIEW, DATE_VIEW, WEATHER_VIEW, QUOTE_VIEW };
View currentView = CLOCK_VIEW;
const int TOTAL_SLIDESHOW_VIEWS = 4;
unsigned long lastViewChangeTime = 0;
String weatherTemp = "N/A";
float previousTemp = -100.0;
int weatherCode = -1;
bool shouldSaveConfig = false;

// --- QUOTES (Shortened for legibility) ---
const int NUM_QUOTES = 5;
const char* quotes[NUM_QUOTES] = {
  "Love what you do.",
  "Believe you can.",
  "Follow your dreams.",
  "Be of value.",
  "You become what you think."
};

// --- FORWARD DECLARATIONS ---
void drawView(View view);
void drawClockView();
void drawDateView();
void drawWeatherView();
void drawQuoteView();
void setupTime();
String getFormattedTimeHHMM();
String getFormattedDate();
String getDayOfWeek();
void fetchWeatherData();
void saveConfigCallback();
void loadConfig();
void saveConfig();
void updateWeatherUrl();

// --- SETUP ---
void setup() {
  Serial.begin(115200);
  while (!Serial);
  randomSeed(analogRead(A0));
  Wire.begin(12, 14);

  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    for(;;);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  display.println("Starting up...");
  display.display();

  if (LittleFS.begin()) {
    loadConfig();
  }

  WiFiManager wifiManager;
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  WiFiManagerParameter custom_gmt("gmt", "GMT Offset (sec)", gmtOffset, 6);
  WiFiManagerParameter custom_lat("lat", "Latitude", latitude, 10);
  WiFiManagerParameter custom_lon("lon", "Longitude", longitude, 10);
  wifiManager.addParameter(&custom_gmt);
  wifiManager.addParameter(&custom_lat);
  wifiManager.addParameter(&custom_lon);

  if (!wifiManager.autoConnect("ESP-Config")) {
    ESP.reset();
  }

  strcpy(gmtOffset, custom_gmt.getValue());
  strcpy(latitude, custom_lat.getValue());
  strcpy(longitude, custom_lon.getValue());

  if (shouldSaveConfig) {
    saveConfig();
  }

  updateWeatherUrl();
  GMT_OFFSET_SEC = atol(gmtOffset);
  setupTime();
  fetchWeatherData();

  currentView = CLOCK_VIEW;
  drawView(currentView);
  lastViewChangeTime = millis();
}

// --- MAIN LOOP ---
void loop() {
  if (millis() - lastViewChangeTime > VIEW_CHANGE_INTERVAL_MS) {
    View nextView = static_cast<View>((currentView + 1) % TOTAL_SLIDESHOW_VIEWS);
    currentView = nextView;
    drawView(currentView);
    lastViewChangeTime = millis();
  }
  if (currentView == CLOCK_VIEW) {
    drawView(currentView);
    delay(100);
  }
}

// --- VIEW DRAWING & ICONS ---
void drawView(View view) {
  display.clearDisplay();
  display.setCursor(0, 0);
  switch (view) {
    case CLOCK_VIEW:
      drawClockView();
      break;
    case DATE_VIEW:
      drawDateView();
      break;
    case WEATHER_VIEW:
      drawWeatherView();
      break;
    case QUOTE_VIEW:
      drawQuoteView();
      break;
  }
  display.display();
}

void drawClockView() {
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Kreuzlingen, CH");
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) { return; }
  String timeStr = getFormattedTimeHHMM();
  display.setTextSize(3);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 25);
  display.println(timeStr);
  int barHeight = 3;
  int seconds = timeinfo.tm_sec;
  int barWidth = map(seconds, 0, 59, 0, SCREEN_WIDTH);
  display.fillRect(0, SCREEN_HEIGHT - barHeight, barWidth, barHeight, WHITE);
}

void drawDateView() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) { return; }
  
  // Draw Month and Calendar Week in top (yellow) section
  char monthStr[12];
  char weekStr[6];
  strftime(monthStr, sizeof(monthStr), "%B", &timeinfo);
  strftime(weekStr, sizeof(weekStr), "CW %V", &timeinfo);
  display.setTextSize(1);
  display.setCursor(2, 2);
  display.print(monthStr);
  display.setCursor(SCREEN_WIDTH - 35, 2);
  display.print(weekStr);
  display.drawFastHLine(0, 12, SCREEN_WIDTH, WHITE);

  // Draw Day of Week and Date in bottom (blue) section
  display.setTextSize(2);
  display.setCursor(5, 20);
  display.println(getDayOfWeek());
  display.setTextSize(1);
  display.setCursor(5, 45);
  display.println(getFormattedDate());
}

void drawSun(int x, int y) { display.fillCircle(x + 10, y + 10, 8, WHITE); display.drawLine(x + 10, y, x + 10, y + 20, WHITE); display.drawLine(x, y + 10, x + 20, y + 10, WHITE); display.drawLine(x + 3, y + 3, x + 17, y + 17, WHITE); display.drawLine(x + 3, y + 17, x + 17, y + 3, WHITE); }
void drawCloud(int x, int y) { display.fillCircle(x + 8, y + 10, 6, WHITE); display.fillCircle(x + 18, y + 10, 8, WHITE); display.fillCircle(x + 13, y + 5, 7, WHITE); }
void drawRain(int x, int y) { drawCloud(x, y); display.drawLine(x + 5, y + 20, x + 5, y + 25, WHITE); display.drawLine(x + 10, y + 20, x + 10, y + 25, WHITE); display.drawLine(x + 15, y + 20, x + 15, y + 25, WHITE); }
void drawSnow(int x, int y) { drawCloud(x, y); display.drawPixel(x + 5, y + 20, WHITE); display.drawPixel(x + 5, y + 21, WHITE); display.drawPixel(x + 6, y + 20, WHITE); display.drawPixel(x + 10, y + 22, WHITE); display.drawPixel(x + 10, y + 23, WHITE); display.drawPixel(x + 11, y + 22, WHITE); display.drawPixel(x + 15, y + 20, WHITE); display.drawPixel(x + 15, y + 21, WHITE); display.drawPixel(x + 16, y + 20, WHITE); }
void drawThunderstorm(int x, int y) { drawCloud(x, y); display.drawLine(x + 10, y + 15, x + 5, y + 25, WHITE); display.drawLine(x + 5, y + 25, x + 15, y + 20, WHITE); }

void drawWeatherView() {
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Weather Forecast");
  int iconX = 5; int iconY = 20;
  switch (weatherCode) {
    case 0: drawSun(iconX, iconY); break;
    case 1: case 2: case 3: drawCloud(iconX, iconY); break;
    case 45: case 48: drawCloud(iconX, iconY); break;
    case 51: case 53: case 55: case 61: case 63: case 65: case 80: case 81: case 82: drawRain(iconX, iconY); break;
    case 71: case 73: case 75: case 77: case 85: case 86: drawSnow(iconX, iconY); break;
    case 95: case 96: case 99: drawThunderstorm(iconX, iconY); break;
    default: display.setCursor(iconX, iconY); display.println("?"); break;
  }
  display.setTextSize(2);
  display.setCursor(iconX + 35, iconY + 5); // Cursor for temperature number
  display.print(weatherTemp); // Print the number

  // Calculate position for degree symbol and 'C'
  int currentCursorX = display.getCursorX(); // X position after printing number
  int degreeSymbolX = currentCursorX + 2;
  int degreeSymbolY = iconY + 8; // Y position for degree symbol

  display.drawCircle(degreeSymbolX, degreeSymbolY, 2, WHITE); // Draw degree symbol

  // Move cursor past the degree symbol before printing 'C'
  display.setCursor(degreeSymbolX + 5, iconY + 5); // Adjust X to be after the circle
  display.println("C");

  // Draw temperature trend arrow
  float currentTemp = weatherTemp.toFloat();
  if (previousTemp > -99.0) { // Check if previousTemp is valid
      if (currentTemp > previousTemp) {
          // Draw UP arrow
          display.drawLine(iconX + 100, iconY + 10, iconX + 105, iconY + 5, WHITE);
          display.drawLine(iconX + 105, iconY + 5, iconX + 110, iconY + 10, WHITE);
      } else if (currentTemp < previousTemp) {
          // Draw DOWN arrow
          display.drawLine(iconX + 100, iconY + 5, iconX + 105, iconY + 10, WHITE);
          display.drawLine(iconX + 105, iconY + 10, iconX + 110, iconY + 5, WHITE);
      }
  }
}

void drawQuoteView() {
  int quoteIndex = random(NUM_QUOTES);
  const char* quote = quotes[quoteIndex];
  
  display.setTextSize(2);
  display.setCursor(5, 20);
  display.setTextWrap(true);
  display.println(quote);
  display.setTextWrap(false);
}

// --- TIME & WEATHER UTILS ---
void setupTime() { configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER); struct tm timeinfo; if (!getLocalTime(&timeinfo)) { Serial.println("Failed to obtain time"); return; } Serial.println("Time synchronized"); }
String getFormattedTimeHHMM() { struct tm timeinfo; if (!getLocalTime(&timeinfo)) { return "??:??"; } char timeString[6]; strftime(timeString, sizeof(timeString), "%H:%M", &timeinfo); return String(timeString); }
String getFormattedDate() { struct tm timeinfo; if (!getLocalTime(&timeinfo)) { return "??-??-????"; } char dateString[11]; strftime(dateString, sizeof(dateString), "%d-%m-%Y", &timeinfo); return String(dateString); }
String getDayOfWeek() { struct tm timeinfo; if (!getLocalTime(&timeinfo)) { return "??"; } char dayString[10]; strftime(dayString, sizeof(dayString), "%A", &timeinfo); return String(dayString); }

void fetchWeatherData() {
  if (WiFi.status() != WL_CONNECTED) { Serial.println("Weather fetch failed: WiFi not connected."); return; }
  HTTPClient http;
  WiFiClient client;
  http.begin(client, weatherApiUrl);
  int httpCode = http.GET();
  if (httpCode > 0) {
    String payload = http.getString();
    DynamicJsonDocument doc(2048); // Increased size for daily forecast
    deserializeJson(doc, payload);
    
    float currentTemp = doc["current_weather"]["temperature"];
    weatherTemp = String(currentTemp, 1); // Format to one decimal place
    weatherCode = doc["current_weather"]["weathercode"].as<int>();

    if (previousTemp > -99.0) {
        // Update previous temp only if it's not the first run
    }
    previousTemp = currentTemp; // Store current temp for next comparison

    Serial.printf("Weather updated: %s C (Code: %d)\n", weatherTemp.c_str(), weatherCode);
  } else {
    Serial.printf("Weather fetch failed, error: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();
}

// --- CONFIGURATION MANAGEMENT ---
void saveConfigCallback() { Serial.println("Should save config"); shouldSaveConfig = true; }
void updateWeatherUrl() { weatherApiUrl = "http://api.open-meteo.com/v1/forecast?latitude=" + String(latitude) + "&longitude=" + String(longitude) + "&current_weather=true&daily=temperature_2m_max,temperature_2m_min"; Serial.println("Updated weather URL: " + weatherApiUrl); }
void loadConfig() { if (LittleFS.exists("/config.json")) { File configFile = LittleFS.open("/config.json", "r"); if (configFile) { Serial.println("Reading config file"); size_t size = configFile.size(); std::unique_ptr<char[]> buf(new char[size]); configFile.readBytes(buf.get(), size); DynamicJsonDocument json(1024); if (deserializeJson(json, buf.get()) == DeserializationError::Ok) { Serial.println("Successfully parsed config"); strcpy(gmtOffset, json["gmtOffset"] | "3600"); strcpy(latitude, json["latitude"] | "47.65"); strcpy(longitude, json["longitude"] | "9.18"); } else { Serial.println("Failed to load json config"); } configFile.close(); } } }
void saveConfig() { Serial.println("Saving config"); DynamicJsonDocument json(1024); json["gmtOffset"] = gmtOffset;
json["latitude"] = latitude;
json["longitude"] = longitude;
File configFile = LittleFS.open("/config.json", "w");
if (!configFile) {
    Serial.println("Failed to open config file for writing");
    return;
}
serializeJson(json, configFile);
configFile.close();
Serial.println("Config saved");
}