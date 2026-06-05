#include <SoftwareSerial.h>
#include <Wire.h>
#include <math.h>
#include <WiFi.h>
#include "MAX30105.h"
#include <TinyGPSPlus.h>

// ==========================================
// CALORIE TRACKER CONFIGURATION
// ==========================================
// Default user weight in pounds (adjust for better accuracy)
const float USER_WEIGHT_LBS = 160.0; 

// General formula: ~0.57 x Weight(lbs) = Calories per mile. 
// Assuming average of 2200 steps per mile.
const float CALORIES_PER_STEP = (0.57 * USER_WEIGHT_LBS) / 2200.0;

float totalCaloriesBurned = 0.0;

// ==========================================
// GPS + BUTTON CONFIG
// ==========================================

static const int RXPin = 3;   // GPS TX -> ESP32 GPIO 3
static const int TXPin = 4;   // GPS RX -> ESP32 GPIO 4
static const uint32_t GPSBaud = 9600;

static const int buttonPin = 2;

TinyGPSPlus gps;
HardwareSerial gpsSerial(1);

volatile bool buttonPressed = false;
volatile unsigned long lastInterruptTime = 0;

void IRAM_ATTR buttonISR() {
  unsigned long currentTime = millis();

  if (currentTime - lastInterruptTime > 250) {
    buttonPressed = true;
    lastInterruptTime = currentTime;
  }
}

// ==========================================
// ACCELEROMETER & STEP COUNTER CONFIGURATION
// ==========================================

#define SDA_PIN 41
#define SCL_PIN 42

#define LSM303_ACC_ADDR 0x19

#define CTRL_REG1_A 0x20
#define CTRL_REG4_A 0x23
#define OUT_X_L_A   0x28

const float ALPHA = 0.15;
const float STEP_THRESHOLD = 1.15;
const float HYSTERESIS = 0.1;
const unsigned long STEP_DELAY = 250;
const unsigned long SENSOR_INTERVAL = 10;

float smoothedMag = 1.0;
unsigned long lastStepTime = 0;
unsigned long lastSensorReadTime = 0;
int stepCount = 0;
bool isHigh = false;

// ==========================================
// MAX30105 TEMPERATURE SENSOR CONFIG
// ==========================================

MAX30105 particleSensor;
unsigned long lastTempReadTime = 0;
const unsigned long TEMP_INTERVAL = 1000;

// ==========================================
// WIFI ACCESS POINT & TCP SERVER CONFIG
// ==========================================

const char* ssid = "ESP32-AP";
const char* password = "12345678";

const int serverPort = 80;

WiFiServer server(serverPort);
WiFiClient client;

int lastBpm = 0;

// ==========================================
// HELPER FUNCTIONS
// ==========================================

void writeRegister(uint8_t deviceAddr, uint8_t regAddr, uint8_t value) {
  Wire.beginTransmission(deviceAddr);
  Wire.write(regAddr);
  Wire.write(value);
  Wire.endTransmission();
}

// ==========================================
// SETUP
// ==========================================

void setup() {
  Serial.begin(115200);
  Serial.println("Initializing system...");

  // GPS setup
  gpsSerial.begin(GPSBaud, SERIAL_8N1, RXPin, TXPin);
  Serial.println("GT-U7 GPS initialized.");

  // Button setup
  pinMode(buttonPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(buttonPin), buttonISR, FALLING);
  Serial.println("Press button to print GPS location.");

  // I2C setup
  Wire.begin(SDA_PIN, SCL_PIN);

  // Accelerometer setup
  writeRegister(LSM303_ACC_ADDR, CTRL_REG1_A, 0x57);
  writeRegister(LSM303_ACC_ADDR, CTRL_REG4_A, 0x08);
  Serial.println("LSM303DLHC initialized.");

  // MAX30102 setup
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 was not found. Please check wiring/power.");
    while (1);
  }

  particleSensor.setup(0);
  particleSensor.enableDIETEMPRDY();
  Serial.println("MAX30105 initialized.");

  // WiFi setup
  WiFi.mode(WIFI_AP);
  Serial.print("Starting Access Point: ");
  Serial.println(ssid);
  WiFi.softAP(ssid, password);

  Serial.print("AP IP Address: ");
  Serial.println(WiFi.softAPIP());

  // TCP server setup
  server.begin();
  server.setNoDelay(true);

  Serial.printf("TCP server listening on port %d\n", serverPort);
  Serial.println("System ready. Start walking and awaiting clients!");
}

// ==========================================
// MAIN LOOP
// ==========================================

void loop() {
  // --------------------------------------------------
  // PART A: Keep reading GPS data
  // --------------------------------------------------

  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }

  if (buttonPressed) {
    buttonPressed = false;

    Serial.println("Button pressed!");

    if (gps.location.isValid()) {
      Serial.print("Latitude: ");
      Serial.println(gps.location.lat(), 6);

      Serial.print("Longitude: ");
      Serial.println(gps.location.lng(), 6);

      Serial.print("Satellites: ");
      Serial.println(gps.satellites.value());

      // Print Current Calories on Button Press
      Serial.print("Total Calories Burned: ");
      Serial.println(totalCaloriesBurned, 2);

      Serial.println();
    } else {
      Serial.println("Location: INVALID (Waiting for Fix)");
    }
  }

  // --------------------------------------------------
  // PART B: WiFi Server Handling
  // --------------------------------------------------

  if (!client || !client.connected()) {
    WiFiClient newClient = server.available();

    if (newClient) {
      client = newClient;
      client.setNoDelay(true);
      client.setTimeout(50);

      Serial.println("Client connected!");
      Serial.print("Client IP: ");
      Serial.println(client.remoteIP());
    }
  }

  if (client && client.connected() && client.available()) {
    String line = client.readStringUntil('\n');
    line.trim();

    if (line.length() > 0) {
      lastBpm = line.toInt();
      Serial.print("Received BPM: ");
      Serial.println(lastBpm);
    }
  }

  if (client && !client.connected()) {
    Serial.println("Client disconnected.");
    client.stop();
  }

  // --------------------------------------------------
  // PART C: Accelerometer, Step & Calorie Detection
  // --------------------------------------------------

  if (millis() - lastSensorReadTime >= SENSOR_INTERVAL) {
    lastSensorReadTime = millis();

    Wire.beginTransmission(LSM303_ACC_ADDR);
    Wire.write(OUT_X_L_A | 0x80);
    Wire.endTransmission();

    Wire.requestFrom(LSM303_ACC_ADDR, 6);

    if (Wire.available() == 6) {
      int16_t x = Wire.read() | (Wire.read() << 8);
      int16_t y = Wire.read() | (Wire.read() << 8);
      int16_t z = Wire.read() | (Wire.read() << 8);

      x >>= 4;
      y >>= 4;
      z >>= 4;

      float x_g = x * 0.001;
      float y_g = y * 0.001;
      float z_g = z * 0.001;

      float mag = sqrt(x_g * x_g + y_g * y_g + z_g * z_g);

      smoothedMag = ALPHA * mag + (1.0 - ALPHA) * smoothedMag;

      if (smoothedMag > STEP_THRESHOLD && !isHigh) {
        if (millis() - lastStepTime > STEP_DELAY) {
          
          // Step Confirmed
          stepCount++;
          totalCaloriesBurned += CALORIES_PER_STEP; // Add step calories
          lastStepTime = millis();
          
          Serial.printf("Steps: %d | Calories: %.2f kcal\n", stepCount, totalCaloriesBurned);
        }

        isHigh = true;
      } else if (smoothedMag < (STEP_THRESHOLD - HYSTERESIS)) {
        isHigh = false;
      }
    }
  }

  // --------------------------------------------------
  // PART D: Temperature Reading
  // --------------------------------------------------

  if (millis() - lastTempReadTime >= TEMP_INTERVAL) {
    lastTempReadTime = millis();

    float temperature = particleSensor.readTemperature();
    float temperatureF = particleSensor.readTemperatureF();

    Serial.print("temperatureC=");
    Serial.print(temperature, 4);
    Serial.print(" temperatureF=");
    Serial.println(temperatureF, 4);
  }
}