#include <SoftwareSerial.h>

SoftwareSerial sim800l(7, 8); // Piny RX a TX pre komunikáciu so SIM800L modulom

String phoneNumber = "+1234567890"; // Zmeňte na skutočné číslo

void setup() {
  Serial.begin(9600);
  sim800l.begin(9600);
}

void loop() {
  if (sim800l.available()) {
    String message = sim800l.readString();
    if (message.indexOf("Get_Location") != -1) {
      String locationData = getLocationData(); // Funkcia na získanie údajov o polohe
      sendMessage(locationData);
    }
  }
}

String getLocationData() {
  sim800l.println("AT+CIPGSMLOC=1,1"); // Príkaz pre získanie údajov o polohe
  delay(2000); // Počkajte na odpoveď modulu
  String response = sim800l.readString();
  // Implementujte kód na spracovanie a extrakciu údajov o polohe zo správy 'response'
  return response;
}

void sendMessage(String message) {
  sim800l.println("AT+CMGF=1"); // Nastavenie SMS módu
  delay(1000);
  sim800l.println("AT+CMGS=\"" + phoneNumber + "\""); // Nastavenie čísla prijímateľa
  delay(1000);
  sim800l.println(message); // Odoslanie správy
  delay(1000);
  sim800l.write(26); // Odoslanie kontrolného znaku (ASCII kód 26)
  delay(1000);
}
