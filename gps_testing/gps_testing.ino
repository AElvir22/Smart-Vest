#include <SoftwareSerial.h>
#include <TinyGPS++.h>

// Pins for SoftwareSerial (TX, RX)
static const int RXPin = 3, TXPin = 4;
static const uint32_t GPSBaud = 9600;

// TinyGPS++ object
TinyGPSPlus gps;

// Serial connection to the GPS device
SoftwareSerial ss(RXPin, TXPin);

void setup() {
  Serial.begin(115200);
  ss.begin(GPSBaud);
  Serial.println("GT-U7 GPS Scanning...");
}

void loop() {
  // Read from GPS and feed to TinyGPS++
  while (ss.available() > 0) {
    if (gps.encode(ss.read())) {
      if (gps.location.isValid()) {
        Serial.print("Latitude: ");
        Serial.println(gps.location.lat(), 6);
        Serial.print("Longitude: ");
        Serial.println(gps.location.lng(), 6);
      } else {
        Serial.println("Location: INVALID (Waiting for Fix)");
      }
    }
  }
}
