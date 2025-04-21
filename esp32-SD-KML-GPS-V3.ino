#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include <SPI.h>
#include <SD.h>

#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include <DHT.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define DHTPIN 14
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

#define PIN_GET_TIME 15 // Tlačidlo na uloženie aktuálneho času
String localTimeStr = ""; // napr. "14:25"
String localDateStr = ""; // napr. "21.04.2025"

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
char kmlFileName[20]; // napr. "track1234.kml"
String currentKMLPath;

bool gpsWasConnected = false;
unsigned long gpsConnectDisplayTime = 0;


void setup() {
  Serial.begin(115200);
  SerialGPS.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);

    pinMode(PIN_GET_TIME, INPUT_PULLUP); // tlačidlo

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("❌ OLED zlyhal!");
    while (true);
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  dht.begin();

  pinMode(BUZZER_PIN, OUTPUT);

  if (!SD.begin(SD_CS)) {
    Serial.println("❌ SD karta sa nenašla alebo nie je pripojená!");
    signalCriticalError();
    while (1);
  }

  Serial.println("✅ SD karta inicializovaná.");
  createNewKMLFile(); // vytvorí názov do kmlFileName
  currentKMLPath = String(kmlFileName); // nastaví cestu pre globálne použitie
  initKML(); // vytvorí súbor a otvorí XML hlavičku
}

void loop() {
  while (SerialGPS.available()) {
    char c = SerialGPS.read();
    gps.encode(c);

  if (gps.location.isValid() && gps.altitude.isValid() && gps.time.isValid()) {
    double lat = gps.location.lat();
    double lng = gps.location.lng();
    double alt = gps.altitude.meters();
    
    logGPS(lat, lng, alt, gps.time.hour(), gps.time.minute(), gps.time.second());
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

      logGPS(lat, lng, alt, gps.time.hour(), gps.time.minute(), gps.time.second());
      printGPSInfo(lat, lng, alt, sats, hdop, speed);
      lastGPSWriteTime = millis();
    }
  }

  if (millis() - lastGPSWriteTime > 30000 && millis() - lastWarningTime > 30000) {
    signalWriteFailure();
    lastWarningTime = millis();
  }
}

void createNewKMLFile() {
  int fileIndex = 1;

  // Prehľadáva možné názvy súborov až po track9999.kml
  for (int i = 1; i <= 9999; i++) {
    sprintf(kmlFileName, "/track%04d.kml", i);
    if (!SD.exists(kmlFileName)) {
      fileIndex = i;
      break;
    }
  }

  sprintf(kmlFileName, "/track%04d.kml", fileIndex);
  Serial.print("🆕 Nový súbor: ");
  Serial.println(kmlFileName);

  checkTimeButton();        // zaznamenaj čas pri stlačení
  updateGPSStatusDisplay(); // spravuje stav displeja podľa GPS fixu
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

  File kmlFile = SD.open(currentKMLPath, FILE_APPEND);
  if (kmlFile) {
    kmlFile.printf("        <!-- %02d:%02d:%02d UTC -->\n", hour, minute, second);
    kmlFile.printf("        %.6f,%.6f,%.1f\n", lng, lat, alt);
    kmlFile.close();
  } else {
    Serial.println("⚠️ Chyba pri zápise do KML.");
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

void checkTimeButton() {
  static bool pressed = false;
  
  if (digitalRead(PIN_GET_TIME) == LOW && !pressed) {
    pressed = true;
    int hour = gps.time.hour() + 2; // posun pre Slovensko (CEST)
    if (hour >= 24) hour -= 24;
    int min = gps.time.minute();
    int sec = gps.time.second();

    int day = gps.date.day();
    int month = gps.date.month();
    int year = gps.date.year();

    char timeStr[6]; // HH:MM
    sprintf(timeStr, "%02d:%02d", hour, min);
    localTimeStr = String(timeStr);

    char dateStr[11]; // DD.MM.YYYY
    sprintf(dateStr, "%02d.%02d.%d", day, month, year);
    localDateStr = String(dateStr);

    Serial.println("🕓 Lokálny čas nastavený: " + localTimeStr);
    Serial.println("📅 Dátum: " + localDateStr);
  }

  if (digitalRead(PIN_GET_TIME) == HIGH && pressed) {
    pressed = false;
  }
}

void displayInfo() {
  float temp = dht.readTemperature();
  float hum = dht.readHumidity();

  display.clearDisplay();
  
  display.setTextSize(3);
  display.setCursor(15, 10);
  display.println(localTimeStr); // Čas

  display.setTextSize(1);
  display.setCursor((SCREEN_WIDTH - localDateStr.length() * 6) / 2, 42);
  display.println(localDateStr); // Dátum

  if (!isnan(temp) && !isnan(hum)) {
    char th[12];
    sprintf(th, "%.0f°C %.0f%%", temp, hum);
    display.setCursor((SCREEN_WIDTH - strlen(th) * 6) / 2, 54);
    display.println(th);
  }

  display.display();
}

void updateGPSStatusDisplay() {
  bool gpsConnected = gps.satellites.isValid() && gps.satellites.value() >= 4;

  // Ak sa stratil fix po pripojení
  if (!gpsConnected && gpsWasConnected) {
    gpsWasConnected = false;
    display.clearDisplay();
  }

  // Ak sa práve pripojil
  if (gpsConnected && !gpsWasConnected) {
    gpsWasConnected = true;
    gpsConnectDisplayTime = millis();

    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(10, 26);
    display.println("GPS pripojene!");
    display.display();
  }

  // Po zobrazení „GPS pripojene!“ na 3 sekundy → hlavné info
  if (gpsWasConnected && millis() - gpsConnectDisplayTime > 3000) {
    displayInfo();  // zobraz čas, dátum, temp, vlhkosť
    return;
  }

  // Ak ešte GPS fix nie je
  if (!gpsConnected) {
    display.clearDisplay();

    // Čakacia správa
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.println("CakamNaGPS ...");

    // Teplota a vlhkosť
    float t = dht.readTemperature();
    float h = dht.readHumidity();

    display.setTextSize(1);
    display.setCursor(30, 45);

    if (isnan(t) || isnan(h)) {
      display.println("T:--C, Vlh:--%");
    } else {
      display.setCursor(15, 48);
      display.printf("Tep:%.0f C , Vlh:%.0f%%", t, h);
    }

    display.display();
  }
}
