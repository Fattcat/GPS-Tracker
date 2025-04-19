#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include <SPI.h>
#include <SD.h>

#define GPS_RX 16
#define GPS_TX 17
#define SD_CS 5
#define BUZZER_PIN 4

TinyGPSPlus gps;
HardwareSerial SerialGPS(1); // UART1 pre GPS modul
File kmlFile;

unsigned long lastLogTime = 0;
unsigned long lastGPSWriteTime = 0;
unsigned long lastWarningTime = 0;
bool kmlStarted = false;
bool gpsFixAnnounced = false;

void setup() {
  Serial.begin(115200);
  SerialGPS.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
  pinMode(BUZZER_PIN, OUTPUT);

  if (!SD.begin(SD_CS)) {
    Serial.println("❌ SD karta sa nenašla alebo nie je pripojená!");
    signalCriticalError(); // dlhé pípnutie
    while (1); // zastav program
  }

  Serial.println("✅ SD karta inicializovaná.");
  initKML();
}

void loop() {
  while (SerialGPS.available()) {
    char c = SerialGPS.read();
    gps.encode(c);

    // Ak nie je fix, zobrazuj NMEA
    if (!gps.location.isValid()) {
      Serial.write(c);
    }
  }

  if (gps.satellites.value() >= 4 && !gpsFixAnnounced) {
    signalGPSFix();
    gpsFixAnnounced = true;
  }

  if (gps.location.isUpdated() && gps.location.isValid()) {
    if (millis() - lastLogTime >= 2000) {
      lastLogTime = millis();

      double lat = gps.location.lat();
      double lng = gps.location.lng();
      double alt = gps.altitude.meters();
      int sats = gps.satellites.value();
      double hdop = gps.hdop.hdop();
      double speed = gps.speed.kmph();

      logGPS(lat, lng);
      printGPSInfo(lat, lng, alt, sats, hdop, speed);
      lastGPSWriteTime = millis();
    }
  }

  // Varovanie pri chýbajúcom logovaní
  if (millis() - lastGPSWriteTime > 30000 && millis() - lastWarningTime > 30000) {
    signalWriteFailure();
    lastWarningTime = millis();
  }
}

void initKML() {
  SD.remove("/track.kml");
  kmlFile = SD.open("/track.kml", FILE_WRITE);

  if (kmlFile) {
    kmlFile.println("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
    kmlFile.println("<kml xmlns=\"http://www.opengis.net/kml/2.2\">");
    kmlFile.println("<Document>");
    kmlFile.println("  <name>GPS Track</name>");
    kmlFile.println("  <Placemark>");
    kmlFile.println("    <name>My Route</name>");
    kmlFile.println("    <LineString>");
    kmlFile.println("      <coordinates>");
    kmlFile.close();
    kmlStarted = true;
  } else {
    Serial.println("❌ Chyba pri vytváraní súboru track.kml");
    signalCriticalError();
  }
}

void logGPS(double lat, double lng) {
  if (!kmlStarted) return;

  kmlFile = SD.open("/track.kml", FILE_APPEND);
  if (kmlFile) {
    kmlFile.printf("        %.6f,%.6f,0\n", lng, lat);
    kmlFile.close();
  } else {
    Serial.println("⚠️ Chyba pri zapisovaní do súboru!");
    signalWriteFailure();
  }
}

void printGPSInfo(double lat, double lng, double alt, int sats, double hdop, double speed) {
  Serial.println("------ GPS INFO ------");
  Serial.print("🧭 Latitude: ");  Serial.println(lat, 6);
  Serial.print("🧭 Longitude: "); Serial.println(lng, 6);
  Serial.print("📏 Altitude: ");  Serial.print(alt, 1); Serial.println(" m");
  Serial.print("📡 Satelity: ");  Serial.println(sats);
  Serial.print("🎯 Presnosť (HDOP): "); Serial.println(hdop);
  Serial.print("🚴 Rýchlosť: "); Serial.print(speed, 1); Serial.println(" km/h");
  Serial.println("----------------------");
  Serial.println();
}

// ▶️ 3x pípnutie pre GPS fix
void signalGPSFix() {
  for (int i = 0; i < 3; i++) {
    tone(BUZZER_PIN, 2000); delay(100);
    noTone(BUZZER_PIN);     delay(100);
  }
}

// ❗ 5x pípnutie pre chybu zápisu
void signalWriteFailure() {
  for (int i = 0; i < 5; i++) {
    tone(BUZZER_PIN, 1000); delay(80);
    noTone(BUZZER_PIN);     delay(80);
  }
}

// ❌ Dlhý tón pre kritickú chybu (napr. SD karta)
void signalCriticalError() {
  tone(BUZZER_PIN, 500); delay(1000);
  noTone(BUZZER_PIN);
}
