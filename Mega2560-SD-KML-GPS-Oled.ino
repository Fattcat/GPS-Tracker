#include <TinyGPSPlus.h>
#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include <DHT.h>

// ==== OLED ====
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ==== DHT11 ====
#define DHTPIN 14
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// ==== Pins ====
#define PIN_GET_TIME 15
#define SD_CS 5
#define BUZZER_PIN 4

// ==== GPS ====
TinyGPSPlus gps;
#define GPS_SERIAL Serial1

// ==== Globálne premenné ====
String localTimeStr = "";
String localDateStr = "";
char kmlFileName[20];
String currentKMLPath;

unsigned long lastDHTUpdateTime = 0;
unsigned long lastGPSWriteTime = 0;
unsigned long lastWarningTime = 0;

bool kmlStarted = false;
bool gpsFixAnnounced = false;
bool gpsWasConnected = false;
unsigned long gpsConnectDisplayTime = 0;

File kmlFile;

void setup() {
  Serial.begin(115200);
  GPS_SERIAL.begin(9600);

  pinMode(PIN_GET_TIME, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);

  // OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("❌ OLED zlyhal!");
    while (true);
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  dht.begin();

  if (!SD.begin(SD_CS)) {
    Serial.println("❌ SD karta sa nenašla!");
    signalCriticalError();
    while (1);
  }

  Serial.println("✅ SD karta inicializovaná.");
  createNewKMLFile();
  currentKMLPath = String(kmlFileName);
  initKML();
}

void loop() {
  while (GPS_SERIAL.available()) {
    char c = GPS_SERIAL.read();
    gps.encode(c);

    if (gps.location.isValid() && gps.altitude.isValid() && gps.time.isValid()) {
      double lat = gps.location.lat();
      double lng = gps.location.lng();
      double alt = gps.altitude.meters();
      logGPS(lat, lng, alt, gps.time.hour(), gps.time.minute(), gps.time.second());
    }
  }

  if (millis() - lastDHTUpdateTime >= 20000) {
    lastDHTUpdateTime = millis();
    displayInfo();
  }

  if (gps.satellites.value() >= 4 && !gpsFixAnnounced) {
    signalGPSFix();
    gpsFixAnnounced = true;
  }

  if (millis() - lastGPSWriteTime > 30000 && millis() - lastWarningTime > 30000) {
    signalWriteFailure();
    lastWarningTime = millis();
  }

  checkTimeButton();
  updateGPSStatusDisplay();
}

void createNewKMLFile() {
  int fileIndex = 1;
  for (int i = 1; i <= 9999; i++) {
    sprintf(kmlFileName, "/track%04d.kml", i);
    if (!SD.exists(kmlFileName)) {
      break;
    }
  }

  Serial.print("🆕 Nový súbor: ");
  Serial.println(kmlFileName);
}

void initKML() {
  kmlFile = SD.open(kmlFileName, FILE_WRITE);
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
    Serial.println("❌ Chyba pri vytváraní súboru.");
    signalCriticalError();
  }
}

void logGPS(double lat, double lng, double alt, int hour, int minute, int second) {
  if (!kmlStarted) return;

File kmlFile = SD.open(currentKMLPath, FILE_WRITE);

  if (kmlFile) {
    String comment = "        <!-- " + String(hour) + ":" + String(minute) + ":" + String(second) + " UTC -->\n";
    String coords = "        " + String(lng, 6) + "," + String(lat, 6) + "," + String(alt, 1) + "\n";

    kmlFile.print(comment);
    kmlFile.print(coords);

    kmlFile.close();
    lastGPSWriteTime = millis();
  } else {
    Serial.println("⚠️ Chyba pri zápise do KML.");
    signalWriteFailure();
  }
}


void checkTimeButton() {
  static bool pressed = false;

  if (digitalRead(PIN_GET_TIME) == LOW && !pressed) {
    pressed = true;

    int hour = gps.time.hour() + 2; // posun pre CEST
    if (hour >= 24) hour -= 24;

    char timeStr[6];
    sprintf(timeStr, "%02d:%02d", hour, gps.time.minute());
    localTimeStr = String(timeStr);

    char dateStr[11];
    sprintf(dateStr, "%02d.%02d.%d", gps.date.day(), gps.date.month(), gps.date.year());
    localDateStr = String(dateStr);

    Serial.println("🕓 Lokálny čas nastavený: " + localTimeStr);
    Serial.println("📅 Dátum: " + localDateStr);
  }

  if (digitalRead(PIN_GET_TIME) == HIGH && pressed) {
    pressed = false;
  }
}

void displayInfo() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();

  display.clearDisplay();
  display.setTextSize(3);
  display.setCursor(15, 10);
  display.println(localTimeStr);

  display.setTextSize(1);
  display.setCursor((SCREEN_WIDTH - localDateStr.length() * 6) / 2, 42);
  display.println(localDateStr);

  if (!isnan(t) && !isnan(h)) {
    char th[12];
    sprintf(th, "%.0f°C %.0f%%", t, h);
    display.setCursor((SCREEN_WIDTH - strlen(th) * 6) / 2, 54);
    display.println(th);
  }

  display.display();
}

void updateGPSStatusDisplay() {
  bool gpsConnected = gps.satellites.isValid() && gps.satellites.value() >= 4;

  if (!gpsConnected && gpsWasConnected) {
    gpsWasConnected = false;
    display.clearDisplay();
  }

  if (gpsConnected && !gpsWasConnected) {
    gpsWasConnected = true;
    gpsConnectDisplayTime = millis();

    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(10, 26);
    display.println("GPS pripojene!");
    display.display();
  }

  if (gpsWasConnected && millis() - gpsConnectDisplayTime > 3000) {
    displayInfo();
    return;
  }

  if (!gpsConnected) {
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(3, 0);
    display.println("CakamNaGPS");
    display.display();
  }
}

void signalGPSFix() {
  for (int i = 0; i < 3; i++) {
    tone(BUZZER_PIN, 2000); delay(100);
    noTone(BUZZER_PIN);     delay(100);
  }
}

void signalWriteFailure() {
  for (int i = 0; i < 5; i++) {
    tone(BUZZER_PIN, 1000); delay(80);
    noTone(BUZZER_PIN);     delay(80);
  }
}

void signalCriticalError() {
  tone(BUZZER_PIN, 500); delay(1000);
  noTone(BUZZER_PIN);
}
