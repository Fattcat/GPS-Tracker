#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <SD.h>
#include <TinyGPS++.h>
#include <SoftwareSerial.h>

// Pinové definície
#define TFT_CS    10
#define TFT_DC    9
#define TFT_RST   8
#define SD_CS     4

// Inicializácia displeja a GPS
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
SoftwareSerial gpsSerial(3, 2); // RX, TX pre GPS
TinyGPSPlus gps;

// Hranice mapy v GPS súradniciach (prispôsob podľa mapy)
#define LAT_MIN 48.1400
#define LAT_MAX 48.1500
#define LON_MIN 17.1000
#define LON_MAX 17.1200

// Načítanie BMP mapy
void drawBMP(const char *filename) {
    File bmpFile = SD.open(filename);
    if (!bmpFile) {
        Serial.println("Chyba pri otváraní BMP súboru!");
        return;
    }

    bmpFile.seek(54); // Preskočenie hlavičky BMP

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

// Prepočet GPS súradníc na súradnice na mape
int gpsToX(float lon) {
    return map(lon * 1000000, LON_MIN * 1000000, LON_MAX * 1000000, 0, 240);
}

int gpsToY(float lat) {
    return map(lat * 1000000, LAT_MAX * 1000000, LAT_MIN * 1000000, 0, 320);
}

// Vykreslenie kurzora
void drawCursor(float lat, float lon) {
    int x = gpsToX(lon);
    int y = gpsToY(lat);
    
    tft.fillCircle(x, y, 5, ST77XX_RED); // Červený kruh ako kurzor
}

void setup() {
    Serial.begin(115200);
    gpsSerial.begin(9600);

    tft.init(240, 320);
    tft.setRotation(3);
    tft.fillScreen(ST77XX_BLACK);

    if (!SD.begin(SD_CS)) {
        Serial.println("SD karta nenájdená!");
        return;
    }

    drawBMP("/mapa.bmp");
}

void loop() {
    static unsigned long lastUpdate = 0;
    
    if (millis() - lastUpdate >= 2000) { // Každé 2 sekundy
        lastUpdate = millis();
        
        while (gpsSerial.available() > 0) {
            if (gps.encode(gpsSerial.read())) {
                if (gps.location.isValid()) {
                    float latitude = gps.location.lat();
                    float longitude = gps.location.lng();
                    
                    drawBMP("/mapa.bmp"); // Prekreslenie mapy
                    drawCursor(latitude, longitude); // Vykreslenie novej polohy
                }
            }
        }
    }
}