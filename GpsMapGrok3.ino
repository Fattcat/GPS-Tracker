#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <SD.h>
#include <TinyGPS++.h>
#include <SoftwareSerial.h>

// Pin definitions
#define TFT_CS    10
#define TFT_DC    9
#define TFT_RST   8
#define SD_CS     4

// Initialize display and GPS
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
SoftwareSerial gpsSerial(3, 2); // RX, TX for GPS
TinyGPSPlus gps;

// Map boundaries in GPS coordinates (adjust to your map)
#define LAT_MIN 48.1400
#define LAT_MAX 48.1500
#define LON_MIN 17.1000
#define LON_MAX 17.1200

// Load and draw BMP map
void drawBMP(const char *filename) {
    File bmpFile = SD.open(filename);
    if (!bmpFile) {
        Serial.println("Error opening BMP file!");
        tft.fillScreen(ST77XX_BLACK);
        tft.setCursor(0, 0);
        tft.println("BMP Error");
        return;
    }

    bmpFile.seek(54); // Skip BMP header
    for (int y = 0; y < 320; y++) {
        for (int x = 0; x < 240; x++) {
            uint8_t b = bmpFile.read();
            uint8_t g = bmpFile.read();
            uint8_t r = bmpFile.read();
            uint16_t color = tft.color565(r, g, b);
            tft.drawPixel(x, 319 - y, color);
        }
    }
    bmpFile.close();
}

// Convert GPS coordinates to screen coordinates
int gpsToX(float lon) {
    return map(lon * 1000000, LON_MIN * 1000000, LON_MAX * 1000000, 0, 240);
}

int gpsToY(float lat) {
    return map(lat * 1000000, LAT_MAX * 1000000, LAT_MIN * 1000000, 0, 320);
}

// Draw cursor at GPS position
void drawCursor(float lat, float lon, uint16_t color = ST77XX_RED) {
    int x = gpsToX(lon);
    int y = gpsToY(lat);
    if (x >= 0 && x < 240 && y >= 0 && y < 320) { // Check bounds
        tft.fillCircle(x, y, 5, color);
    }
}

void setup() {
    Serial.begin(115200);
    gpsSerial.begin(9600);

    // Initialize display
    tft.init(240, 320);
    tft.setRotation(3);
    tft.fillScreen(ST77XX_BLACK);

    // Initialize SD card
    if (!SD.begin(SD_CS)) {
        Serial.println("SD card not found!");
        tft.println("SD Error");
        while (1); // Halt if SD fails
    }

    // Draw map once at startup
    drawBMP("/mapa.bmp");
}

void loop() {
    static float lastLat = 0, lastLon = 0; // Store last position
    static unsigned long lastUpdate = 0;
    const unsigned long updateInterval = 1000; // Update every 1 second

    // Process GPS data continuously
    while (gpsSerial.available() > 0) {
        if (gps.encode(gpsSerial.read())) {
            if (gps.location.isValid() && millis() - lastUpdate >= updateInterval) {
                float latitude = gps.location.lat();
                float longitude = gps.location.lng();

                // Only update if position has changed significantly
                if (abs(latitude - lastLat) > 0.0001 || abs(longitude - lastLon) > 0.0001) {
                    // Erase old cursor by redrawing map section or using a black circle
                    drawCursor(lastLat, lastLon, ST77XX_BLACK); // Erase previous
                    drawCursor(latitude, longitude); // Draw new position

                    lastLat = latitude;
                    lastLon = longitude;
                    lastUpdate = millis();

                    // Debug output
                    Serial.print("Lat: "); Serial.print(latitude, 6);
                    Serial.print(" Lon: "); Serial.println(longitude, 6);
                }
            }
        }
    }

    // Handle no GPS signal
    if (millis() > 5000 && gps.charsProcessed() < 10) {
        tft.fillScreen(ST77XX_BLACK);
        tft.setCursor(0, 0);
        tft.println("No GPS Signal");
        drawBMP("/mapa.bmp"); // Redraw map
        lastUpdate = millis(); // Reset timer
    }
}