/*
 * ============================================================
 *  GPS Tracker — ESP32 + SD + OLED + Buzzer
 *  Verzia: 3.0  |  Terén, bicykel, turistika
 * ============================================================
 *
 *  ZMENY v 3.0:
 *   ✅ Formát CSV — súbor je VŽDY platný, aj po vypnutí bez tlačidla
 *   ✅ GPS NOISE FILTER — nezapíše bod ak stojíš (rýchlosť < 1 km/h)
 *   ✅ HDOP filter — zapisuje len keď je GPS presný (HDOP < 2.5)
 *   ✅ Minimálne satelity zvýšené na 5
 *   ✅ Minimálna vzdialenosť zvýšená na 5 m
 *   ✅ UTC offset opravený + komentár pre letný/zimný čas
 *   ✅ BOOT tlačidlo odstránené — vypni ESP32 kedykoľvek
 *   ✅ OLED zobrazuje HDOP, rýchlosť, presnosť v reálnom čase
 *   ✅ Automatická detekcia pohybu
 *
 *  ZAPOJENIE ESP32-WROOM-32U:
 *   GPS  TX → GPIO16  |  GPS  RX → GPIO17
 *   SD   CS → GPIO5   |  MOSI → GPIO23  |  MISO → GPIO19  |  SCK → GPIO18
 *   OLED SCL → GPIO22 |  OLED SDA → GPIO21
 *   Buzzer  → GPIO4
 *
 *  VÝSTUPNÝ SÚBOR: /track0001.csv
 *   Formát riadku: lat,lon,alt_m,HH:MM:SS,speed_kmh,hdop,satelity
 *   Príklad: 48.123456,17.654321,312.5,14:32:15,4.2,1.2,8
 *
 *  GPS NOISE FILTERING — prečo záznamy bez pohybu:
 *   NEO-6M v interiéri / slabý signál → GPS "blúdi" aj keď stojíš.
 *   Riešenie: zapisujeme BOD len ak VŠETKY podmienky splnené:
 *     1. GPS rýchlosť > MIN_SPEED_KMH  (0.8 km/h)
 *     2. HDOP < MAX_HDOP               (2.5 = dobrá presnosť)
 *     3. Satelity >= MIN_SATS          (5)
 *     4. Pohyb od posledného bodu >= MIN_DIST_M (5 m)
 *     5. Interval >= LOG_INTERVAL_MS   (2 sekundy)
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
//  ⚙️  KONFIGURÁCIA — uprav podľa potreby
// ============================================================

// Časová zóna:
//   Zimný čas (október–marec): UTC_OFFSET = 1
//   Letný čas (apríl–október): UTC_OFFSET = 2
#define UTC_OFFSET        1

// GPS filter — NE-zapisuj bod ak:
#define MIN_SPEED_KMH     0.8f    // rýchlosť pod túto hodnotu = stojíš
#define MAX_HDOP          2.5f    // HDOP nad túto hodnotu = zlá presnosť
#define MIN_SATS          5       // menej satelitov = nespoľahlivý fix
#define MIN_DIST_M        5.0f    // pohyb menej ako 5 m = ignoruj
#define LOG_INTERVAL_MS   2000    // minimálny čas medzi bodmi (ms)

// Piny
#define GPS_RX            16
#define GPS_TX            17
#define SD_CS              5
#define BUZZER_PIN         4

// OLED
#define SCREEN_WIDTH      128
#define SCREEN_HEIGHT      64
#define OLED_RESET         -1

// Debug — vypni ak nechceš NMEA výpis na Serial
#define DEBUG_SERIAL      true
// ============================================================

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool oledOK = false;

TinyGPSPlus gps;
HardwareSerial SerialGPS(1);

// --- Stav ---
char     csvFileName[22];
bool     csvReady        = false;
bool     gpsUartOK       = false;
bool     fixAnnounced    = false;
bool     isMoving        = false;

// --- Štatistiky ---
uint32_t pointCount      = 0;
uint32_t skippedNoise    = 0;   // počet zamietnutých bodov (šum)
float    totalDistM      = 0.0f;

// --- Posledná pozícia ---
double   lastLat         = 0.0;
double   lastLng         = 0.0;
bool     hasLastPos      = false;

// --- Časovače ---
unsigned long lastWriteMs    = 0;
unsigned long lastDisplayMs  = 0;
unsigned long lastUartCheck  = 0;
unsigned long bootMs         = 0;
unsigned long uartCharCount  = 0;

// --- Zobrazenie ---
String   dispTime        = "--:--";
String   dispDate        = "--.--.----";
bool     hasTime         = false;

// ============================================================
//  HAVERSINE — vzdialenosť v metroch
// ============================================================
float haversineM(double la1, double lo1, double la2, double lo2) {
  const float R = 6371000.0f;
  float dLa = radians((float)(la2 - la1));
  float dLo = radians((float)(lo2 - lo1));
  float a   = sinf(dLa*.5f)*sinf(dLa*.5f)
            + cosf(radians((float)la1))*cosf(radians((float)la2))
            * sinf(dLo*.5f)*sinf(dLo*.5f);
  return R * 2.0f * atan2f(sqrtf(a), sqrtf(1.0f - a));
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("\n========================================"));
  Serial.println(F("  GPS TRACKER v3.0  |  CSV Noise Filter"));
  Serial.println(F("========================================"));
  Serial.printf("  UTC offset: +%d\n", UTC_OFFSET);
  Serial.printf("  Min rychlost: %.1f km/h\n", MIN_SPEED_KMH);
  Serial.printf("  Max HDOP: %.1f\n", MAX_HDOP);
  Serial.printf("  Min satelity: %d\n", MIN_SATS);
  Serial.printf("  Min vzdialenost: %.0f m\n\n", MIN_DIST_M);

  SerialGPS.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
  bootMs = millis();

  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN);

  // OLED
  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    oledOK = true;
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    bootScreen();
    Serial.println(F("[OLED] OK"));
  } else {
    Serial.println(F("[OLED] Nenajdene, pokracujem bez displeja"));
  }

  // SD
  Serial.print(F("[SD] Inicializujem... "));
  if (!SD.begin(SD_CS)) {
    Serial.println(F("CHYBA! Skontroluj: CS=5 MOSI=23 MISO=19 SCK=18"));
    signalError();
    oledMsg(F("SD CHYBA!"), F("CS=5 MOSI=23"), F("MISO=19 SCK=18"));
    while (true) delay(1000);
  }
  Serial.println(F("OK"));

  // Vytvor CSV súbor
  createCSVFile();

  Serial.println(F("[GPS] Cakam na UART signal z NEO-6M..."));
  Serial.println(F("[GPS] Ak nevidis NMEA vety, zamen TX<->RX kable!\n"));
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  unsigned long now = millis();

  // --- Čítaj GPS ---
  while (SerialGPS.available()) {
    char c = SerialGPS.read();
    uartCharCount++;
    if (DEBUG_SERIAL) Serial.write(c);
    gps.encode(c);
  }

  // --- UART watchdog (každých 5s) ---
  if (now - lastUartCheck > 5000) {
    lastUartCheck = now;
    if (uartCharCount == 0 && !gpsUartOK) {
      Serial.println(F("[GPS] ZIADNE DATA! Zamen TX<->RX alebo skontroluj 3.3V napajanie."));
      oledNoUart(now);
    } else if (uartCharCount > 0 && !gpsUartOK) {
      gpsUartOK = true;
      Serial.println(F("\n[GPS] UART OK — prijinam NMEA vety"));
    }
    uartCharCount = 0;
  }

  // --- Aktualizuj čas (aj bez plného fixu) ---
  refreshTime();

  // --- GPS fix announcement ---
  if (gps.satellites.isValid()
   && (int)gps.satellites.value() >= MIN_SATS
   && gps.hdop.isValid()
   && gps.hdop.hdop() <= MAX_HDOP
   && !fixAnnounced) {
    fixAnnounced = true;
    signalFix();
    Serial.printf("[GPS] FIX! Sat:%d HDOP:%.1f\n",
                  (int)gps.satellites.value(), gps.hdop.hdop());
  }

  // --- Zápis bodu ---
  if (now - lastWriteMs >= (unsigned long)LOG_INTERVAL_MS) {
    tryLogPoint(now);
  }

  // --- OLED refresh ---
  if (now - lastDisplayMs > 1500) {
    lastDisplayMs = now;
    refreshDisplay(now);
  }
}

// ============================================================
//  GPS BOD — filter + zápis
// ============================================================
void tryLogPoint(unsigned long now) {
  // --- Základná validácia ---
  if (!gps.location.isValid()
   || !gps.altitude.isValid()
   || !gps.time.isValid()
   || !gps.speed.isValid()
   || !gps.hdop.isValid()
   || !gps.satellites.isValid()) {
    return;
  }

  double lat  = gps.location.lat();
  double lng  = gps.location.lng();
  double alt  = gps.altitude.meters();
  float  spd  = gps.speed.kmph();
  float  hdop = gps.hdop.hdop();
  int    sats = (int)gps.satellites.value();

  // --- Ochrana pred nulovými súradnicami ---
  if (lat == 0.0 || lng == 0.0 || isnan(lat) || isnan(lng)) return;

  // ════════════════════════════════════════
  //  GPS NOISE FILTER
  // ════════════════════════════════════════

  // 1) Rýchlosť — primárny filter šumu
  //    GPS šum spôsobuje "pohyb" aj keď stojíš
  //    ale rýchlosť zostáva blízko nuly
  if (spd < MIN_SPEED_KMH) {
    skippedNoise++;
    isMoving = false;
    return;
  }

  // 2) Presnosť HDOP
  //    < 1.0 = výborná  |  1–2 = dobrá  |  2–5 = prijateľná  |  > 5 = zlá
  if (hdop > MAX_HDOP) {
    skippedNoise++;
    return;
  }

  // 3) Počet satelitov
  if (sats < MIN_SATS) {
    skippedNoise++;
    return;
  }

  // 4) Vzdialenosť od posledného bodu
  float dist = 0.0f;
  if (hasLastPos) {
    dist = haversineM(lastLat, lastLng, lat, lng);
    if (dist < MIN_DIST_M) {
      skippedNoise++;
      return;
    }
  }

  // ════════════════════════════════════════
  //  VŠETKY FILTRE PREŠLI — zapíš bod
  // ════════════════════════════════════════
  isMoving = true;

  int h = ((int)gps.time.hour() + UTC_OFFSET) % 24;
  int m = (int)gps.time.minute();
  int s = (int)gps.time.second();

  if (writeCSVPoint(lat, lng, alt, h, m, s, spd, hdop, sats)) {
    if (hasLastPos) totalDistM += dist;
    lastLat = lat; lastLng = lng; hasLastPos = true;
    lastWriteMs = now;
    pointCount++;

    Serial.printf("[BOD %4lu] %.6f,%.6f  Alt:%.0fm  Spd:%.1fkm/h"
                  "  HDOP:%.1f  Sat:%d  Dist:%.0fm\n",
                  pointCount, lat, lng, alt, spd, hdop, sats,
                  hasLastPos ? dist : 0.0f);
  }
}

// ============================================================
//  ČAS
// ============================================================
void refreshTime() {
  if (!gps.time.isValid()) return;
  int h = ((int)gps.time.hour() + UTC_OFFSET) % 24;
  char t[6];
  snprintf(t, sizeof(t), "%02d:%02d", h, (int)gps.time.minute());
  dispTime = String(t);
  hasTime  = true;

  if (gps.date.isValid() && gps.date.year() > 2000) {
    char d[11];
    snprintf(d, sizeof(d), "%02d.%02d.%d",
             (int)gps.date.day(), (int)gps.date.month(), (int)gps.date.year());
    dispDate = String(d);
  }
}

// ============================================================
//  CSV SÚBOR
// ============================================================
void createCSVFile() {
  for (int i = 1; i <= 9999; i++) {
    snprintf(csvFileName, sizeof(csvFileName), "/track%04d.csv", i);
    if (!SD.exists(csvFileName)) {
      Serial.printf("[SD] Novy subor: %s\n", csvFileName);
      break;
    }
  }
  // Záhlavie CSV
  File f = SD.open(csvFileName, FILE_WRITE);
  if (f) {
    f.println(F("lat,lon,alt_m,time,speed_kmh,hdop,satellites"));
    f.close();
    csvReady = true;
    Serial.println(F("[SD] CSV subor vytvoreny s hlavickou."));
  } else {
    Serial.println(F("[SD] Chyba pri vytvarani CSV!"));
    signalError();
    oledMsg(F("CSV CHYBA!"), F(""), F(""));
    while (true) delay(1000);
  }
}

bool writeCSVPoint(double lat, double lng, double alt,
                   int h, int m, int s,
                   float spd, float hdop, int sats) {
  if (!csvReady) return false;
  File f = SD.open(csvFileName, FILE_APPEND);
  if (!f) {
    Serial.println(F("[SD] Chyba pri zapise!"));
    signalWriteErr();
    return false;
  }
  // lat,lon,alt_m,HH:MM:SS,speed,hdop,sats
  f.printf("%.7f,%.7f,%.1f,%02d:%02d:%02d,%.2f,%.2f,%d\n",
           lat, lng, alt, h, m, s, spd, hdop, sats);
  f.close();
  return true;
}

// ============================================================
//  OLED
// ============================================================
void bootScreen() {
  if (!oledOK) return;
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(8, 4);  display.println(F("GPS"));
  display.setCursor(8, 24); display.println(F("TRACKER"));
  display.setTextSize(1);
  display.setCursor(2, 50); display.println(F("v3.0  Inicializujem..."));
  display.display();
  delay(900);
}

void oledMsg(const __FlashStringHelper* l1,
             const __FlashStringHelper* l2,
             const __FlashStringHelper* l3) {
  if (!oledOK) return;
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0,  4); display.println(l1);
  display.setCursor(0, 22); display.println(l2);
  display.setCursor(0, 38); display.println(l3);
  display.display();
}

void oledNoUart(unsigned long now) {
  if (!oledOK) return;
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0,  0); display.println(F("GPS: ZIADNE DATA!"));
  display.setCursor(0, 12); display.println(F("Skontroluj TX/RX:"));
  display.setCursor(0, 24); display.println(F("TX modulu -> GPIO16"));
  display.setCursor(0, 36); display.println(F("RX modulu -> GPIO17"));
  display.setCursor(0, 50); display.println(F("Napajanie: 3.3V!"));
  display.display();
}

void refreshDisplay(unsigned long now) {
  if (!oledOK) return;
  display.clearDisplay();

  int  sats  = gps.satellites.isValid() ? (int)gps.satellites.value() : 0;
  float hdop = gps.hdop.isValid()       ? gps.hdop.hdop()             : 99.0f;
  float spd  = gps.speed.isValid()      ? gps.speed.kmph()            : 0.0f;
  bool  good = fixAnnounced && hdop <= MAX_HDOP && sats >= MIN_SATS;

  if (!gpsUartOK) {
    // ── Žiadny UART ──────────────────────────
    display.setTextSize(1);
    display.setCursor(0, 0); display.println(F("GPS: ZIADNY SIGNAL"));
    display.setCursor(0,14); display.println(F("TX->GPIO16"));
    display.setCursor(0,24); display.println(F("RX->GPIO17"));
    char buf[18];
    snprintf(buf, sizeof(buf), "Cakam: %lus", (now-bootMs)/1000);
    display.setCursor(0, 50); display.print(buf);

  } else if (!good) {
    // ── Hľadám fix ───────────────────────────
    display.setTextSize(1);
    display.setCursor(0, 0); display.println(F("Hladam satelity..."));

    // Satelitný bar
    char satLine[20];
    snprintf(satLine, sizeof(satLine), "Sat: %d/%d  HDOP:%.1f", sats, MIN_SATS, hdop < 99 ? hdop : 0.0f);
    display.setCursor(0, 13); display.print(satLine);

    // Vizuálne bloky
    for (int i = 0; i < min(sats, 10); i++)
      display.fillRect(i*12, 25, 10, 8, SSD1306_WHITE);
    for (int i = sats; i < MIN_SATS; i++)
      display.drawRect(i*12, 25, 10, 8, SSD1306_WHITE);

    if (hasTime) {
      display.setCursor(0, 38);
      display.print(F("Cas: ")); display.print(dispTime);
    }

    unsigned long sec = (now - bootMs) / 1000;
    char tbuf[18];
    if (sec < 60) snprintf(tbuf, sizeof(tbuf), "Cakam: %lus", sec);
    else          snprintf(tbuf, sizeof(tbuf), "Cakam: %lum%lus", sec/60, sec%60);
    display.setCursor(0, 52); display.print(tbuf);

  } else {
    // ── Fix OK — hlavné zobrazenie ────────────

    // Čas veľký
    display.setTextSize(3);
    display.setCursor(8, 0);
    display.print(dispTime);

    // Rýchlosť vedľa času (malá)
    display.setTextSize(1);
    char spdBuf[10];
    if (spd >= 1.0f) snprintf(spdBuf, sizeof(spdBuf), "%.1fkm/h", spd);
    else             strcpy(spdBuf, "STOJI");
    display.setCursor(SCREEN_WIDTH - (int)strlen(spdBuf)*6, 2);
    display.print(spdBuf);

    // Dátum
    display.setTextSize(1);
    display.setCursor(0, 26); display.print(dispDate);

    // Satelity + HDOP
    char satHdop[20];
    const char* hdopTxt = hdop<=1.0f?"Excel":hdop<=2.0f?"Dobre":hdop<=3.5f?"OK":"Slabo";
    snprintf(satHdop, sizeof(satHdop), "S%d %s", sats, hdopTxt);
    display.setCursor(0, 38); display.print(satHdop);

    // Pohyb indikátor
    display.setCursor(SCREEN_WIDTH - 36, 38);
    display.print(isMoving ? F("[ REC]") : F("[STOP]"));

    // Body + vzdialenosť
    char ptBuf[22];
    if (totalDistM < 1000.0f)
      snprintf(ptBuf, sizeof(ptBuf), "%lub  %.0fm", pointCount, totalDistM);
    else
      snprintf(ptBuf, sizeof(ptBuf), "%lub %.2fkm", pointCount, totalDistM/1000.0f);
    display.setCursor(0, 52); display.print(ptBuf);

    // Zamietnuté šumové body
    if (skippedNoise > 0) {
      char skBuf[14];
      snprintf(skBuf, sizeof(skBuf), "skip:%lu", skippedNoise);
      display.setCursor(SCREEN_WIDTH - (int)strlen(skBuf)*6, 52);
      display.print(skBuf);
    }
  }

  display.display();
}

// ============================================================
//  ZVUKOVÉ SIGNÁLY
// ============================================================
void signalFix() {
  for (int i = 0; i < 3; i++) {
    tone(BUZZER_PIN, 2000); delay(90);
    noTone(BUZZER_PIN);     delay(90);
  }
}
void signalWriteErr() {
  for (int i = 0; i < 4; i++) {
    tone(BUZZER_PIN, 800); delay(70);
    noTone(BUZZER_PIN);    delay(70);
  }
}
void signalError() {
  tone(BUZZER_PIN, 400); delay(1500); noTone(BUZZER_PIN);
}
