#include <WiFi.h>
#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"

// --- Wi-Fi Settings ---
const char* ssid = "ESP32-AP";
const char* password = "12345678";
const char* serverIP = "192.168.4.1";
const int serverPort = 80;

// --- MAX30102 interrupt pin ---
// Wire the sensor's INT pin to this GPIO. The INT line is open-drain &
// active-low, so we use INPUT_PULLUP and trigger on FALLING.
const int MAX30102_INT_PIN = 40;

// Flag set by the ISR, read by loop().
volatile bool sensorDataReady = false;

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

// Send timing
unsigned long lastSend = 0;
const unsigned long SEND_INTERVAL = 2000;  // send every 2 seconds

// --- Interrupt Service Routine ---
void IRAM_ATTR onSensorDataReady() {
  sensorDataReady = true;
}

void setup() {
  Serial.begin(115200);

  // --- Init MAX30102 over I2C (SDA=GPIO41, SCL=GPIO42) ---
  Serial.println("Initializing MAX30102...");
  Wire.begin(41, 42);
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 not found. Check wiring!");
    while (1);  // halt
  }

  // Set up the sensor for heart rate monitoring
  byte ledBrightness = 0x1F;  // ~6.4mA - good for HR
  byte sampleAverage = 4;
  byte ledMode = 2;       // Red + IR only
  byte sampleRate = 100;  // 100 Hz - what the algorithm expects
  int pulseWidth = 411;
  int adcRange = 4096;
  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
  particleSensor.setPulseAmplitudeRed(0x0A);
  particleSensor.setPulseAmplitudeGreen(0);

  // --- Enable the "new sample ready" interrupt on the sensor ---
  // With sampleAverage=4 @ 100Hz this fires ~every 40ms.
  particleSensor.enableDATARDY();

  // --- Configure the ESP32 GPIO and attach our ISR ---
  pinMode(MAX30102_INT_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(MAX30102_INT_PIN), onSensorDataReady, FALLING);

  // Clear any pending interrupt latch on the sensor before we start
  particleSensor.getINT1();

  Serial.println("Sensor ready. Place finger on sensor.");

  // Set ESP32 to Station mode (client mode)
  WiFi.mode(WIFI_STA);

  // Connect to the Access Point
  Serial.print("Connecting to AP: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to the AP!");
  Serial.print("My IP Address: ");
  Serial.println(WiFi.localIP());
}

// Feed a new IR sample to the beat detector and update the running BPM average.
// Called from loop() whenever the sensor signals a new sample is ready.
// Reads/writes the beat-detection globals (lastBeat, beatsPerMinute, rates[],
// rateSpot, beatAvg).
void processIRSample(long irValue) {
  if (!checkForBeat(irValue)) return;

  if (lastBeat == 0) {
    lastBeat = millis();
    return;
  }

  long delta = millis() - lastBeat;
  lastBeat = millis();
  beatsPerMinute = 60 / (delta / 1000.0);

  if (beatsPerMinute <= 40 || beatsPerMinute >= 255) return;

  rates[rateSpot++] = (byte)beatsPerMinute;
  rateSpot %= RATE_SIZE;

  beatAvg = 0;
  for (byte x = 0; x < RATE_SIZE; x++)
    beatAvg += rates[x];
  beatAvg /= RATE_SIZE;

  // Print immediately on every beat
  Serial.print("BPM=");
  Serial.print(beatsPerMinute, 1);
  Serial.print("  Avg=");
  Serial.println(beatAvg);
}

void loop() {
  //Only read the sensor when the ISR tells us a new sample is ready.
  if (sensorDataReady) {
    sensorDataReady = false;     // clear our flag first
    particleSensor.getINT1();    // clear the sensor's interrupt latch
                                 // (without this, INT stays asserted
                                 // and we'll never see another falling edge)

    processIRSample(particleSensor.getIR());
  }

  //WiFi handling 
  if (!client.connected()) {
    if (!client.connect(serverIP, serverPort)) {
      return;  // can't send, but sensor already ran above
    }
  }

  // Send BPM every SEND_INTERVAL ms.
  if (client.connected() && millis() - lastSend > SEND_INTERVAL) {
    lastSend = millis();
    Serial.print("Sending BPM: ");
    client.println(beatAvg);
  }
}