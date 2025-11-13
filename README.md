# MicroDashboard

ESP8266-based OLED weather dashboard with multiple information views.

## Features

- **Clock View**: Large time display with location and seconds progress bar
- **Date View**: Current date with calendar week
- **Current Weather**: Live weather with icon and temperature
- **3-Day Forecast**: Weather forecast with icons and temperature ranges
- **Sun Times**: Sunrise/sunset times and day length
- **Moon Phase**: Current moon phase with illumination percentage
- **Quote View**: Rotating motivational quotes with WiFi SSID
- **System Info**: WiFi signal, uptime, memory, and IP address

## Hardware

- ESP8266 (ESP-12E/NodeMCU)
- SSD1306 OLED Display (128x64, I2C)
- I2C Pins: SDA=D6 (GPIO12), SCL=D7 (GPIO14)

## Weather Data

- Uses Open-Meteo API (free, no API key required)
- Automatic geocoding for city locations
- Automatic timezone detection
- Weather codes: WMO standard

## Configuration

On first boot, the device creates a WiFi access point "ESP-Config":
- City/Location
- Temperature unit (C/F/Both)
- View duration
- Manual coordinates (optional)

## Dependencies

- Adafruit GFX Library
- Adafruit SSD1306
- WiFiManager
- ArduinoJson
- LittleFS

## Building

This is a PlatformIO project. To build:

```bash
pio run
pio run --target upload
```

## License

MIT
