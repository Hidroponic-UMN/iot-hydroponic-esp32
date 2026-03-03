/**
 * ESP32 + DHT22 — Room Temperature & Humidity
 * 
 * Reference Arduino code for when the physical hardware is ready.
 * 
 * Wiring:
 *   DHT22 VCC  → ESP32 3.3V
 *   DHT22 GND  → ESP32 GND
 *   DHT22 DATA → ESP32 GPIO 4
 * 
 * Libraries needed (install via Arduino Library Manager):
 *   - DHT sensor library (by Adafruit)
 *   - ArduinoJson (by Benoit Blanchon)
 *   - Adafruit Unified Sensor
 */

// #include <WiFi.h>
// #include <HTTPClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <DHT.h>
#include "ArduinoJson.h"

// ===== CONFIGURATION =====
const char* WIFI_SSID     = "ACES";
const char* WIFI_PASSWORD = "bukanuntukifdansi";
const char* SERVER_URL    = "http://192.168.1.121:3000/api/room";

#define DHT_PIN   4
#define DHT_TYPE  DHT22
#define SEND_INTERVAL_MS  5000

// ===== GLOBALS =====
DHT dht(DHT_PIN, DHT_TYPE);
unsigned long lastSend = 0;

void setup() {
    Serial.begin(115200);
    Serial.println("\n🌱 ESP32 Room Sensor Starting...");

    // Init DHT22
    dht.begin();

    // Connect to WiFi
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\n✅ Connected! IP: %s\n", WiFi.localIP().toString().c_str());
}

void loop() {
    if (millis() - lastSend < SEND_INTERVAL_MS) return;
    lastSend = millis();

    // Read sensor
    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();

    // Validate reading
    if (isnan(temperature) || isnan(humidity)) {
        Serial.println("❌ Failed to read DHT22 sensor!");
        return;
    }

    Serial.printf("📡 Temp: %.1f°C  Humidity: %.1f%%\n", temperature, humidity);

    // Send to server
    if (WiFi.status() == WL_CONNECTED) {
        WiFiClient client;
        HTTPClient http;
        http.begin(client, SERVER_URL);
        http.addHeader("Content-Type", "application/json");

        // Build JSON payload
        JsonDocument doc;
        doc["temperature"] = round(temperature * 10.0) / 10.0;
        doc["humidity"] = round(humidity * 10.0) / 10.0;

        String payload;
        serializeJson(doc, payload);

        int httpCode = http.POST(payload);

        if (httpCode == 200) {
            Serial.println("✅ Data sent successfully");
        } else {
            Serial.printf("❌ Server error: %d\n", httpCode);
        }

        http.end();
    } else {
        Serial.println("❌ WiFi disconnected, reconnecting...");
        WiFi.reconnect();
    }
}