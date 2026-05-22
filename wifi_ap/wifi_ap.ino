#include <WiFi.h>

// Network credentials for the Access Point we create
const char* ssid = "ESP32-AP";
const char* password = "12345678";

// The port our TCP server listens on
const int serverPort = 80;

// Create a TCP server object on the chosen port
WiFiServer server(serverPort);

// Create a WiFi client object to hold the connected station
WiFiClient client;

// Last received BPM value
int lastBpm = 0;

void setup() {
  Serial.begin(115200);

  // Set ESP32 to Access Point mode
  WiFi.mode(WIFI_AP);

  // Start the Access Point with our SSID and password
  Serial.print("Starting Access Point: ");
  Serial.println(ssid);
  WiFi.softAP(ssid, password);

  // Print the AP's IP address (default is 192.168.4.1)
  Serial.print("AP IP Address: ");
  Serial.println(WiFi.softAPIP());

  // Start the TCP server
  server.begin();
  server.setNoDelay(true);
  Serial.print("TCP server listening on port ");
  Serial.println(serverPort);
}

void loop() {
  // 1. Check if a new client is trying to connect.
  //    If we don't already have one, accept it.
  if (!client || !client.connected()) {
    WiFiClient newClient = server.available();
    if (newClient) {
      client = newClient;
      client.setNoDelay(true);
      Serial.println("Client connected!");
      Serial.print("Client IP: ");
      Serial.println(client.remoteIP());
    }
  }

  // 2. If a client is connected and has sent data, read it.
  //    The station sends one BPM value per line, ending in '\n'.
  if (client && client.connected() && client.available()) {
    String line = client.readStringUntil('\n');
    line.trim();  // remove '\r' or whitespace

    if (line.length() > 0) {
      // Convert the received text back into an integer
      int bpm = line.toInt();
      lastBpm = bpm;

      Serial.print("Received BPM: ");
      Serial.println(bpm);
    }
  }

  // 3. Detect when a client disconnects, so the slot is freed up
  //    for the next connection attempt.
  if (client && !client.connected()) {
    Serial.println("Client disconnected.");
    client.stop();
  }
}