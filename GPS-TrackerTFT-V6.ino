/*
 * ============================================================
 *  GPS Tracker v6.0 — TFT Dashboard (ST7789 240x320)
 *  Rýchly render, Tachometer, Farebné statusy
 * ============================================================
 MAKE SURE in Documents\Arduino\libraries\TFT_eSPI User_Setup.h :

#define ST7789_DRIVER
#define TFT_WIDTH  240
#define TFT_HEIGHT 320
#define TFT_MOSI 23
#define TFT_SCLK 18
#define TFT_DC   2    // Dátový pin
#define TFT_RST  -1   // Reset (ak je pripojený na 3.3V, daj -1, inak číslo pinu)
#define TFT_CS   15   // Chip Select
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define SMOOTH_FONT
#define SPI_FREQUENCY  40000000
 */

#include <TFT_eSPI.h> // Použi TFT_eSPI (rýchlejšie ako Adafruit)
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include <SPI.h>
#include <SD.h>

// ============================================================
//  HARDWARE PINS (Uprav podľa zapojenia)
// ============================================================
// TFT (SPI): SCK=18, MISO=19, MOSI=23 (štandardné VSPI)
// TFT DC=2, CS=15, RST=-1 (alebo iný)
// SD: CS=5
#define SD_CS             5
#define BUZZER_PIN        26 // Presunuté, aby nekolidovalo s TFT
#define GPS_RX            16
#define GPS_TX            17

// ============================================================
//  KONFIGURÁCIA
// ============================================================
#define MAX_SPEED_KMH     120  // Maximum na tachometri
#define MIN_SPEED_KMH     0.8f
#define MAX_HDOP          2.5f
#define MIN_SATS          5
#define MIN_DIST_M        5.0f
#define LOG_INTERVAL_MS   2000

TFT_eSPI tft = TFT_eSPI(); 
TinyGPSPlus gps;
HardwareSerial SerialGPS(1);

// Štruktúry dát
struct SystemState {
  bool sdReady = false;
  bool gpsFix = false;
  bool isMoving = false;
  bool blinkVisible = true;
  unsigned long lastBlink = 0;
} sys;

struct GPSData {
  double lat = 0, lng = 0, alt = 0;
  float speed = 0, hdop = 99.9;
  uint8_t sats = 0;
  uint8_t hour = 0, minute = 0, second = 0;
  uint16_t day = 1, month = 1, year = 2026;
  bool valid = false;
} gpsData;

struct TrackStats {
  uint32_t pointCount = 0;
  float totalDistM = 0;
  uint32_t skippedNoise = 0;
  char fileName[20] = "/track0001.csv";
} track;

struct LastPos {
  double lat = 0, lng = 0;
  bool has = false;
} lastPos;

unsigned long lastWriteMs = 0;
float oldSpeed = 0; // Na hladkú animáciu ručičky

// ============================================================
//  POMOCNÉ FUNKCIE
// ============================================================

float haversineM(double la1, double lo1, double la2, double lo2) {
  const float R = 6371000.0f;
  float dLa = radians((float)(la2 - la1));
  float dLo = radians((float)(lo2 - lo1));
  float a = sinf(dLa * 0.5f) * sinf(dLa * 0.5f) +
            cosf(radians((float)la1)) * cosf(radians((float)la2)) *
            sinf(dLo * 0.5f) * sinf(dLo * 0.5f);
  return R * 2.0f * atan2f(sqrtf(a), sqrtf(1.0f - a));
}

// Vykreslenie tachometra (statická časť)
void drawDashboardBase() {
  tft.fillScreen(TFT_BLACK);

  // Pozadie tachometra
  tft.drawCircle(90, 120, 75, 0x39E7); // Svetlomodrý okraj
  tft.drawCircle(90, 120, 72, 0x2986); // Tmavomodrý okraj
  
  // Oblúk škály (Červená - Žltá - Zelená)
  // Ľavá časť (nízka rýchlosť) - Zelená
  tft.drawArc(90, 120, 60, 50, -140, -45, TFT_GREEN, TFT_BLACK);
  // Stred (stredná) - Žltá
  tft.drawArc(90, 120, 60, 50, -45, 45, TFT_YELLOW, TFT_BLACK);
  // Pravá (vysoká) - Červená
  tft.drawArc(90, 120, 60, 50, 45, 140, TFT_RED, TFT_BLACK);

  // Texty na škále
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.setCursor(35, 65); tft.print("0");
  tft.setCursor(45, 175); tft.print("60");
  tft.setCursor(115, 175); tft.print("120");

  // Pravý panel - Header
  tft.setTextSize(2);
  tft.setTextColor(TFT_CYAN);
  tft.setCursor(175, 10);
  tft.println("STATS");
  tft.drawFastHLine(165, 30, 65, TFT_CYAN);

  // Spodný status bar
  tft.fillRect(0, 200, 240, 40, 0x18E3); // Tmavý pozadie
  tft.drawFastHLine(0, 200, 240, TFT_WHITE);
}

// Vykreslenie ručičky
void drawNeedle(float speed) {
  // Interpolácia uhla: -140 stupňov (0 km/h) až +140 stupňov (120 km/h)
  float angle = map(speed, 0, MAX_SPEED_KMH, -140, 140);
  if (angle < -140) angle = -140;
  if (angle > 140) angle = 140;

  // Vymazanie starej ručičky (čiernym kruhom)
  tft.fillCircle(90, 120, 45, TFT_BLACK);
  
  // Prekreslenie stredu
  tft.drawCircle(90, 120, 45, TFT_NAVY);
  
  // Výpočet súradníc konca ručičky
  int rad = radians(angle);
  int endX = 90 + 40 * cos(rad);
  int endY = 120 + 40 * sin(rad);

  // Vykreslenie novej ručičky
  int needleColor = (speed > 80) ? TFT_RED : TFT_ORANGE;
  tft.drawLine(90, 120, endX, endY, needleColor);
  tft.drawLine(90, 120, endX + 1, endY + 1, needleColor); // Hrúbka 2px
  
  // Stredový čap
  tft.fillCircle(90, 120, 5, TFT_WHITE);
}

// Vykreslenie digitálnej rýchlosti v strede
void drawCenterSpeed(float speed) {
  tft.fillRect(70, 135, 40, 20, TFT_BLACK); // Vymazanie
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(75, 135);
  tft.print((int)speed);
  
  tft.setTextSize(1);
  tft.setTextColor(TFT_ORANGE);
  tft.print("km/h");
}

// Vykreslenie pravého panela (Texty)
void drawStatsPanel() {
  tft.setTextSize(1);
  
  // Satelity (Blikanie logika)
  if (sys.blinkVisible || gpsData.sats >= 4) {
    tft.setTextColor(gpsData.sats >= 4 ? TFT_GREEN : TFT_RED);
  } else {
    tft.setTextColor(TFT_BLACK); // "Vypne" text pri blikaní
  }
  tft.setCursor(170, 45);
  tft.printf("SAT: %d", gpsData.sats);

  // HDOP
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(170, 65);
  tft.printf("HDOP: %.1f", gpsData.hdop);

  // Čas (Veľký)
  tft.setTextColor(TFT_YELLOW);
  tft.setTextSize(2);
  tft.setCursor(165, 95);
  tft.printf("%02d:%02d", gpsData.hour, gpsData.minute);

  // Dátum
  tft.setTextColor(TFT_NAVY);
  tft.setTextSize(1);
  tft.setCursor(170, 125);
  tft.printf("%02d.%02d.%04d", gpsData.day, gpsData.month, gpsData.year);

  // Vzdialenosť
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(170, 150);
  if (track.totalDistM < 1000) tft.printf("DIST: %.0fm", track.totalDistM);
  else tft.printf("DIST: %.1fkm", track.totalDistM / 1000.0f);

  // Body
  tft.setCursor(170, 170);
  tft.printf("POINTS: %lu", track.pointCount);
}

// Spodný status bar
void drawStatusBar() {
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  
  // Stav nahrávania
  if (sys.gpsFix) {
    tft.setCursor(10, 210);
    tft.print(sys.isMoving ? "REC ●" : "STOP ○");
    tft.setTextColor(sys.isMoving ? TFT_GREEN : TFT_RED);
  } else {
    tft.setCursor(10, 210);
    tft.print("SEARCHING");
  }

  // Šum (Skip)
  tft.setTextColor(TFT_NAVY);
  tft.setTextSize(1);
  tft.setCursor(150, 215);
  tft.printf("SKIP: %lu", track.skippedNoise);
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  SerialGPS.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
  
  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN);

  // Init TFT
  tft.init();
  tft.setRotation(3); // Landscape (320x240)
  tft.fillScreen(TFT_BLACK);
  tft.setTextFont(2);

  drawDashboardBase();
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(100, 100);
  tft.println("BOOTING...");
  delay(1000);

  // Init SD
  if (SD.begin(SD_CS)) {
    sys.sdReady = true;
    // Logika vytvorenia súboru...
    for (int i = 1; i <= 9999; i++) {
      sprintf(track.fileName, "/track%04d.csv", i);
      if (!SD.exists(track.fileName)) break;
    }
    File f = SD.open(track.fileName, FILE_WRITE);
    if (f) {
      f.println("lat,lon,alt,time,speed,hdop,sats");
      f.close();
    }
  }
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  while (SerialGPS.available()) {
    char c = SerialGPS.read();
    if (gps.encode(c)) {
      gpsData.lat = gps.location.lat();
      gpsData.lng = gps.location.lng();
      gpsData.speed = gps.speed.isValid() ? gps.speed.kmph() : 0;
      gpsData.hdop = gps.hdop.isValid() ? gps.hdop.hdop() : 99.9;
      gpsData.sats = gps.satellites.isValid() ? (uint8_t)gps.satellites.value() : 0;
      gpsData.valid = gps.location.isValid();
      
      // Čas
      if (gps.time.isValid()) {
        gpsData.hour = gps.time.hour(); // + UTC offset logic here
        gpsData.minute = gps.time.minute();
        gpsData.second = gps.time.second();
      }
      if (gps.date.isValid()) {
        gpsData.day = gps.date.day();
        gpsData.month = gps.date.month();
        gpsData.year = gps.date.year();
      }
    }
  }

  // Blikanie satelitov (0-3)
  if (millis() - sys.lastBlink > 400) {
    sys.lastBlink = millis();
    sys.blinkVisible = !sys.blinkVisible;
  }

  // Fix detekcia
  if (gpsData.sats >= 4 && gpsData.hdop < MAX_HDOP) sys.gpsFix = true;
  else sys.gpsFix = false;

  // Pohyb
  sys.isMoving = (gpsData.speed >= MIN_SPEED_KMH);

  // Zápis bodu
  if (sys.gpsFix && sys.sdReady && gpsData.valid && (millis() - lastWriteMs > LOG_INTERVAL_MS)) {
     // ... Logika zápisu na SD (rovnaká ako predtým) ...
     // Zjednodušene pre tento výpis:
     if (gpsData.speed >= MIN_SPEED_KMH && gpsData.hdop < MAX_HDOP) {
        File f = SD.open(track.fileName, FILE_APPEND);
        if(f) {
           f.printf("%.6f,%.6f,%.1f,%02d:%02d:%02d,%.2f,%.2f,%d\n", 
             gpsData.lat, gpsData.lng, gps.altitude.meters(), 
             gpsData.hour, gpsData.minute, gpsData.second, 
             gpsData.speed, gpsData.hdop, gpsData.sats);
           f.close();
           track.pointCount++;
           if(lastPos.has) track.totalDistM += haversineM(lastPos.lat, lastPos.lng, gpsData.lat, gpsData.lng);
           lastPos.lat = gpsData.lat; lastPos.lng = gpsData.lng; lastPos.has = true;
        }
     } else {
       track.skippedNoise++;
     }
     lastWriteMs = millis();
  }

  // --- RENDER (Vykresľovanie) ---
  
  // 1. Tachometer (Len ak sa rýchlosť zmenila)
  if (abs(gpsData.speed - oldSpeed) > 0.2) {
    drawNeedle(gpsData.speed);
    drawCenterSpeed(gpsData.speed);
    oldSpeed = gpsData.speed;
  }

  // 2. Štatistiky (Obnovujeme každých 500ms aby neblikali)
  static unsigned long lastStatsUpdate = 0;
  if (millis() - lastStatsUpdate > 500) {
    drawStatsPanel();
    drawStatusBar();
    lastStatsUpdate = millis();
  }
}
