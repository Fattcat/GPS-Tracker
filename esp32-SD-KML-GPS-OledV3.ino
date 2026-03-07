/*
 * ============================================================
 *  GPS Tracker — ESP32 + SD + OLED + Buzzer
 *  Verzia: 2.0  |  Opravená & Vylepšená
 * ============================================================
 *  OPRAVY:
 *   - KRITICKÁ: FILE_WRITE → FILE_APPEND v logGPS()
 *   - KRITICKÁ: finalizeKML() sa teraz volá pri stlačení tlačidla
 *   - Odstránený variable shadowing (lokálna kmlFile v logGPS)
 *   - UTC offset ako konfigurovateľná konštanta
 *   - Validácia GPS súradníc (ochrana pred 0,0 / NaN bodmi)
 *   - lastGPSWriteTime sa teraz správne aktualizuje
 *
 *  NOVÉ FUNKCIE:
 *   - Haversine filter — nový bod len ak sa pohneš ≥ MIN_DIST_M
 *   - Časovač medzi bodmi (LOG_INTERVAL_MS) — šetrí SD kartu
 *   - Počítadlo bodov + celková vzdialenosť na OLED
 *   - STOP tlačidlo (GPIO0 / BOOT) → správne ukončí KML
 *   - Štatistiky po ukončení záznamu na OLED
 *   - Rôzne zvukové signály (fix, strata signálu, koniec, chyba)
 *   - Boot obrazovka na OLED
 *   - F() makro pre konštantné stringy → úspora RAM
 *   - Záznam pokračuje aj bez OLED (OLED nie je blokujúci)
 *
 *  ZAPOJENIE:
 *   GPS TX → GPIO16  |  GPS RX → GPIO17
 *   SD  CS → GPIO5   |  MOSI → GPIO23  |  MISO → GPIO19  |  SCK → GPIO18
 *   OLED SCL → GPIO22  |  OLED SDA → GPIO21
 *   Buzzer → GPIO4
 *   STOP tlačidlo → GPIO0 (zabudovaný BOOT button)
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
//  KONFIGURÁCIA — uprav podľa potreby
// ============================================================
#define GPS_RX           16       // GPS modul TX → ESP32 RX
#define GPS_TX           17       // GPS modul RX → ESP32 TX
#define SD_CS             5       // SD karta Chip Select
#define BUZZER_PIN        4       // Pasívny bzučiak
#define STOP_BTN_PIN      0       // BOOT tlačidlo — ukončí záznam

#define UTC_OFFSET        2       // Letný čas: 2 | Zimný čas: 1
#define LOG_INTERVAL_MS   3000    // Min. čas medzi GPS bodmi (ms)
#define MIN_DIST_M        2.0f    // Min. pohyb pred novým bodom (metre)
#define DISPLAY_UPDATE_MS 3000    // Interval obnovy OLED (ms)
#define NO_FIX_WARN_MS    45000   // Čas bez GPS signálu pred upozornením (ms)
#define MIN_SATELLITES    4       // Minimálny počet satelitov pre platný fix
// ============================================================

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
#define OLED_RESET     -1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool oledAvailable = false;

TinyGPSPlus gps;
HardwareSerial SerialGPS(1);

// --- Stav záznamu ---
bool     kmlStarted      = false;
bool     gpsFixAnnounced = false;
bool     isRecording     = true;
char     kmlFileName[22];

// --- Štatistiky ---
uint32_t pointCount   = 0;
float    totalDistM   = 0.0f;

// --- Posledná zapísaná pozícia (pre Haversine filter) ---
double   lastLat    = 0.0;
double   lastLng    = 0.0;
bool     hasLastPos = false;

// --- Časovače ---
unsigned long lastGPSWriteTime  = 0;
unsigned long lastWarningTime   = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastBtnCheck      = 0;

// --- Zobrazenie času/dátumu ---
String localTimeStr = "--:--";
String localDateStr = "--.--.----";

// ============================================================
//  HAVERSINE — vzdialenosť dvoch GPS bodov v metroch
// ============================================================
float haversineM(double lat1, double lon1, double lat2, double lon2) {
  const float R = 6371000.0f;
  float dLat = radians((float)(lat2 - lat1));
  float dLon = radians((float)(lon2 - lon1));
  float a = sinf(dLat * 0.5f) * sinf(dLat * 0.5f)
          + cosf(radians((float)lat1)) * cosf(radians((float)lat2))
          * sinf(dLon * 0.5f) * sinf(dLon * 0.5f);
  return R * 2.0f * atan2f(sqrtf(a), sqrtf(1.0f - a));
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  SerialGPS.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);

  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN);
  pinMode(STOP_BTN_PIN, INPUT_PULLUP);

  // OLED — neblokujúci init
  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    oledAvailable = true;
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    showBootScreen();
  } else {
    Serial.println(F("OLED nenajdene — pokracujem bez displeja"));
  }

  // SD karta
  if (!SD.begin(SD_CS)) {
    Serial.println(F("SD karta nenajdena!"));
    signalCriticalError();
    oledShowError(F("SD CHYBA!"));
    while (true) { delay(1000); }   // Čakaj — nič iné nerob
  }
  Serial.println(F("SD OK"));

  createNewKMLFile();
  initKML();
}

// ============================================================
//  HLAVNÁ SLUČKA
// ============================================================
void loop() {
  // 1) Načítaj GPS dáta
  while (SerialGPS.available()) {
    gps.encode(SerialGPS.read());
  }

  // 2) Aktualizuj čas/dátum zo GPS
  updateLocalTimeFromGPS();

  // 3) Kontrola STOP tlačidla (s debounce)
  unsigned long now = millis();
  if (now - lastBtnCheck > 200) {
    lastBtnCheck = now;
    if (digitalRead(STOP_BTN_PIN) == LOW && isRecording) {
      finalizeKML();
      isRecording = false;
      Serial.println(F("Zaznam ukonceny tlacidlom."));
      signalTrackFinished();
      displayStopped();
      return;
    }
  }

  if (!isRecording) return;

  // 4) Zápis GPS bodu
  if (gps.location.isValid()
      && gps.altitude.isValid()
      && gps.time.isValid()
      && gps.location.lat() != 0.0   // Ochrana pred nulovými súradnicami
      && gps.location.lng() != 0.0
      && !isnan(gps.location.lat())
      && !isnan(gps.location.lng())) {

    if (now - lastGPSWriteTime >= (unsigned long)LOG_INTERVAL_MS) {
      double lat = gps.location.lat();
      double lng = gps.location.lng();
      double alt = gps.altitude.meters();

      // Haversine filter — zaloguj len ak bol pohyb
      float dist = hasLastPos
                   ? haversineM(lastLat, lastLng, lat, lng)
                   : 999.0f;   // Prvý bod vždy zapíš

      if (!hasLastPos || dist >= MIN_DIST_M) {
        if (logGPS(lat, lng, alt,
                   gps.time.hour(),
                   gps.time.minute(),
                   gps.time.second())) {
          if (hasLastPos) totalDistM += dist;
          lastLat    = lat;
          lastLng    = lng;
          hasLastPos = true;
          lastGPSWriteTime = now;
          pointCount++;
        }
      }
    }
  }

  // 5) Signál pri prvom GPS fixe
  if (gps.satellites.isValid()
      && gps.satellites.value() >= MIN_SATELLITES
      && !gpsFixAnnounced) {
    signalGPSFix();
    gpsFixAnnounced = true;
  }

  // 6) Upozornenie na stratu GPS signálu (len ak sme fix mali)
  if (gpsFixAnnounced
      && now - lastGPSWriteTime > NO_FIX_WARN_MS
      && now - lastWarningTime  > NO_FIX_WARN_MS) {
    signalNoSignal();
    lastWarningTime = now;
    Serial.println(F("UPOZORNENIE: GPS signal strateny!"));
  }

  // 7) Aktualizácia OLED displeja
  if (now - lastDisplayUpdate > DISPLAY_UPDATE_MS) {
    lastDisplayUpdate = now;
    displayInfo();
  }
}

// ============================================================
//  AKTUALIZÁCIA LOKÁLNEHO ČASU
// ============================================================
void updateLocalTimeFromGPS() {
  if (!gps.time.isValid() || !gps.date.isValid()) return;

  int hour = ((int)gps.time.hour() + UTC_OFFSET) % 24;

  char timeStr[6];
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d", hour, (int)gps.time.minute());
  localTimeStr = String(timeStr);

  char dateStr[11];
  snprintf(dateStr, sizeof(dateStr), "%02d.%02d.%d",
           (int)gps.date.day(),
           (int)gps.date.month(),
           (int)gps.date.year());
  localDateStr = String(dateStr);
}

// ============================================================
//  SD / KML FUNKCIE
// ============================================================

// Nájde prvý voľný názov súboru track0001.kml – track9999.kml
void createNewKMLFile() {
  for (int i = 1; i <= 9999; i++) {
    snprintf(kmlFileName, sizeof(kmlFileName), "/track%04d.kml", i);
    if (!SD.exists(kmlFileName)) {
      Serial.print(F("Novy subor: "));
      Serial.println(kmlFileName);
      return;
    }
  }
  // Záchrana — prepíš posledný slot
  snprintf(kmlFileName, sizeof(kmlFileName), "/track9999.kml");
  Serial.println(F("SD plna — pouzivam track9999.kml"));
}

// Zapíše hlavičku KML súboru
void initKML() {
  File f = SD.open(kmlFileName, FILE_WRITE);
  if (!f) {
    Serial.println(F("Chyba pri vytvarani KML!"));
    signalCriticalError();
    oledShowError(F("KML CHYBA!"));
    return;
  }
  f.println(F("<?xml version=\"1.0\" encoding=\"UTF-8\"?>"));
  f.println(F("<kml xmlns=\"http://www.opengis.net/kml/2.2\">"));
  f.println(F("<Document>"));
  f.println(F("  <name>GPS Track</name>"));
  f.println(F("  <Style id=\"trackStyle\">"));
  f.println(F("    <LineStyle><color>ff0000ff</color><width>3</width></LineStyle>"));
  f.println(F("  </Style>"));
  f.println(F("  <Placemark>"));
  f.println(F("    <name>My Route</name>"));
  f.println(F("    <styleUrl>#trackStyle</styleUrl>"));
  f.println(F("    <LineString>"));
  f.println(F("      <altitudeMode>absolute</altitudeMode>"));
  f.println(F("      <coordinates>"));
  f.close();
  kmlStarted = true;
  Serial.println(F("KML inicializovany."));
}

// Zapíše jeden GPS bod — vracia true pri úspechu
// OPRAVA: FILE_APPEND namiesto FILE_WRITE
// OPRAVA: Odstránený variable shadowing globálnej kmlFile
bool logGPS(double lat, double lng, double alt,
            int hour, int minute, int second) {
  if (!kmlStarted) return false;

  File f = SD.open(kmlFileName, FILE_APPEND);   // ← OPRAVA: FILE_APPEND
  if (!f) {
    Serial.println(F("Chyba pri zapise GPS bodu!"));
    signalWriteFailure();
    return false;
  }

  // Časový komentár (UTC)
  f.printf("        <!-- %02d:%02d:%02d UTC -->\n", hour, minute, second);
  // KML súradnice: lon,lat,alt (7 desatinných miest = ~1 cm presnosť)
  f.printf("        %.7f,%.7f,%.1f\n", lng, lat, alt);
  f.close();
  return true;
}

// Správne uzavrie KML súbor — MUSÍ sa zavolať pred vypnutím!
void finalizeKML() {
  if (!kmlStarted) return;

  File f = SD.open(kmlFileName, FILE_APPEND);
  if (f) {
    f.println(F("      </coordinates>"));
    f.println(F("    </LineString>"));
    f.println(F("  </Placemark>"));
    f.println(F("</Document>"));
    f.println(F("</kml>"));
    f.close();
    kmlStarted = false;
    Serial.println(F("KML riadne uzavrety."));
  } else {
    Serial.println(F("Chyba pri uzavreti KML!"));
  }
}

// ============================================================
//  OLED DISPLEJ
// ============================================================

void showBootScreen() {
  if (!oledAvailable) return;
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(10, 8);
  display.println(F("GPS"));
  display.setCursor(10, 28);
  display.println(F("TRACKER"));
  display.setTextSize(1);
  display.setCursor(15, 50);
  display.println(F("Inicializacia..."));
  display.display();
  delay(1200);
}

void oledShowError(const __FlashStringHelper* msg) {
  if (!oledAvailable) return;
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(5, 20);
  display.println(msg);
  display.display();
}

void displayInfo() {
  if (!oledAvailable) return;
  display.clearDisplay();

  // --- Čas (veľký) ---
  display.setTextSize(3);
  display.setCursor(8, 2);
  display.print(localTimeStr);

  // --- Dátum ---
  display.setTextSize(1);
  int dw = (int)localDateStr.length() * 6;
  display.setCursor(max(0, (SCREEN_WIDTH - dw) / 2), 38);
  display.print(localDateStr);

  // --- Stav REC / STP + počet bodov ---
  display.setCursor(0, 48);
  if (isRecording) {
    display.print(F("REC "));
  } else {
    display.print(F("STP "));
  }
  display.print(pointCount);
  display.print(F("b"));

  // --- Vzdialenosť ---
  display.setCursor(0, 57);
  if (totalDistM < 1000.0f) {
    char buf[12];
    snprintf(buf, sizeof(buf), "%.0fm", totalDistM);
    display.print(buf);
  } else {
    char buf[12];
    snprintf(buf, sizeof(buf), "%.2fkm", totalDistM / 1000.0f);
    display.print(buf);
  }

  // --- Satelity (vpravo hore) ---
  int satCount = gps.satellites.isValid() ? (int)gps.satellites.value() : 0;
  char satBuf[6];
  snprintf(satBuf, sizeof(satBuf), "S%d", satCount);
  display.setCursor(SCREEN_WIDTH - (int)strlen(satBuf) * 6, 38);
  display.print(satBuf);

  // --- HDOP / kvalita GPS (vpravo dole) ---
  const char* hdopStr = "NoGPS";
  if (gps.hdop.isValid()) {
    float h = gps.hdop.hdop();
    if      (h <= 1.0f) hdopStr = "Excel";
    else if (h <= 2.0f) hdopStr = "Dobre";
    else if (h <= 5.0f) hdopStr = "OK";
    else if (h <= 10.f) hdopStr = "Slabo";
    else                hdopStr = "Bad";
  }
  display.setCursor(SCREEN_WIDTH - (int)strlen(hdopStr) * 6, 57);
  display.print(hdopStr);

  display.display();
}

void displayStopped() {
  if (!oledAvailable) return;
  display.clearDisplay();

  display.setTextSize(2);
  display.setCursor(22, 2);
  display.println(F("STOP"));

  display.setTextSize(1);
  display.setCursor(0, 30);
  display.print(F("Body: "));
  display.println(pointCount);

  display.setCursor(0, 42);
  if (totalDistM < 1000.0f) {
    char buf[16];
    snprintf(buf, sizeof(buf), "Dlzka: %.0f m", totalDistM);
    display.print(buf);
  } else {
    char buf[20];
    snprintf(buf, sizeof(buf), "Dlzka: %.3f km", totalDistM / 1000.0f);
    display.print(buf);
  }

  display.setCursor(0, 54);
  display.print(kmlFileName);

  display.display();
}

// ============================================================
//  ZVUKOVÉ SIGNÁLY
// ============================================================

// 3× krátke — GPS fix získaný
void signalGPSFix() {
  for (int i = 0; i < 3; i++) {
    tone(BUZZER_PIN, 2000); delay(100);
    noTone(BUZZER_PIN);     delay(100);
  }
}

// 5× rýchle — chyba zápisu na SD
void signalWriteFailure() {
  for (int i = 0; i < 5; i++) {
    tone(BUZZER_PIN, 1000); delay(80);
    noTone(BUZZER_PIN);     delay(80);
  }
}

// 2× dlhé — strata GPS signálu
void signalNoSignal() {
  tone(BUZZER_PIN, 700); delay(500);
  noTone(BUZZER_PIN);    delay(200);
  tone(BUZZER_PIN, 700); delay(500);
  noTone(BUZZER_PIN);
}

// Vzostupná melódia — záznam úspešne ukončený
void signalTrackFinished() {
  int notes[] = {800, 1200, 1600, 2000};
  for (int n : notes) {
    tone(BUZZER_PIN, n); delay(120);
    noTone(BUZZER_PIN);  delay(40);
  }
}

// 1× dlhé nízke — fatálna chyba (SD/KML)
void signalCriticalError() {
  tone(BUZZER_PIN, 400); delay(1500);
  noTone(BUZZER_PIN);
}
