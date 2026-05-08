#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"  // This is included with the SparkFun library

MAX30105 particleSensor;

const byte RATE_SIZE = 8;  // Increase this for more averaging (4 is good for quick response)
byte rates[RATE_SIZE];     // Array of heart rates
byte rateSpot = 0;
long lastBeat = 0;  // Time at which the last beat occurred

float beatsPerMinute;
int beatAvg;

void setup() {
  Serial.begin(115200);
  Serial.println("Initializing Heart Rate Monitor...");

  // Initialize sensor
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30105 was not found. Please check wiring/power.");
    while (1)
      ;
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
}

void loop() {
  // We still read the IR value because that is what the algorithm uses to find the heartbeat
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

  if (beatsPerMinute > 40 && beatsPerMinute < 255) {
    Serial.print("BPM: ");
    Serial.print(beatsPerMinute);
    Serial.print("\tAvg BPM: ");
    Serial.print(beatAvg);
  } else {
    Serial.print("BPM: --\tAvg BPM: ");
    Serial.print(beatAvg);
  }

  // If the IR value is very low, there is probably no finger on the sensor
  if (irValue < 50000) {
    Serial.print(" No finger detected!");
  }

  Serial.println();
}