/*
 * ============================================================
 *  GPS Tracker — ESP32 + SD + OLED + Buzzer
 *  Verzia: 5.1 STABLE  |  S blikaním satelitov 0-3
 * ============================================================
 */

#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>

// ============================================================
//  ⚙️ KONFIGURÁCIA
// ============================================================

#define MIN_SPEED_KMH     0.8f
#define MAX_HDOP          2.5f
#define MIN_SATS          5
#define MIN_DIST_M        5.0f
#define LOG_INTERVAL_MS   2000

#define DISPLAY_INTERVAL_MS   200
#define UART_CHECK_INTERVAL_MS 1000
#define SD_WATCHDOG_INTERVAL_MS 10000
#define MOVEMENT_DEBOUNCE_MS  3000
#define BLINK_INTERVAL_MS     400  // Rýchlosť blikania (ms)

#define GPS_RX            16
#define GPS_TX            17
#define SD_CS             5
#define BUZZER_PIN        4

#define SCREEN_WIDTH      128
#define SCREEN_HEIGHT     64
#define OLED_RESET        -1
#define OLED_ADDR         0x3C

#define CSV_HEADER        "lat,lon,alt_m,time,speed_kmh,hdop,satellites\n"
#define MAX_FILE_SIZE_KB  2048
#define CSV_FLUSH_EVERY   5

#define DEBUG_SERIAL      true
#define DEBUG_NMEA        false

// ============================================================
//  GLOBÁLNE PREMENNÉ
// ============================================================

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
TinyGPSPlus gps;
HardwareSerial SerialGPS(1);

struct SystemState {
  bool oledReady = false;
  bool sdReady = false;
  bool gpsUartOk = false;
  bool gpsFix = false;
  bool isMoving = false;
  bool movingDebounce = false;
  uint8_t errorFlags = 0;
} sys;

struct GPSData {
  double lat = 0, lng = 0, alt = 0;
  float speed = 0, hdop = 99.9;
  uint8_t sats = 0;
  uint16_t year = 0;
  uint8_t month = 0, day = 0, hour = 0, minute = 0, second = 0;
  bool valid = false;
} gpsData;

struct TrackStats {
  char fileName[22] = "/track0001.csv";
  uint32_t pointCount = 0;
  uint32_t skippedNoise = 0;
  uint32_t writeCounter = 0;
  float totalDistM = 0;
  uint32_t lastFileCheck = 0;
} track;

struct LastPos {
  double lat = 0, lng = 0;
  bool has = false;
} lastPos;

struct Timers {
  unsigned long lastWrite = 0;
  unsigned long lastDisplay = 0;
  unsigned long lastUartCheck = 0;
  unsigned long lastSDCheck = 0;
  unsigned long lastMovementChange = 0;
  unsigned long bootTime = 0;
  unsigned long lastValidGPS = 0;
  unsigned long lastBlink = 0;  // Pre blikanie satelitov
} timers;

struct DisplayBuffer {
  char timeStr[6] = "--:--";
  char dateStr[11] = "--.--.----";
  char speedStr[10] = "0.0km/h";
  char satStr[12] = "S:0";
  char hdopStr[10] = "H:-";
  char distStr[12] = "0m";
  char statusStr[8] = "GPS";
  char skipStr[12] = "";
  bool needsRedraw = true;
  bool blinkVisible = true;  // Stav blikania
} disp;

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

int8_t getUTCOffset() {
  if (gpsData.year < 2020 || gpsData.month == 0) return 1;
  
  if (gpsData.month > 3 && gpsData.month < 10) return 2;
  if (gpsData.month == 3) {
    int lastSun = 31;
    for (int d = 31; d >= 25; d--) {
      int y = gpsData.year, m = 3, q = d;
      if (m < 3) { m += 12; y--; }
      int h = (q + ((13*(m+1))/5) + y + (y/4) - (y/100) + (y/400)) % 7;
      if (h == 1) { lastSun = d; break; }
    }
    return (gpsData.day >= lastSun && gpsData.hour >= 1) ? 2 : 1;
  }
  if (gpsData.month == 10) {
    int lastSun = 31;
    for (int d = 31; d >= 25; d--) {
      int y = gpsData.year, m = 10, q = d;
      if (m < 3) { m += 12; y--; }
      int h = (q + ((13*(m+1))/5) + y + (y/4) - (y/100) + (y/400)) % 7;
      if (h == 1) { lastSun = d; break; }
    }
    return (gpsData.day < lastSun || (gpsData.day == lastSun && gpsData.hour < 1)) ? 2 : 1;
  }
  return 1;
}

void safeStrCopy(char *dest, const char *src, size_t maxLen) {
  strncpy(dest, src, maxLen - 1);
  dest[maxLen - 1] = '\0';
}

// ============================================================
//  HARDWARE INIT
// ============================================================

bool initOLED() {
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) return false;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);
  return true;
}

bool initSD() {
  if (!SD.begin(SD_CS, SPI, 40000000)) return false;
  File test = SD.open("/.test", FILE_WRITE);
  if (!test) return false;
  test.println("OK");
  test.close();
  SD.remove("/.test");
  return true;
}

void initBuzzer() {
  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN);
}

// ============================================================
//  GPS PROCESSING
// ============================================================

void processGPSData() {
  if (!gps.location.isValid() || !gps.time.isValid()) {
    gpsData.valid = false;
    return;
  }

  gpsData.lat = gps.location.lat();
  gpsData.lng = gps.location.lng();
  gpsData.alt = gps.altitude.isValid() ? gps.altitude.meters() : 0;
  gpsData.speed = gps.speed.isValid() ? gps.speed.kmph() : 0;
  gpsData.hdop = gps.hdop.isValid() ? gps.hdop.hdop() : 99.9;
  gpsData.sats = gps.satellites.isValid() ? (uint8_t)gps.satellites.value() : 0;
  
  gpsData.year = gps.date.year();
  gpsData.month = gps.date.month();
  gpsData.day = gps.date.day();
  
  int8_t utcOff = getUTCOffset();
  int h = ((int)gps.time.hour() + utcOff) % 24;
  if (h < 0) h += 24;
  gpsData.hour = (uint8_t)h;
  gpsData.minute = (uint8_t)gps.time.minute();
  gpsData.second = (uint8_t)gps.time.second();
  
  gpsData.valid = true;
  timers.lastValidGPS = millis();
  
  snprintf(disp.timeStr, sizeof(disp.timeStr), "%02d:%02d", gpsData.hour, gpsData.minute);
  snprintf(disp.dateStr, sizeof(disp.dateStr), "%02d.%02d.%04d", 
           gpsData.day, gpsData.month, gpsData.year);
}

// ============================================================
//  GPS NOISE FILTER
// ============================================================

bool passesNoiseFilter(float speed, float hdop, uint8_t sats, 
                       double lat, double lng, float *outDist) {
  if (speed < MIN_SPEED_KMH) return false;
  if (hdop > MAX_HDOP) return false;
  if (sats < MIN_SATS) return false;
  
  if (lastPos.has) {
    float dist = haversineM(lastPos.lat, lastPos.lng, lat, lng);
    if (outDist) *outDist = dist;
    if (dist < MIN_DIST_M) return false;
  } else if (outDist) {
    *outDist = 0;
  }
  
  return true;
}

// ============================================================
//  CSV OPERATIONS
// ============================================================

void createNewCSVFile() {
  for (int i = 1; i <= 9999; i++) {
    snprintf(track.fileName, sizeof(track.fileName), "/track%04d.csv", i);
    if (!SD.exists(track.fileName)) break;
  }
  
  File f = SD.open(track.fileName, FILE_WRITE);
  if (!f) {
    sys.errorFlags |= 0x02;
    return;
  }
  f.print(CSV_HEADER);
  f.close();
  track.pointCount = 0;
  track.writeCounter = 0;
  sys.sdReady = true;
}

bool writeCSVPoint(double lat, double lng, double alt,
                   uint8_t h, uint8_t m, uint8_t s,
                   float spd, float hdop, uint8_t sats) {
  if (!sys.sdReady) return false;
  
  File f = SD.open(track.fileName, FILE_APPEND);
  if (!f) {
    sys.errorFlags |= 0x02;
    return false;
  }
  
  f.printf("%.7f,%.7f,%.1f,%02d:%02d:%02d,%.2f,%.2f,%d\n",
           lat, lng, alt, h, m, s, spd, hdop, sats);
  f.close();
  
  track.writeCounter++;
  if (track.writeCounter >= CSV_FLUSH_EVERY) {
    track.writeCounter = 0;
  }
  
  return true;
}

void checkFileSize() {
  if (millis() - track.lastFileCheck < SD_WATCHDOG_INTERVAL_MS) return;
  track.lastFileCheck = millis();
  
  File f = SD.open(track.fileName, FILE_READ);
  if (!f) return;
  
  uint32_t size = f.size();
  f.close();
  
  if (size >= (MAX_FILE_SIZE_KB * 1024)) {
    createNewCSVFile();
  }
}

// ============================================================
//  DISPLAY UPDATE — S BLIKANÍM SATELITOV 0-3
// ============================================================

void updateBlinkState() {
  // Blikanie každých BLINK_INTERVAL_MS
  if (millis() - timers.lastBlink >= BLINK_INTERVAL_MS) {
    timers.lastBlink = millis();
    disp.blinkVisible = !disp.blinkVisible;  // Prepnúť stav
  }
}

void updateDisplayBuffer() {
  // Aktualizácia blikania
  updateBlinkState();
  
  if (!sys.gpsUartOk) {
    safeStrCopy(disp.statusStr, "GPS", sizeof(disp.statusStr));
  } else if (!sys.gpsFix) {
    safeStrCopy(disp.statusStr, "HLDAM", sizeof(disp.statusStr));
  } else {
    safeStrCopy(disp.statusStr, sys.isMoving ? "REC" : "STOP", sizeof(disp.statusStr));
  }
  
  if (gpsData.valid) {
    if (gpsData.speed >= 1.0f) {
      snprintf(disp.speedStr, sizeof(disp.speedStr), "%.1f", gpsData.speed);
    } else {
      safeStrCopy(disp.speedStr, "0.0", sizeof(disp.speedStr));
    }
    
    // BLIKANIE: 0-3 satelity = bliká, 4+ = stabilné
    // VŽDY rovnaká dĺžka: "S:X " (4 znaky vrátane medzery)
    if (gpsData.sats <= 3) {
      if (disp.blinkVisible) {
        snprintf(disp.satStr, sizeof(disp.satStr), "S:%d ", gpsData.sats);  // Vždy 4 znaky
      } else {
        snprintf(disp.satStr, sizeof(disp.satStr), "    ", 4);  // 4 medzery
      }
    } else {
      // 4 a viac satelitov = vždy viditeľné
      snprintf(disp.satStr, sizeof(disp.satStr), "S:%d ", gpsData.sats);  // Vždy 4 znaky
    }
    
    snprintf(disp.hdopStr, sizeof(disp.hdopStr), "H%.1f", gpsData.hdop);
  } else {
    safeStrCopy(disp.speedStr, "--.-", sizeof(disp.speedStr));
    
    // Žiadny signál = bliká "S:0 " (vždy 4 znaky)
    if (disp.blinkVisible) {
      snprintf(disp.satStr, sizeof(disp.satStr), "S:0 ", 4);
    } else {
      snprintf(disp.satStr, sizeof(disp.satStr), "    ", 4);
    }
    
    safeStrCopy(disp.hdopStr, "H--", sizeof(disp.hdopStr));
  }
  
  if (track.totalDistM < 1000.0f) {
    snprintf(disp.distStr, sizeof(disp.distStr), "%.0fm", track.totalDistM);
  } else {
    snprintf(disp.distStr, sizeof(disp.distStr), "%.1fk", track.totalDistM / 1000.0f);
  }
  
  if (track.skippedNoise > 0) {
    snprintf(disp.skipStr, sizeof(disp.skipStr), "-%lu", track.skippedNoise);
  } else {
    disp.skipStr[0] = '\0';
  }
  
  disp.needsRedraw = true;
}

void renderDisplay() {
  if (!sys.oledReady || !disp.needsRedraw) return;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);

  // 1. ČAS (veľký)
  display.setTextSize(3);
  display.setCursor(0, 0);
  display.print(disp.timeStr);

  // 2. STATUS (pravý horný)
  display.setTextSize(1);
  int sx = SCREEN_WIDTH - (strlen(disp.statusStr) * 6) - 2;
  display.setCursor(sx, 4);
  display.print(disp.statusStr);

  // 3. DÁTUM
  display.setTextSize(1);
  display.setCursor(0, 26);
  display.print(disp.dateStr);

  // 4. RÝCHLOSŤ
  display.setTextSize(2);
  display.setCursor(0, 38);
  display.print(disp.speedStr);
  display.setTextSize(1);
  display.print(" km/h");

  // 5. SATELITY + HDOP (ľavý spodok)
  display.setCursor(0, 52);
  display.print(disp.satStr);  // Toto môže byť "S:3" alebo "    " pri blikaní
  display.print("  ");
  display.print(disp.hdopStr);

  // 6. BODY + VZDIALENOSŤ (pravý spodok)
  char info[20];
  snprintf(info, sizeof(info), "%luB %s", track.pointCount, disp.distStr);
  int ix = SCREEN_WIDTH - (strlen(info) * 6) - 2;
  display.setCursor(ix, 52);
  display.print(info);

  display.display();
  disp.needsRedraw = false;
}

// ============================================================
//  MOVEMENT DETECTION
// ============================================================

void updateMovementState(float speed) {
  unsigned long now = millis();
  bool currentlyMoving = (speed >= MIN_SPEED_KMH);
  
  if (currentlyMoving != sys.movingDebounce) {
    if (now - timers.lastMovementChange >= MOVEMENT_DEBOUNCE_MS) {
      sys.isMoving = currentlyMoving;
      sys.movingDebounce = currentlyMoving;
      timers.lastMovementChange = now;
      disp.needsRedraw = true;
    }
  } else {
    timers.lastMovementChange = now;
  }
}

// ============================================================
//  ERROR HANDLING
// ============================================================

void handleErrors() {
  if (sys.errorFlags & 0x02) {
    static unsigned long lastRetry = 0;
    if (millis() - lastRetry > 5000) {
      lastRetry = millis();
      if (initSD()) {
        sys.errorFlags &= ~0x02;
        createNewCSVFile();
      }
    }
  }
  
  if (sys.gpsUartOk && millis() - timers.lastValidGPS > 30000) {
    sys.gpsUartOk = false;
    sys.gpsFix = false;
    disp.needsRedraw = true;
  }
}

void signalBeep(uint16_t freq, uint16_t duration, uint8_t repeats) {
  for (uint8_t i = 0; i < repeats; i++) {
    tone(BUZZER_PIN, freq);
    delay(duration);
    noTone(BUZZER_PIN);
    if (i < repeats - 1) delay(50);
  }
}

// ============================================================
//  SETUP
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(50);
  
  if (DEBUG_SERIAL) {
    Serial.println(F("\n=== GPS Tracker v5.1 STABLE ==="));
  }
  
  timers.bootTime = millis();
  
  initBuzzer();
  
  sys.oledReady = initOLED();
  if (sys.oledReady && DEBUG_SERIAL) Serial.println(F("[OLED] OK"));
  
  sys.sdReady = initSD();
  if (sys.sdReady) {
    if (DEBUG_SERIAL) Serial.println(F("[SD] OK"));
    createNewCSVFile();
  } else {
    if (DEBUG_SERIAL) Serial.println(F("[SD] FAIL"));
    if (sys.oledReady) {
      display.clearDisplay();
      display.setCursor(0, 0);
      display.print(F("SD ERROR!"));
      display.display();
    }
  }
  
  SerialGPS.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
  
  if (sys.oledReady) {
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(10, 8);
    display.print(F("GPS"));
    display.setCursor(10, 32);
    display.print(F("TRACKER"));
    display.setTextSize(1);
    display.setCursor(2, 54);
    display.print(F("v5.1 READY"));
    display.display();
    delay(800);
  }
  
  signalBeep(2000, 50, 2);
}

// ============================================================
//  LOOP
// ============================================================

void loop() {
  unsigned long now = millis();
  
  // 1. Čítaj GPS
  while (SerialGPS.available()) {
    char c = SerialGPS.read();
    if (DEBUG_NMEA && DEBUG_SERIAL) Serial.write(c);
    if (gps.encode(c)) {
      processGPSData();
    }
  }
  
  // 2. UART watchdog
  if (now - timers.lastUartCheck >= UART_CHECK_INTERVAL_MS) {
    timers.lastUartCheck = now;
    static uint32_t lastCharCount = 0;
    uint32_t currentCount = gps.location.isValid() ? 1 : 0;
    
    if (currentCount > lastCharCount && !sys.gpsUartOk) {
      sys.gpsUartOk = true;
      if (DEBUG_SERIAL) Serial.println(F("[GPS] UART OK"));
    }
    lastCharCount = currentCount;
  }
  
  // 3. GPS fix detection
  if (sys.gpsUartOk && gpsData.valid && !sys.gpsFix) {
    if (gpsData.sats >= MIN_SATS && gpsData.hdop <= MAX_HDOP) {
      sys.gpsFix = true;
      if (DEBUG_SERIAL) {
        Serial.printf("[GPS] FIX! Sats:%d HDOP:%.1f\n", gpsData.sats, gpsData.hdop);
      }
      signalBeep(2200, 70, 3);
    }
  }
  
  // 4. Zápis bodu
  if (sys.gpsFix && sys.sdReady && gpsData.valid && 
      (now - timers.lastWrite >= LOG_INTERVAL_MS)) {
    
    float dist = 0;
    if (passesNoiseFilter(gpsData.speed, gpsData.hdop, gpsData.sats,
                          gpsData.lat, gpsData.lng, &dist)) {
      
      if (writeCSVPoint(gpsData.lat, gpsData.lng, gpsData.alt,
                        gpsData.hour, gpsData.minute, gpsData.second,
                        gpsData.speed, gpsData.hdop, gpsData.sats)) {
        
        if (lastPos.has) track.totalDistM += dist;
        lastPos.lat = gpsData.lat;
        lastPos.lng = gpsData.lng;
        lastPos.has = true;
        
        timers.lastWrite = now;
        track.pointCount++;
        
        if (DEBUG_SERIAL) {
          Serial.printf("[REC] #%lu %.6f,%.6f %.0fm %.1fkm/h H%.1f S%d +%.0fm\n",
                        track.pointCount, gpsData.lat, gpsData.lng,
                        gpsData.alt, gpsData.speed, gpsData.hdop, 
                        gpsData.sats, dist);
        }
      }
    } else {
      track.skippedNoise++;
    }
  }
  
  // 5. Movement state
  if (gpsData.valid) {
    updateMovementState(gpsData.speed);
  }
  
  // 6. Display update (každých 200ms)
  if (now - timers.lastDisplay >= DISPLAY_INTERVAL_MS) {
    timers.lastDisplay = now;
    updateDisplayBuffer();
    renderDisplay();
  }
  
  // 7. File size check
  checkFileSize();
  
  // 8. Error handling
  handleErrors();
}
