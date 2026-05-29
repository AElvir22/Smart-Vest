#include <Wire.h>
#include <math.h>
#include <WiFi.h>

// ==========================================
// ACCELEROMETER & STEP COUNTER CONFIGURATION
// ==========================================

// Heltec WiFi LoRa 32 V3 I2C pins
#define SDA_PIN 41
#define SCL_PIN 42

// LSM303DLHC Accelerometer I2C address
#define LSM303_ACC_ADDR 0x19

// Register addresses
#define CTRL_REG1_A 0x20
#define CTRL_REG4_A 0x23
#define OUT_X_L_A   0x28

// Step detection parameters
const float ALPHA = 0.15;              // Low-pass filter coefficient
const float STEP_THRESHOLD = 1.15;     // g-force threshold for a step
const float HYSTERESIS = 0.1;          // Prevents double-counting around the threshold
const unsigned long STEP_DELAY = 250;  // Minimum ms between steps
const unsigned long SENSOR_INTERVAL = 10; // 10ms = 100Hz polling rate

float smoothedMag = 1.0;
unsigned long lastStepTime = 0;
unsigned long lastSensorReadTime = 0;
int stepCount = 0;
bool isHigh = false;

// ==========================================
// WIFI ACCESS POINT & TCP SERVER CONFIG
// ==========================================

// Network credentials for the Access Point
const char* ssid = "ESP32-AP";
const char* password = "12345678";

// The port our TCP server listens on
const int serverPort = 80;

// Create a TCP server and client object
WiFiServer server(serverPort);
WiFiClient client;

// Last received BPM value
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
  
  // 1. Initialize I2C and Accelerometer
  Wire.begin(SDA_PIN, SCL_PIN);
  
  // Configure CTRL_REG1_A (100Hz data rate, normal mode, X/Y/Z axes enabled)
  writeRegister(LSM303_ACC_ADDR, CTRL_REG1_A, 0x57);
  
  // Configure CTRL_REG4_A (High resolution mode (12-bit), +/- 2g scale)
  writeRegister(LSM303_ACC_ADDR, CTRL_REG4_A, 0x08);
  Serial.println("LSM303DLHC initialized.");

  // 2. Initialize WiFi Access Point
  WiFi.mode(WIFI_AP);
  Serial.print("Starting Access Point: ");
  Serial.println(ssid);
  WiFi.softAP(ssid, password);

  Serial.print("AP IP Address: ");
  Serial.println(WiFi.softAPIP());

  // 3. Initialize TCP Server
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
  // PART A: WiFi Server Handling
  // --------------------------------------------------
  
  // 1. Check if a new client is trying to connect
  if (!client || !client.connected()) {
    WiFiClient newClient = server.available();
    if (newClient) {
      client = newClient;
      client.setNoDelay(true);
      client.setTimeout(50); // Keep reads fast to avoid blocking the accelerometer
      Serial.println("Client connected!");
      Serial.print("Client IP: ");
      Serial.println(client.remoteIP());
    }
  }

  // 2. If a client is connected and has sent data, read it
  if (client && client.connected() && client.available()) {
    String line = client.readStringUntil('\n');
    line.trim();  // remove '\r' or whitespace

    if (line.length() > 0) {
      lastBpm = line.toInt();
      Serial.print("Received BPM: ");
      Serial.println(lastBpm);
    }
  }

  // 3. Detect when a client disconnects
  if (client && !client.connected()) {
    Serial.println("Client disconnected.");
    client.stop();
  }

  // --------------------------------------------------
  // PART B: Accelerometer & Step Detection (100Hz)
  // --------------------------------------------------
  
  // Non-blocking 10ms timer replaces delay(10)
  if (millis() - lastSensorReadTime >= SENSOR_INTERVAL) {
    lastSensorReadTime = millis();

    // Read 6 bytes of acceleration data
    Wire.beginTransmission(LSM303_ACC_ADDR);
    Wire.write(OUT_X_L_A | 0x80); 
    Wire.endTransmission();
    
    Wire.requestFrom(LSM303_ACC_ADDR, 6);
    if (Wire.available() == 6) {
      int16_t x = Wire.read() | (Wire.read() << 8);
      int16_t y = Wire.read() | (Wire.read() << 8);
      int16_t z = Wire.read() | (Wire.read() << 8);
      
      // 12-bit left-justified. Shift right by 4.
      x >>= 4; y >>= 4; z >>= 4;
      
      // Convert to g-force
      float x_g = x * 0.001;
      float y_g = y * 0.001;
      float z_g = z * 0.001;
      
      // Calculate overall magnitude vector
      float mag = sqrt(x_g * x_g + y_g * y_g + z_g * z_g);
      
      // Low-pass exponential smoothing
      smoothedMag = ALPHA * mag + (1.0 - ALPHA) * smoothedMag;
      
      // Peak detection
      if (smoothedMag > STEP_THRESHOLD && !isHigh) {
        if (millis() - lastStepTime > STEP_DELAY) {
          stepCount++;
          lastStepTime = millis();
          Serial.printf("Step count: %d\n", stepCount);
        }
        isHigh = true;
      } else if (smoothedMag < (STEP_THRESHOLD - HYSTERESIS)) {
        isHigh = false; 
      }
    }
  }
}