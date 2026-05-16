#include <WiFi.h>
#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"

// Network credentials of the AP we want to connect to
const char* ssid = "ESP32-AP";
const char* password = "12345678";

// The default IP address of the ESP32 Access Point
const char* serverIP = "192.168.4.1";
const int serverPort = 80;

// Create a WiFi client object
WiFiClient client;

// MAX30102 sensor object
MAX30105 particleSensor;

// Beat detection state
const byte RATE_SIZE = 4;        // moving average of last 4 readings
byte rates[RATE_SIZE];
byte rateSpot = 0;
long lastBeat = 0;
float beatsPerMinute = 0;
int beatAvg = 0;

// Send timing (we replace delay() with a millis timer — see note below)
unsigned long lastSend = 0;
const unsigned long SEND_INTERVAL = 2000;  // send every 2 seconds

void setup() {
  Serial.begin(115200);

  // --- Init MAX30102 over I2C (SDA=GPIO21, SCL=GPIO22) ---
  Serial.println("Initializing MAX30102...");
  Wire.begin(41, 42);
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 not found. Check wiring!");
    while (1);  // halt
  }

  // Set up the sensor for heart rate monitoring
  byte ledBrightness = 0x1F;  // ~6.4mA - good for HR
  byte sampleAverage = 4;
  byte ledMode = 2;       // Red + IR only (works for MAX30102 AND MAX30105)
  byte sampleRate = 100;  // 100 Hz - what the algorithm expects
  int pulseWidth = 411;
  int adcRange = 4096;
  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
  particleSensor.setPulseAmplitudeRed(0x0A);
  particleSensor.setPulseAmplitudeGreen(0);  // Turn off Green LED (not needed for HR)
  Serial.println("Sensor ready. Place finger on sensor.");

  // Set ESP32 to Station mode (client mode)
  WiFi.mode(WIFI_STA);

  // Connect to the Access Point
  Serial.print("Connecting to AP: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  // Wait until successfully connected to the Wi-Fi network
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to the AP!");
  Serial.print("My IP Address: ");
  Serial.println(WiFi.localIP());
}

void loop() {
  // 1. Read the IR value EVERY loop iteration.
  //    checkForBeat() must be called continuously or we miss heartbeats.
  long irValue = particleSensor.getIR();

  // checkForBeat() is a function from "heartRate.h"
  if (checkForBeat(irValue) == true) {
    // We sensed a beat! Calculate the BPM
    if (lastBeat == 0) {
      lastBeat = millis();
    } else {
      long delta = millis() - lastBeat;
      lastBeat = millis();

      beatsPerMinute = 60 / (delta / 1000.0);
    }
    // Make sure the reading makes sense (between 20 and 255 BPM)
    if (beatsPerMinute < 255 && beatsPerMinute > 40) {
      // Store this reading in the array to calculate an average
      rates[rateSpot++] = (byte)beatsPerMinute;
      rateSpot %= RATE_SIZE;  // Wrap around the array

      // Take average of readings
      beatAvg = 0;
      for (byte x = 0; x < RATE_SIZE; x++)
        beatAvg += rates[x];
      beatAvg /= RATE_SIZE;
    }
  }

  // 2. Check if we are connected to the server. If not, connect.
  if (!client.connected()) {
    Serial.println("Attempting to connect to server...");

    if (client.connect(serverIP, serverPort)) {
      Serial.println("Connected to server successfully!");
    } else {
      Serial.println("Connection failed. Retrying in 2 seconds...");
      delay(2000);
      return;  // Exit the loop early and try again
    }
  }

  // 3. If connected, send BPM every SEND_INTERVAL ms.
  //    We use millis() instead of delay() so the sensor keeps sampling
  //    in between sends — delay(2000) would freeze beat detection.
  if (client.connected() && millis() - lastSend > SEND_INTERVAL) {
    lastSend = millis();

    Serial.print("Sending BPM: ");
    Serial.println(beatAvg);

    // Send the BPM value.
    // println() automatically adds the '\n' newline character the server expects!
    client.println(beatAvg);
  }
}