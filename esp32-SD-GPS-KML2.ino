#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include <SPI.h>
#include <SD.h>

TinyGPSPlus gps;
HardwareSerial SerialGPS(1); // UART1 pre GPS

const int GPS_RX = 16; // RX z ESP32 do TX z GPS
const int GPS_TX = 17; // TX z ESP32 do RX z GPS
const int SD_CS = 5;   // CS pin SD karty

File kmlFile;
unsigned long lastLogTime = 0;
bool kmlInitialized = false;

void setup() {
  Serial.begin(115200);
  SerialGPS.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);

  if (!SD.begin(SD_CS)) {
    Serial.println("SD karta sa nenašla!");
    while (1);
  }
  Serial.println("SD karta OK");

  initKML();
}

void loop() {
  while (SerialGPS.available()) {
    gps.encode(SerialGPS.read());
  }

  if (gps.location.isUpdated() && gps.location.isValid()) {
    if (millis() - lastLogTime >= 2000) {
      lastLogTime = millis();
      logGPS(gps.location.lat(), gps.location.lng());
    }
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
    kmlInitialized = true;
  }
}

void logGPS(double lat, double lng) {
  if (!kmlInitialized) return;

  kmlFile = SD.open("/track.kml", FILE_APPEND);
  if (kmlFile) {
    kmlFile.print("        ");
    kmlFile.print(lng, 6);
    kmlFile.print(",");
    kmlFile.print(lat, 6);
    kmlFile.println(",0");
    kmlFile.close();

    Serial.print("Uložené: ");
    Serial.print(lat, 6);
    Serial.print(", ");
    Serial.println(lng, 6);
  }
}

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
}
