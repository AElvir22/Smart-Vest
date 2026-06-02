#include <WiFi.h>

// --- Wi-Fi Settings ---
const char* ssid = "ESP32-AP";
const char* password = "12345678";
const char* serverIP = "192.168.4.1";
const int serverPort = 80;

// --- Water Sensor Pins ---
// Note: ESP32 pinouts differ from standard Arduinos.
// GPIO 5 is a standard digital pin, GPIO 34 is an analog-input pin.
#define POWER_PIN  5  
#define SIGNAL_PIN 34 

// --- Sensor Variables ---
int waterLevel = 0; 
int lastWaterLevel = -1; // Stores the previous reading

// --- Tracking Variables ---
unsigned long lastChangeTime = 0; 
const unsigned long IDLE_TIMEOUT_MS = 15UL * 60UL * 1000UL; // 15 minutes in milliseconds
const int NOISE_TOLERANCE = 50; // Threshold to ignore minor analog fluctuations
bool warningSent = false; // Prevents spamming the warning

// Create a WiFi client object
WiFiClient client;

void setup() {
  Serial.begin(115200); // Set to 115200 to match the host ESP32

  // Configure water sensor pins
  pinMode(POWER_PIN, OUTPUT);   
  digitalWrite(POWER_PIN, LOW); // Turn the sensor OFF initially to prevent corrosion

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

  // Initialize the timer baseline
  lastChangeTime = millis();
}

void loop() {
  // 1. Maintain connection to the server
  if (!client.connected()) {
    Serial.println("Attempting to connect to server...");
    
    if (client.connect(serverIP, serverPort)) {
      Serial.println("Connected to server successfully!");
    } else {
      Serial.println("Connection failed. Retrying in 2 seconds...");
      delay(2000);
      return; 
    }
  }

  // 2. Read sensor and send data
  if (client.connected()) {
    
    // --- Read the Water Sensor ---
    digitalWrite(POWER_PIN, HIGH);       // turn the sensor ON
    delay(10);                           // wait 10 milliseconds to stabilize
    waterLevel = analogRead(SIGNAL_PIN); // read the analog value from sensor
    digitalWrite(POWER_PIN, LOW);        // turn the sensor OFF immediately

    // --- Check for Water Level Changes ---
    // If the difference between current and last level is greater than the noise tolerance
    if (abs(waterLevel - lastWaterLevel) > NOISE_TOLERANCE) {
      lastChangeTime = millis();    // Reset the 15-minute timer
      lastWaterLevel = waterLevel;  // Update the recorded baseline level
      warningSent = false;          // Reset the warning flag so it can fire again later
    }

    // --- Check if 15 Minutes Have Passed ---
    if ((millis() - lastChangeTime >= IDLE_TIMEOUT_MS) && !warningSent) {
      Serial.println("WARNING: Water level unchanged for 15 minutes. Rehydrate!");
      client.println("WARNING: Rehydrate!");
      warningSent = true; // Ensure this only sends once per 15-minute idle streak
    }

    // Print to local Serial Monitor
    Serial.print("Water Level Sensor Value: ");
    Serial.println(waterLevel);
    
    // Send the sensor value to the Host ESP32
    // println() automatically adds the '\n' character the server is looking for
    client.println(waterLevel);
    
    // Wait for 2 seconds before taking the next reading
    delay(2000); 
  }
}