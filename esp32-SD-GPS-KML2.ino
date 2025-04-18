#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include <SPI.h>
#include <SD.h>

TinyGPSPlus gps;
HardwareSerial SerialGPS(1); // UART1 pre GPS

const int GPS_RX = 16;
const int GPS_TX = 17;
const int SD_CS = 5;
const int BUZZER_PIN = 4; // Piezo bzučiak

File kmlFile;
unsigned long lastLogTime = 0;
unsigned long startTime = 0;
bool kmlInitialized = false;
bool kmlFinalized = false;
bool gpsFixSignaled = false;

void setup() {
  Serial.begin(115200);
  SerialGPS.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
  pinMode(BUZZER_PIN, OUTPUT);

  if (!SD.begin(SD_CS)) {
    Serial.println("❌ SD karta sa nenašla!");
    while (1);
  }
  Serial.println("✅ SD karta OK");

  if (SD.exists("/track.kml") && !kmlFinalized) {
    finalizeKML();
  }

  initKML();
  startTime = millis();
}

void loop() {
  while (SerialGPS.available()) {
    gps.encode(SerialGPS.read());
  }

  if (gps.location.isUpdated() && gps.location.isValid()) {
    if (!gpsFixSignaled && gps.satellites.value() >= 4) {
      gpsFixSignaled = true;
      signalGPSFix(); // 3× krátke pípnutia
    }

    if (millis() - lastLogTime >= 2000 && !kmlFinalized) {
      lastLogTime = millis();

      double lat = gps.location.lat();
      double lng = gps.location.lng();
      double alt = gps.altitude.meters();
      int sats = gps.satellites.value();
      double hdop = gps.hdop.hdop();
      double speed = gps.speed.kmph();

      logGPS(lat, lng);
      printGPSInfo(lat, lng, alt, sats, hdop, speed);
    }
  }

  // Skrátené ukončenie po 2 minútach
  if (millis() - startTime >= 2 * 60 * 1000UL && !kmlFinalized) {
    finalizeKML();
    kmlFinalized = true;
    Serial.println("✅ Trasa bola uložená po 2 minútach.");
  }
}

// Inicializácia KML súboru
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
    kmlInitialized = true;
  }
}

// Logovanie bodu do KML
void logGPS(double lat, double lng) {
  if (!kmlInitialized || kmlFinalized) return;

  kmlFile = SD.open("/track.kml", FILE_APPEND);
  if (kmlFile) {
    kmlFile.print("        ");
    kmlFile.print(lng, 6);
    kmlFile.print(",");
    kmlFile.print(lat, 6);
    kmlFile.println(",0");
    kmlFile.close();
  }
}

// Dokončenie KML súboru
void finalizeKML() {
  kmlFile = SD.open("/track.kml", FILE_APPEND);
  if (kmlFile) {
    kmlFile.println("      </coordinates>");
    kmlFile.println("    </LineString>");
    kmlFile.println("  </Placemark>");
    kmlFile.println("</Document>");
    kmlFile.println("</kml>");
    kmlFile.close();
  }
  kmlFinalized = true;
}

// Výpis info do Serial monitora
void printGPSInfo(double lat, double lng, double alt, int sats, double hdop, double speed) {
  Serial.println("------ GPS INFO ------");
  Serial.print("🧭 Latitude: ");  Serial.println(lat, 6);
  Serial.print("🧭 Longitude: "); Serial.println(lng, 6);
  Serial.print("📏 Altitude: ");  Serial.print(alt, 1); Serial.println(" m");
  Serial.print("📡 Satelity: ");  Serial.println(sats);
  Serial.print("🎯 Presnosť (HDOP): "); Serial.println(hdop);
  Serial.print("🚴 Rýchlosť: "); Serial.print(speed, 1); Serial.println(" km/h");
  Serial.println("----------------------\n");
}

// Zvuková signalizácia získania GPS fixu (3× krátke pípnutie)
void signalGPSFix() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);
    digitalWrite(BUZZER_PIN, LOW);
    delay(100);
  }
}
