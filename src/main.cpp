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

// Location Configuration
char cityName[50] = "Kreuzlingen, Switzerland";
char displayName[30] = "Kreuzlingen, CH";
char timezone[50] = "CET-1CEST,M3.5.0,M10.5.0/3";
char tempUnit[2] = "C";
int viewDuration = 5000;
bool manualCoordinates = false;

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
String weatherApiUrl = "";

// Slideshow settings
const int VIEW_CHANGE_INTERVAL_MS = 5000;

// --- GLOBAL VARIABLES ---
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
enum View { CLOCK_VIEW, DATE_VIEW, WEATHER_VIEW, QUOTE_VIEW, SUN_TIMES_VIEW, MOON_VIEW, FORECAST_VIEW, SYSTEM_INFO_VIEW };
View currentView = CLOCK_VIEW;
const int TOTAL_SLIDESHOW_VIEWS = 8;
unsigned long lastViewChangeTime = 0;
String weatherTemp = "N/A";
float previousTemp = -100.0;
int weatherCode = -1;
bool shouldSaveConfig = false;

// Sun Times
String sunriseTime = "N/A";
String sunsetTime = "N/A";
unsigned long lastSunFetch = 0;
const unsigned long SUN_FETCH_INTERVAL = 3600000;

// Moon Phase
int moonPhase = 0;
float moonIllumination = 0.0;

// Extended Forecast (3-day)
String forecastDays[3] = {"", "", ""};
float forecastMaxTemps[3] = {0, 0, 0};
float forecastMinTemps[3] = {0, 0, 0};
int forecastCodes[3] = {-1, -1, -1};
unsigned long lastForecastFetch = 0;
const unsigned long FORECAST_FETCH_INTERVAL = 21600000;

// System Info
unsigned long bootTime = 0;

// --- QUOTES (Shortened for legibility) ---
const int NUM_QUOTES = 26;
const char* quotes[NUM_QUOTES] = {
  "Love what you do.",
  "Believe you can.",
  "Follow your dreams.",
  "Be of value.",
  "You become what you think.",
  "Shine bright.",
  "Stay positive.",
  "Dream big.",
  "Embrace the journey.",
  "Choose joy.",
  "Live boldly.",
  "Grow through it.",
  "Create your reality.",
  "Spread kindness.",
  "Believe in you.",
  "Stay curious.",
  "Act with purpose.",
  "Mind over matter.",
  "Focus on growth.",
  "Radiate positivity.",
  "Find your bliss.",
  "Keep moving forward.",
  "Be your best.",
  "Trust the process.",
  "Breathe and believe.",
  "Make it happen."
};


// --- FORWARD DECLARATIONS ---
void drawView(View view);
void drawClockView();
void drawDateView();
void drawWeatherView();
void drawQuoteView();
void drawSunTimesView();
void drawMoonView();
void drawForecastView();
void drawSystemInfoView();
void setupTime();
String getFormattedTimeHHMM();
String getFormattedDate();
String getDayOfWeek();
void fetchWeatherData();
void fetchGeocodingData(String city);
int calculateMoonPhase();
String getMoonPhaseName(int phase);
void drawMoonIcon(int x, int y, int phase);
float celsiusToFahrenheit(float celsius);
String formatTemperature(float tempC, bool showBoth);
String calculateDayLength();
String calculateTimeUntilSunset();
String getUptime();
int getWiFiSignalStrength();
String getWiFiSignalBars();
String getWeatherDescription(int weatherCode);
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

  bootTime = millis();

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

  char durationStr[5];
  sprintf(durationStr, "%d", viewDuration / 1000);
  char manualStr[2];
  sprintf(manualStr, "%d", manualCoordinates ? 1 : 0);

  WiFiManagerParameter custom_city("city", "City, Country", cityName, 50);
  WiFiManagerParameter custom_display("display", "Display Name (optional)", displayName, 30);
  WiFiManagerParameter custom_temp("temp", "Temp Unit (C/F/B)", tempUnit, 2);
  WiFiManagerParameter custom_duration("duration", "View Duration (sec)", durationStr, 4);
  WiFiManagerParameter custom_manual("manual", "Manual Coords? (0/1)", manualStr, 2);
  WiFiManagerParameter custom_lat("lat", "Latitude (if manual)", latitude, 10);
  WiFiManagerParameter custom_lon("lon", "Longitude (if manual)", longitude, 10);

  wifiManager.addParameter(&custom_city);
  wifiManager.addParameter(&custom_display);
  wifiManager.addParameter(&custom_temp);
  wifiManager.addParameter(&custom_duration);
  wifiManager.addParameter(&custom_manual);
  wifiManager.addParameter(&custom_lat);
  wifiManager.addParameter(&custom_lon);

  if (!wifiManager.autoConnect("ESP-Config")) {
    ESP.reset();
  }

  strcpy(cityName, custom_city.getValue());
  strcpy(displayName, custom_display.getValue());
  strcpy(tempUnit, custom_temp.getValue());
  viewDuration = atoi(custom_duration.getValue()) * 1000;
  manualCoordinates = (atoi(custom_manual.getValue()) == 1);

  if (manualCoordinates) {
    strcpy(latitude, custom_lat.getValue());
    strcpy(longitude, custom_lon.getValue());
    Serial.println("Using manual coordinates");
  } else {
    fetchGeocodingData(String(cityName));
  }

  if (strlen(displayName) == 0) {
    strcpy(displayName, cityName);
  }

  if (shouldSaveConfig) {
    saveConfig();
  }

  updateWeatherUrl();
  configTime(timezone, NTP_SERVER);

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
  } else {
    Serial.println("Time synchronized");
  }

  fetchWeatherData();

  currentView = CLOCK_VIEW;
  drawView(currentView);
  lastViewChangeTime = millis();
}

// --- MAIN LOOP ---
void loop() {
  if (millis() - lastViewChangeTime > viewDuration) {
    View nextView = static_cast<View>((currentView + 1) % TOTAL_SLIDESHOW_VIEWS);
    currentView = nextView;
    drawView(currentView);
    lastViewChangeTime = millis();
  }
  if (currentView == CLOCK_VIEW) {
    drawView(currentView);
    delay(100);
  }

  // Periodic weather updates (every 10 minutes)
  static unsigned long lastWeatherUpdate = 0;
  if (millis() - lastWeatherUpdate > 600000) {
    fetchWeatherData();
    lastWeatherUpdate = millis();
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
    case SUN_TIMES_VIEW:
      drawSunTimesView();
      break;
    case MOON_VIEW:
      drawMoonView();
      break;
    case FORECAST_VIEW:
      drawForecastView();
      break;
    case SYSTEM_INFO_VIEW:
      drawSystemInfoView();
      break;
  }
  display.display();
}

void drawClockView() {
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(displayName);
  display.drawFastHLine(0, 12, SCREEN_WIDTH, WHITE);
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
  display.println("Current Weather");
  display.drawFastHLine(0, 12, SCREEN_WIDTH, WHITE);

  int iconX = 10;
  int iconY = 18;

  // Draw weather icon
  switch (weatherCode) {
    case 0: case 1: drawSun(iconX, iconY); break;  // Clear & Mainly clear
    case 2: case 3: drawCloud(iconX, iconY); break;  // Partly cloudy & Overcast
    case 45: case 48: drawCloud(iconX, iconY); break;  // Fog
    case 51: case 53: case 55: case 61: case 63: case 65: case 80: case 81: case 82: drawRain(iconX, iconY); break;
    case 71: case 73: case 75: case 77: case 85: case 86: drawSnow(iconX, iconY); break;
    case 95: case 96: case 99: drawThunderstorm(iconX, iconY); break;
    default: display.setCursor(iconX, iconY); display.println("?"); break;
  }

  // Temperature (number only in size 3)
  float tempValue = weatherTemp.toFloat();
  int tempInt;
  if (tempUnit[0] == 'F') {
    float tempF = celsiusToFahrenheit(tempValue);
    tempInt = (int)tempF;
  } else {
    tempInt = (int)tempValue;
  }

  // Draw temperature number
  display.setTextSize(3);
  String tempNumStr = String(tempInt);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(tempNumStr, 50, 20, &x1, &y1, &w, &h);
  display.setCursor(50, 20);
  display.print(tempNumStr);

  // Draw unit in smaller size
  display.setTextSize(1);
  display.setCursor(50 + w + 2, 20);
  if (tempUnit[0] == 'F') {
    display.print("o");
    display.setCursor(50 + w + 8, 20);
    display.print("F");
  } else {
    display.print("o");
    display.setCursor(50 + w + 8, 20);
    display.print("C");
  }

  // Weather description below
  display.setTextSize(1);
  display.setCursor(2, 54);
  String weatherDesc = getWeatherDescription(weatherCode);
  display.print(weatherDesc);

  // Temperature with unit
  display.setCursor(60, 54);
  if (tempUnit[0] == 'B') {
    display.print(String(tempValue, 1) + "C");
    display.print("/");
    float tempF = celsiusToFahrenheit(tempValue);
    display.print(String(tempF, 0) + "F");
  } else if (tempUnit[0] == 'F') {
    float tempF = celsiusToFahrenheit(tempValue);
    display.print(String(tempF, 1) + "F");
  } else {
    display.print(String(tempValue, 1) + "C");
  }
}

void drawQuoteView() {
  // Draw WiFi SSID in top (yellow) section
  display.setTextSize(1);
  display.setCursor(2, 2);
  String ssid = WiFi.SSID();
  display.println(ssid);
  display.drawFastHLine(0, 12, SCREEN_WIDTH, WHITE);

  // Draw quote in bottom (blue) section with word wrapping
  int quoteIndex = random(NUM_QUOTES);
  const char* quote = quotes[quoteIndex];

  String text = String(quote);
  int textLen = text.length();
  
  // Choose font size based on quote length
  // Size 2: ~50-70 chars fit, line height ~16px, fits ~2 lines
  // Size 1: ~100+ chars fit, line height ~8px, fits ~3 lines
  int fontSize = (textLen > 70) ? 1 : 2;
  int lineHeight = (fontSize == 2) ? 16 : 8;
  
  // Dynamically adjust display area height
  // Size 2: ~2 lines + padding = 40px, so center it (start at ~12px)
  // Size 1: ~3 lines + padding = 28px, so give more space (start at ~18px)
  int cursorY = (fontSize == 2) ? 18 : 18;
  int availableHeight = SCREEN_HEIGHT - cursorY;
  
  display.setTextSize(fontSize);
  int cursorX = 5;
  int maxWidth = SCREEN_WIDTH - 10; // Leave margins

  int startIndex = 0;

  while (startIndex < text.length() && cursorY < SCREEN_HEIGHT) {
    // Find how much text fits on current line
    int endIndex = startIndex;
    int lastSpaceIndex = -1;
    String testLine = "";

    while (endIndex < text.length()) {
      testLine += text[endIndex];

      // Track last space position for word breaking
      if (text[endIndex] == ' ') {
        lastSpaceIndex = endIndex;
      }

      // Check if line width exceeds max width
      int16_t x1, y1;
      uint16_t w, h;
      display.getTextBounds(testLine, 0, 0, &x1, &y1, &w, &h);

      if (w > maxWidth) {
        // Line is too long, break at last space if available
        if (lastSpaceIndex > startIndex) {
          endIndex = lastSpaceIndex;
        } else {
          // No space found, break at previous character
          endIndex--;
        }
        break;
      }
      endIndex++;
    }

    // Draw the line
    String line = text.substring(startIndex, endIndex);
    line.trim(); // Remove leading/trailing spaces
    display.setCursor(cursorX, cursorY);
    display.println(line);

    // Move to next line
    cursorY += lineHeight;
    startIndex = endIndex;

    // Skip spaces at the start of next line
    while (startIndex < text.length() && text[startIndex] == ' ') {
      startIndex++;
    }
  }
}

void drawSunTimesView() {
  display.setTextSize(1);
  display.setCursor(2, 2);
  display.println("Sun Times");
  display.drawFastHLine(0, 12, SCREEN_WIDTH, WHITE);

  // Rise row
  int sunIconY = 22;
  display.fillCircle(4, sunIconY, 2, WHITE);
  display.drawLine(4, sunIconY - 4, 4, sunIconY - 3, WHITE);   // Top ray
  display.drawLine(0, sunIconY, 1, sunIconY, WHITE);            // Left ray
  display.drawLine(7, sunIconY, 8, sunIconY, WHITE);            // Right ray
  
  display.setCursor(12, 20);
  display.print("Rise ");
  display.println(sunriseTime);

  // Set row
  sunIconY = 32;
  display.fillCircle(4, sunIconY, 2, WHITE);
  display.drawLine(4, sunIconY + 4, 4, sunIconY + 3, WHITE);   // Bottom ray
  display.drawLine(0, sunIconY, 1, sunIconY, WHITE);            // Left ray
  display.drawLine(7, sunIconY, 8, sunIconY, WHITE);            // Right ray
  
  display.setCursor(12, 30);
  display.print("Set  ");
  display.println(sunsetTime);

  // Day length
  display.setCursor(2, 44);
  display.print("Length: ");
  display.println(calculateDayLength());

  // Time until sunset
  display.setCursor(2, 54);
  display.print("Left: ");
  String timeLeft = calculateTimeUntilSunset();
  display.println(timeLeft);
}

void drawMoonView() {
  moonPhase = calculateMoonPhase();

  display.setTextSize(1);
  display.setCursor(2, 2);
  display.println("Moon Phase");
  display.drawFastHLine(0, 12, SCREEN_WIDTH, WHITE);

  // Draw moon icon on left side
  int moonX = 5;
  int moonY = 18;
  drawMoonIcon(moonX, moonY, moonPhase);

  // Illumination percentage next to moon icon
  int illuminationPct = (int)(moonIllumination * 100);
  display.setTextSize(1);
  display.setCursor(moonX + 20, 22);
  display.print(illuminationPct);
  display.print("%");

  // Phase name (left-aligned, size 1 to prevent truncation)
  display.setTextSize(1);
  String phaseName = getMoonPhaseName(moonPhase);
  display.setCursor(2, 40);
  display.println(phaseName);

  // Lunar day info (left-aligned)
  display.setCursor(2, 50);
  int lunarDay = (int)(moonIllumination * 29.53);
  display.print("Day ");
  display.print(lunarDay);
  display.print(" of 29");
}

void drawMoonIcon(int x, int y, int phase) {
  int radius = 8;  // Smaller: 16x16 total size
  int centerX = x + radius;
  int centerY = y + radius;

  // Draw full circle
  display.fillCircle(centerX, centerY, radius, WHITE);

  // Draw shadow based on phase
  if (phase == 0) {
    // New moon - all dark
    display.fillCircle(centerX, centerY, radius - 1, BLACK);
  } else if (phase == 4) {
    // Full moon - no shadow
  } else if (phase < 4) {
    // Waxing - shadow on left
    int shadowWidth = map(phase, 0, 4, radius, -radius);
    for (int i = -radius; i <= radius; i++) {
      int lineHeight = sqrt(radius * radius - i * i);
      if (i < shadowWidth) {
        display.drawLine(centerX + i, centerY - lineHeight,
                        centerX + i, centerY + lineHeight, BLACK);
      }
    }
  } else {
    // Waning - shadow on right
    int shadowWidth = map(phase, 4, 8, -radius, radius);
    for (int i = -radius; i <= radius; i++) {
      int lineHeight = sqrt(radius * radius - i * i);
      if (i > shadowWidth) {
        display.drawLine(centerX + i, centerY - lineHeight,
                        centerX + i, centerY + lineHeight, BLACK);
      }
    }
  }

  // Outline
  display.drawCircle(centerX, centerY, radius, WHITE);
}

void drawForecastView() {
  display.setTextSize(1);
  display.setCursor(2, 2);
  display.println("3-Day Forecast");
  display.drawFastHLine(0, 12, SCREEN_WIDTH, WHITE);

  int yPos = 16;
  int lineHeight = 11;

  for (int i = 0; i < 3; i++) {
    int iconCode = forecastCodes[i];
    Serial.printf("Draw forecast %d: Code=%d\n", i, iconCode);

    // Get weather icon character
    String iconChar = " ";
    if (iconCode == 0 || iconCode == 1) {
      iconChar = "O";  // Sun
    } else if (iconCode == 2 || iconCode == 3) {
      iconChar = "~";  // Cloud
    } else if (iconCode == 45 || iconCode == 48) {
      iconChar = "=";  // Fog
    } else if ((iconCode >= 51 && iconCode <= 67) || (iconCode >= 80 && iconCode <= 82)) {
      iconChar = "'";  // Rain
    } else if ((iconCode >= 71 && iconCode <= 77) || (iconCode >= 85 && iconCode <= 86)) {
      iconChar = "*";  // Snow
    } else if (iconCode >= 95) {
      iconChar = "#";  // Storm
    }

    // Day number
    String dateStr = forecastDays[i];
    String dayNum = "  ";
    if (dateStr.length() >= 10) {
      dayNum = dateStr.substring(8, 10);
    }

    // Weather description
    String weatherDesc = getWeatherDescription(iconCode);

    // Pad weather description to fixed width
    while (weatherDesc.length() < 8) {
      weatherDesc += " ";
    }

    // Temperature range
    String tempRange = "";
    if (tempUnit[0] == 'B') {
      tempRange = String((int)forecastMaxTemps[i]) + "/" +
                  String((int)forecastMinTemps[i]) + "C";
    } else if (tempUnit[0] == 'F') {
      tempRange = String((int)celsiusToFahrenheit(forecastMaxTemps[i])) + "/" +
                  String((int)celsiusToFahrenheit(forecastMinTemps[i])) + "F";
    } else {
      tempRange = String((int)forecastMaxTemps[i]) + "/" +
                  String((int)forecastMinTemps[i]) + "C";
    }

    // Build complete line: Icon Day Desc Temp
    display.setCursor(2, yPos);
    display.print(iconChar);
    display.print(" ");
    display.print(dayNum);
    display.print(" ");
    display.print(weatherDesc);
    display.print(" ");
    display.print(tempRange);

    yPos += lineHeight;
  }

  // Show current weather in last line
  display.drawFastHLine(0, 51, SCREEN_WIDTH, WHITE);
  display.setCursor(2, 54);
  display.print("Now: ");
  display.print(getWeatherDescription(weatherCode));
  display.print(" ");
  display.print(weatherTemp);
  if (tempUnit[0] == 'F') {
    float tempF = celsiusToFahrenheit(weatherTemp.toFloat());
    display.print("F");
  } else {
    display.print("C");
  }
}

void drawSystemInfoView() {
  display.setTextSize(1);
  display.setCursor(2, 2);
  display.println("System Info");
  display.drawFastHLine(0, 12, SCREEN_WIDTH, WHITE);

  int yPos = 18;
  int lineHeight = 11;

  // WiFi strength
  display.setCursor(2, yPos);
  display.print("WiFi:");
  display.print(getWiFiSignalBars());
  display.print(" ");
  display.print(getWiFiSignalStrength());
  display.println("dBm");
  yPos += lineHeight;

  // Uptime
  display.setCursor(2, yPos);
  display.print("Up: ");
  display.println(getUptime());
  yPos += lineHeight;

  // Free memory
  display.setCursor(2, yPos);
  display.print("RAM: ");
  display.print(ESP.getFreeHeap() / 1024);
  display.println("KB");
  yPos += lineHeight;

  // IP address
  display.setCursor(2, yPos);
  display.print("IP: ");
  display.println(WiFi.localIP().toString());
}

// --- TIME & WEATHER UTILS ---
void setupTime() { configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER); struct tm timeinfo; if (!getLocalTime(&timeinfo)) { Serial.println("Failed to obtain time"); return; } Serial.println("Time synchronized"); }
String getFormattedTimeHHMM() { struct tm timeinfo; if (!getLocalTime(&timeinfo)) { return "??:??"; } char timeString[6]; strftime(timeString, sizeof(timeString), "%H:%M", &timeinfo); return String(timeString); }
String getFormattedDate() { struct tm timeinfo; if (!getLocalTime(&timeinfo)) { return "??-??-????"; } char dateString[11]; strftime(dateString, sizeof(dateString), "%d-%m-%Y", &timeinfo); return String(dateString); }
String getDayOfWeek() { struct tm timeinfo; if (!getLocalTime(&timeinfo)) { return "??"; } char dayString[10]; strftime(dayString, sizeof(dayString), "%A", &timeinfo); return String(dayString); }

float celsiusToFahrenheit(float celsius) {
  return (celsius * 9.0 / 5.0) + 32.0;
}

String formatTemperature(float tempC, bool showBoth) {
  if (tempUnit[0] == 'F') {
    float tempF = celsiusToFahrenheit(tempC);
    return String(tempF, 1) + "F";
  } else if (tempUnit[0] == 'B' || showBoth) {
    float tempF = celsiusToFahrenheit(tempC);
    return String(tempC, 1) + "C/" + String(tempF, 0) + "F";
  } else {
    return String(tempC, 1) + "C";
  }
}

String calculateDayLength() {
  if (sunriseTime == "N/A" || sunsetTime == "N/A") {
    return "N/A";
  }

  int riseH = sunriseTime.substring(0, 2).toInt();
  int riseM = sunriseTime.substring(3, 5).toInt();
  int setH = sunsetTime.substring(0, 2).toInt();
  int setM = sunsetTime.substring(3, 5).toInt();

  int totalMinutes = (setH * 60 + setM) - (riseH * 60 + riseM);
  int hours = totalMinutes / 60;
  int minutes = totalMinutes % 60;

  return String(hours) + "h " + String(minutes) + "m";
}

String calculateTimeUntilSunset() {
  if (sunsetTime == "N/A") {
    return "N/A";
  }

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "N/A";
  }

  int nowH = timeinfo.tm_hour;
  int nowM = timeinfo.tm_min;
  int setH = sunsetTime.substring(0, 2).toInt();
  int setM = sunsetTime.substring(3, 5).toInt();

  int nowMinutes = nowH * 60 + nowM;
  int setMinutes = setH * 60 + setM;
  int remaining = setMinutes - nowMinutes;

  if (remaining < 0) {
    return "Set"; // Sun already set
  }

  int hours = remaining / 60;
  int minutes = remaining % 60;

  if (hours > 0) {
    return String(hours) + "h " + String(minutes) + "m";
  } else {
    return String(minutes) + "m";
  }
}

int calculateMoonPhase() {
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);

  int year = timeinfo->tm_year + 1900;
  int month = timeinfo->tm_mon + 1;
  int day = timeinfo->tm_mday;

  if (month < 3) {
    year--;
    month += 12;
  }

  int a = year / 100;
  int b = a / 4;
  int c = 2 - a + b;
  int e = 365.25 * (year + 4716);
  int f = 30.6001 * (month + 1);

  double jd = c + day + e + f - 1524.5;
  double daysSinceNew = jd - 2451549.5;
  double newMoons = daysSinceNew / 29.53;
  double phase = (newMoons - (int)newMoons);

  moonIllumination = phase;

  if (phase < 0.0625) return 0;
  if (phase < 0.1875) return 1;
  if (phase < 0.3125) return 2;
  if (phase < 0.4375) return 3;
  if (phase < 0.5625) return 4;
  if (phase < 0.6875) return 5;
  if (phase < 0.8125) return 6;
  return 7;
}

String getMoonPhaseName(int phase) {
  const char* names[] = {
    "New Moon",
    "Waxing Crescent",
    "First Quarter",
    "Waxing Gibbous",
    "Full Moon",
    "Waning Gibbous",
    "Last Quarter",
    "Waning Crescent"
  };
  return String(names[phase]);
}

String getUptime() {
  unsigned long uptime = (millis() - bootTime) / 1000;
  int days = uptime / 86400;
  int hours = (uptime % 86400) / 3600;
  int minutes = (uptime % 3600) / 60;

  if (days > 0) {
    return String(days) + "d " + String(hours) + "h";
  } else if (hours > 0) {
    return String(hours) + "h " + String(minutes) + "m";
  } else {
    return String(minutes) + "m";
  }
}

int getWiFiSignalStrength() {
  return WiFi.RSSI();
}

String getWiFiSignalBars() {
  int rssi = WiFi.RSSI();
  if (rssi >= -50) return "####";
  if (rssi >= -60) return "###.";
  if (rssi >= -70) return "##..";
  if (rssi >= -80) return "#...";
  return "....";
}

void fetchGeocodingData(String city) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Geocoding failed: WiFi not connected");
    return;
  }

  String url = "https://geocoding-api.open-meteo.com/v1/search?name=" + city + "&count=1&language=en&format=json";

  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();
  http.begin(client, url);

  int httpCode = http.GET();
  if (httpCode > 0) {
    String payload = http.getString();
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, payload);

    if (doc["results"].size() > 0) {
      float lat = doc["results"][0]["latitude"];
      float lon = doc["results"][0]["longitude"];
      const char* tz = doc["results"][0]["timezone"];
      const char* name = doc["results"][0]["name"];
      const char* country = doc["results"][0]["country"];

      dtostrf(lat, 8, 4, latitude);
      dtostrf(lon, 8, 4, longitude);

      if (tz != nullptr) {
        strcpy(timezone, tz);
      }

      Serial.printf("Geocoding success: %s, %s (%.4f, %.4f)\n", name, country, lat, lon);
      Serial.printf("Timezone: %s\n", timezone);
    } else {
      Serial.println("City not found, using defaults");
    }
  } else {
    Serial.printf("Geocoding failed: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();
}

void fetchWeatherData() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Weather fetch failed: WiFi not connected");
    return;
  }

  weatherApiUrl = "https://api.open-meteo.com/v1/forecast?";
  weatherApiUrl += "latitude=" + String(latitude);
  weatherApiUrl += "&longitude=" + String(longitude);
  weatherApiUrl += "&current_weather=true";
  weatherApiUrl += "&daily=temperature_2m_max,temperature_2m_min,weathercode,sunrise,sunset";
  weatherApiUrl += "&forecast_days=3";
  weatherApiUrl += "&timezone=auto";

  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();
  http.begin(client, weatherApiUrl);

  int httpCode = http.GET();
  if (httpCode > 0) {
    String payload = http.getString();
    DynamicJsonDocument doc(4096);
    deserializeJson(doc, payload);

    float currentTemp = doc["current_weather"]["temperature"];
    weatherTemp = String(currentTemp, 1);
    weatherCode = doc["current_weather"]["weathercode"].as<int>();

    if (previousTemp > -99.0) {
      // Temperature trend tracking
    }
    previousTemp = currentTemp;
    
    Serial.printf("Weather code: %d, Temp: %.1f\n", weatherCode, currentTemp);

    const char* sunrise = doc["daily"]["sunrise"][0];
    const char* sunset = doc["daily"]["sunset"][0];
    if (sunrise != nullptr) {
      sunriseTime = String(sunrise).substring(11, 16);
    }
    if (sunset != nullptr) {
      sunsetTime = String(sunset).substring(11, 16);
    }

    for (int i = 0; i < 3; i++) {
      forecastMaxTemps[i] = doc["daily"]["temperature_2m_max"][i];
      forecastMinTemps[i] = doc["daily"]["temperature_2m_min"][i];
      forecastCodes[i] = doc["daily"]["weathercode"][i];
      const char* date = doc["daily"]["time"][i];
      if (date != nullptr) {
        forecastDays[i] = String(date);
      }
      Serial.printf("Forecast day %d: Code=%d, Max=%.1f, Min=%.1f\n", 
                    i, forecastCodes[i], forecastMaxTemps[i], forecastMinTemps[i]);
    }

    lastForecastFetch = millis();
    lastSunFetch = millis();

    Serial.printf("Weather updated: %s°C, Code: %d\n", weatherTemp.c_str(), weatherCode);
    Serial.printf("Sunrise: %s, Sunset: %s\n", sunriseTime.c_str(), sunsetTime.c_str());
  } else {
    Serial.printf("Weather fetch failed: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();
}

String getWeatherDescription(int weatherCode) {
  // WMO Weather codes to short text
  if (weatherCode == 0) return "Clear";
  if (weatherCode == 1) return "M.Clear";  // Mainly clear
  if (weatherCode == 2) return "P.Cloudy"; // Partly cloudy
  if (weatherCode == 3) return "Overcast";
  if (weatherCode == 45 || weatherCode == 48) return "Fog";
  if (weatherCode >= 51 && weatherCode <= 55) return "Drizzle";
  if (weatherCode >= 61 && weatherCode <= 65) return "Rain";
  if (weatherCode == 66 || weatherCode == 67) return "Fr.Rain"; // Freezing rain
  if (weatherCode >= 71 && weatherCode <= 77) return "Snow";
  if (weatherCode >= 80 && weatherCode <= 82) return "Showers";
  if (weatherCode >= 85 && weatherCode <= 86) return "SnowSh";  // Snow showers
  if (weatherCode >= 95 && weatherCode <= 99) return "Storm";
  return "N/A";
}

// --- CONFIGURATION MANAGEMENT ---
void saveConfigCallback() { Serial.println("Should save config"); shouldSaveConfig = true; }
void updateWeatherUrl() { weatherApiUrl = "http://api.open-meteo.com/v1/forecast?latitude=" + String(latitude) + "&longitude=" + String(longitude) + "&current_weather=true&daily=temperature_2m_max,temperature_2m_min"; Serial.println("Updated weather URL: " + weatherApiUrl); }
void loadConfig() {
  if (LittleFS.exists("/config.json")) {
    File configFile = LittleFS.open("/config.json", "r");
    if (configFile) {
      Serial.println("Reading config file");
      size_t size = configFile.size();
      std::unique_ptr<char[]> buf(new char[size]);
      configFile.readBytes(buf.get(), size);
      DynamicJsonDocument json(2048);
      if (deserializeJson(json, buf.get()) == DeserializationError::Ok) {
        Serial.println("Successfully parsed config");
        strcpy(cityName, json["cityName"] | "Kreuzlingen, Switzerland");
        strcpy(displayName, json["displayName"] | "Kreuzlingen, CH");
        strcpy(latitude, json["latitude"] | "47.65");
        strcpy(longitude, json["longitude"] | "9.18");
        strcpy(timezone, json["timezone"] | "CET-1CEST,M3.5.0,M10.5.0/3");
        strcpy(tempUnit, json["tempUnit"] | "C");
        viewDuration = json["viewDuration"] | 5000;
        manualCoordinates = json["manualCoordinates"] | false;
        Serial.printf("Loaded: %s at (%.2f, %.2f)\n", cityName, atof(latitude), atof(longitude));
      } else {
        Serial.println("Failed to load json config");
      }
      configFile.close();
    }
  }
}
void saveConfig() {
  Serial.println("Saving config");
  DynamicJsonDocument json(2048);
  json["cityName"] = cityName;
  json["displayName"] = displayName;
  json["latitude"] = latitude;
  json["longitude"] = longitude;
  json["timezone"] = timezone;
  json["tempUnit"] = tempUnit;
  json["viewDuration"] = viewDuration;
  json["manualCoordinates"] = manualCoordinates;

  File configFile = LittleFS.open("/config.json", "w");
  if (!configFile) {
    Serial.println("Failed to open config file for writing");
    return;
  }
  serializeJson(json, configFile);
  configFile.close();
  Serial.println("Config saved");
}