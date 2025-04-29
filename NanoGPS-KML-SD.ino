// Version for Arduino Nano board
#include <TinyGPSPlus.h>
#include <SoftwareSerial.h>
#include <SPI.h>
#include <SD.h>

#define GPS_RX 3
#define GPS_TX 2
#define SD_CS 10
#define BUZZER_PIN 4

#define AUTH_ENABLED 1

TinyGPSPlus gps;
SoftwareSerial SerialGPS(GPS_RX, GPS_TX);
File kmlFile;
String currentKMLFile = "";

unsigned long lastLogTime = 0;
unsigned long lastGPSWriteTime = 0;
unsigned long lastWarningTime = 0;
bool kmlStarted = false;
bool gpsFixAnnounced = false;
bool gpsPreviouslyConnected = false;

void setup() {
  Serial.begin(9600);
  SerialGPS.begin(9600);
  pinMode(BUZZER_PIN, OUTPUT);

  delay(100);

  if (!SD.begin(SD_CS)) {
    Serial.println("❌ SD karta sa nenašla!");
    signalSDCardFailed();
    delay(50);
    while (1);
  }
  Serial.println("✅ SD karta OK");
  signalSDCardOK();

#if AUTH_ENABLED
  if (!checkAuthentication()) {
    while (1);
  }
#else
  Serial.println("⚠️ Autentifikácia je vypnutá cez AUTH_ENABLED");
#endif

  if (!checkAuthentication()) {
    while (1);
  }
}

void loop() {
  while (SerialGPS.available()) {
    gps.encode(SerialGPS.read());
  }

  bool hasFix = gps.satellites.value() >= 4 && gps.location.isValid();

  if (hasFix && !gpsPreviouslyConnected) {
    gpsPreviouslyConnected = true;
    Serial.println("----------------------------");
    Serial.println("🔁 Pripojenie bolo obnovené ...");
    Serial.println("📍 Pokračujem v doplňovaní GPS údajov ...");
    Serial.println("----------------------------");

    if (!gpsFixAnnounced) {
      signalGPSFix();
      gpsFixAnnounced = true;
    }

    if (!kmlStarted) {
      currentKMLFile = getNextKMLFilename();
      initKML(currentKMLFile);
    }
  }

  if (!hasFix && gpsPreviouslyConnected) {
    gpsPreviouslyConnected = false;
    Serial.println("⚠️ GPS signál stratený. Čakám na obnovenie...");
  }

  if (hasFix && gps.location.isUpdated()) {
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

  if (!hasFix && millis() - lastGPSWriteTime > 30000 && millis() - lastWarningTime > 30000) {
    signalNoFixWarning();
    lastWarningTime = millis();
  }
}

String getNextKMLFilename() {
  for (int i = 0; i <= 999; i++) {
    String name;
    if (i == 0) {
      name = "track.kml";
    } else {
      name = "track" + String(i) + ".kml";
    }

    if (!SD.exists(name)) {
      return name;
    }
  }
  return "track999.kml";
}

void initKML(String filename) {
  kmlFile = SD.open(filename, FILE_WRITE);
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
    Serial.print("📝 Vytvorený nový súbor: ");
    Serial.println(filename);
  } else {
    Serial.println("❌ Nepodarilo sa vytvoriť KML súbor!");
  }
}

void logGPS(double lat, double lng) {
  if (!kmlStarted) return;

  kmlFile = SD.open(currentKMLFile, FILE_APPEND);
  if (kmlFile) {
    kmlFile.print("        ");
    kmlFile.print(lng, 6);
    kmlFile.print(",");
    kmlFile.print(lat, 6);
    kmlFile.println(",0");
    kmlFile.close();
  } else {
    Serial.println("❌ Chyba pri zápise do súboru.");
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
  Serial.println("----------------------\n");
}

void signalGPSFix() {
  for (int i = 0; i < 3; i++) {
    tone(BUZZER_PIN, 2000); delay(100);
    noTone(BUZZER_PIN);    delay(100);
  }
}

void signalNoFixWarning() {
  for (int i = 0; i < 5; i++) {
    tone(BUZZER_PIN, 1000); delay(80);
    noTone(BUZZER_PIN);     delay(80);
  }
  Serial.println("--------------------\n⚠️ GPS NEpripojené !\n--------------------");
  Serial.println("🔎 Prebieha vyhľadávanie a pripájanie k satelitom ...");
}

void signalSDCardOK() {
  tone(BUZZER_PIN, 1500); delay(300);
  noTone(BUZZER_PIN);     delay(200);
  tone(BUZZER_PIN, 2500); delay(300);
  noTone(BUZZER_PIN);
}

void signalSDCardFailed() {
  tone(BUZZER_PIN, 800); delay(800);
  noTone(BUZZER_PIN); delay(800);
  tone(BUZZER_PIN, 800); delay(800);
  noTone(BUZZER_PIN); delay(800);
}

bool checkAuthentication() {
  File authFile = SD.open("auth.txt", FILE_READ);
  if (!authFile) {
    Serial.println("❌ Súbor auth.txt sa nenašiel!");
    triggerAuthAlarm();
    return false;
  }

  String authLine = authFile.readStringUntil('\n');
  authFile.close();
  authLine.trim();

  if (authLine == "AiJKJJIoloi5P74o") {
    Serial.println("✅ Autentifikácia SD karty úspešná.");
    return true;
  } else {
    Serial.println("❌ Neplatný obsah súboru auth.txt!");
    triggerAuthAlarm();
    return false;
  }
}

void triggerAuthAlarm() {
  Serial.println("🚨 Autentifikácia zlyhala! Spúšťam alarm...");
  while (true) {
    tone(BUZZER_PIN, 1200); delay(300);
    tone(BUZZER_PIN, 1500); delay(300);
    noTone(BUZZER_PIN);     delay(200);
  }
}