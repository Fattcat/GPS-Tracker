#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>

// I deleted function with DHT11 sensor, because esp32 could NOT STAND power delivering for too many devices, sensors
// It would be so nice, if it would works but in my case it did NOT !
// I tried it and it WORKS but then GPS did NOT work :( !

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define GPS_RX 16
#define GPS_TX 17
#define SD_CS 5
#define BUZZER_PIN 4

TinyGPSPlus gps;
HardwareSerial SerialGPS(1);
File kmlFile;

unsigned long lastGPSWriteTime = 0;
unsigned long lastWarningTime = 0;
bool kmlStarted = false;
bool gpsFixAnnounced = false;
char kmlFileName[20];
String currentKMLPath;

bool gpsWasConnected = false;
unsigned long gpsConnectDisplayTime = 0;

String localTimeStr = "--:--";
String localDateStr = "--.--.----";

unsigned long lastDisplayUpdate = 0;

void setup() {
  Serial.begin(115200);
  SerialGPS.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("❌ OLED zlyhal!");
    while (true);
  }

  display.clearDisplay();
  delay(50); // Pre istotu nejaké oneskorenie pretože na displeji sa  
  display.setTextColor(SSD1306_WHITE);

  pinMode(BUZZER_PIN, OUTPUT);

  if (!SD.begin(SD_CS)) {
    Serial.println("❌ SD karta sa nenašla alebo nie je pripojená!");
    signalCriticalError();
    while (1);
  }

  Serial.println("✅ SD karta inicializovaná.");
  createNewKMLFile();
  currentKMLPath = String(kmlFileName);
  initKML();
}

void loop() {
  while (SerialGPS.available()) {
    gps.encode(SerialGPS.read());
  }

  updateLocalTimeFromGPS();

  if (gps.location.isValid() && gps.altitude.isValid() && gps.time.isValid()) {
    double lat = gps.location.lat();
    double lng = gps.location.lng();
    double alt = gps.altitude.meters();
    logGPS(lat, lng, alt, gps.time.hour(), gps.time.minute(), gps.time.second());
  }

  if (gps.satellites.value() >= 4 && !gpsFixAnnounced) {
    signalGPSFix();
    gpsFixAnnounced = true;
  }

  if (millis() - lastGPSWriteTime > 30000 && millis() - lastWarningTime > 30000) {
    signalWriteFailure();
    lastWarningTime = millis();
  }

  if (millis() - lastDisplayUpdate > 5000) {
    lastDisplayUpdate = millis();
    displayInfo();
  }
}

void updateLocalTimeFromGPS() {
  if (gps.time.isValid() && gps.date.isValid()) {
    int hour = gps.time.hour() + 2;
    if (hour >= 24) hour -= 24;
    int min = gps.time.minute();
    int day = gps.date.day();
    int month = gps.date.month();
    int year = gps.date.year();

    char timeStr[6];
    sprintf(timeStr, "%02d:%02d", hour, min);
    localTimeStr = String(timeStr);

    char dateStr[11];
    sprintf(dateStr, "%02d.%02d.%d", day, month, year);
    localDateStr = String(dateStr);
  }
}

void createNewKMLFile() {
  int fileIndex = 1;
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
  File kmlFile = SD.open(currentKMLPath.c_str(), FILE_WRITE);
  if (kmlFile) {
    kmlFile.print("        <!-- ");
    if (hour < 10) kmlFile.print("0"); kmlFile.print(hour); kmlFile.print(":");
    if (minute < 10) kmlFile.print("0"); kmlFile.print(minute); kmlFile.print(":");
    if (second < 10) kmlFile.print("0"); kmlFile.print(second);
    kmlFile.println(" UTC -->");
    kmlFile.print("        ");
    kmlFile.print(lng, 6); kmlFile.print(",");
    kmlFile.print(lat, 6); kmlFile.print(",");
    kmlFile.println(alt, 1);
    kmlFile.close();
  } else {
    Serial.println("⚠️ Chyba pri zápise do KML.");
    signalWriteFailure();
  }
}

void displayInfo() {
  display.clearDisplay();

  display.setTextSize(3);
  display.setCursor(15, 10);
  display.println(localTimeStr); // Čas

  display.setTextSize(1);
  display.setCursor((SCREEN_WIDTH - localDateStr.length() * 6) / 2, 42);
  display.println(localDateStr); // Dátum

  // Počet satelitov v pravom dolnom rohu
  int satCount = gps.satellites.isValid() ? gps.satellites.value() : 0;
  char satBuf[5];
  sprintf(satBuf, "%d", satCount);
  display.setCursor(SCREEN_WIDTH - strlen(satBuf) * 6, SCREEN_HEIGHT - 8);
  display.print(satBuf);

  // HDOP ako text v ľavom dolnom rohu
  String hdopStatus = "Nepripojene";
  if (gps.hdop.isValid()) {
    float hdop = gps.hdop.hdop();
    if (hdop <= 2.0) {
      hdopStatus = "Vyborne";
    } else if (hdop <= 5.0) {
      hdopStatus = "Dobre";
    } else if (hdop <= 10.0) {
      hdopStatus = "Slabe";
    } else {
      hdopStatus = "Nepripojene";
    }
  }

  display.setTextSize(1);
  display.setCursor(0, SCREEN_HEIGHT - 8);
  display.print(hdopStatus);

  display.display();
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
