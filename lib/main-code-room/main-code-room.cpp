/*
 * ============================================================
 *  Wemos + DHT22 — Room Sensor (Direct WiFi)
 *  Hidroponik Dashboard — Lab Smart Farming C502
 * ============================================================
 *
 *  Fungsi:
 *    - Baca suhu & kelembaban ruangan dari DHT22
 *    - Publish langsung ke MQTT (tidak ada ESP-NOW)
 *
 *  MQTT Topic: hidroponik/room
 *  Payload:    {"temperature": 26.5, "humidity": 62.0}
 *
 *  Library yang dibutuhkan (Arduino Library Manager):
 *    - PubSubClient (by Nick O'Leary)
 *    - DHT sensor library (by Adafruit)
 *    - Adafruit Unified Sensor
 *    - ArduinoJson (by Benoit Blanchon)
 *
 *  Board: LOLIN(WEMOS) D1 R2 & mini  atau  ESP32 Dev Module
 * ============================================================
 */

#if defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266HTTPClient.h>
#elif defined(ESP32)
  #include <WiFi.h>
  #include <HTTPClient.h>
#endif
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DHT.h>

// ============================================================
//  ⚡ CONFIG — UBAH SESUAI LAB
// ============================================================

#define TYPE_ID         "ROOM_MONITORING"
#define RACK_ID         0
#define DESC_DEVICE     "Device untuk ukur temp dan humidity"

#define WIFI_SSID       "Real"      // Ubah: nama WiFi
#define WIFI_PASSWORD   "aqm3xppp"         // Ubah: password WiFi

#define MQTT_SERVER     "10.34.184.30"       // Ubah: IP server Docker
#define MQTT_PORT       1883
#define MQTT_USER       "esp32-0"
#define MQTT_PASSWORD   "room0"

#define DHT_PIN         4                     // Pin data DHT22
#define DHT_TYPE        DHT22
#define SEND_INTERVAL   5000                  // Kirim setiap 5 detik

// ============================================================
//  Internal — jangan diubah
// ============================================================
WiFiClient espClient;
PubSubClient mqtt(espClient);
DHT dht(DHT_PIN, DHT_TYPE);

unsigned long lastSend = 0;
bool isRegistered = false;
char mqtt_topic[32];
char client_id[32];
char signin_topic[32];
char signin_ack [32];
String mac_addr = "c7b7fae9-34f6-4cc9-8f48-03c295629ed3";

// ============================================================
//  WiFi connection + auto-reconnect
// ============================================================
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.printf("\n📡 Connecting to WiFi: %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n✅ WiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\n❌ WiFi failed! Restarting...");
    delay(3000);
    ESP.restart();
  }
}

// ============================================================
//  MQTT connection + auto-reconnect
// ============================================================
void connectMQTT() {
  if (mqtt.connected()) return;

  Serial.printf("🔌 Connecting to MQTT: %s:%d...\n", MQTT_SERVER, MQTT_PORT);

  while (!mqtt.connected()) {
    if (mqtt.connect(client_id, MQTT_USER, MQTT_PASSWORD)) {
      mqtt.subscribe(signin_ack);
      Serial.println("✅ MQTT connected!");
      Serial.printf("📤 Publishing to: %s\n", mqtt_topic);
    } else {
      Serial.printf("❌ MQTT failed (rc=%d). Retry in 3s...\n", mqtt.state());
      delay(3000);
    }
  }
}

void registerDevice() {
  JsonDocument doc;

  doc["mac_addr"] = mac_addr;
  doc["type_id"] = TYPE_ID;
  doc["desc"] = DESC_DEVICE;
  JsonObject desc = doc["attr"].to<JsonObject>();
  desc["about"] = "ini esp32 untuk rack " + String(RACK_ID);
  desc["rack_id"] = String(RACK_ID);

  char payload[256];
  serializeJson(doc, payload);

  if (mqtt.publish(signin_topic, payload)) {
    Serial.println("✅ Device registration sent");
    Serial.println(payload);
  } else {
    Serial.println("❌ Device registration failed");
  }
}

// ============================================================
//  Read DHT22 & publish to MQTT
// ============================================================
void publishRoomData() {
  float temp = dht.readTemperature();
  float hum  = dht.readHumidity();

  // Check sensor read
  if (isnan(temp) || isnan(hum)) {
    Serial.println("⚠️ DHT22 read failed! Check wiring.");
    return;
  }

  // Build JSON
  StaticJsonDocument<256> root;
  root["mac_addr"] = mac_addr;
  JsonObject data = root["data"].to<JsonObject>();
  data["temperature"] = round(temp * 10.0) / 10.0;
  data["humidity"] = round(hum * 10.0) / 10.0;

  char payload[128];
  serializeJson(root, payload);

  // Publish
  if (mqtt.publish(mqtt_topic, payload)) {
    Serial.printf("[%lu] 🏠 Room: Temp=%.1f°C  Humidity=%.1f%%\n", millis() / 1000, temp, hum);
  } else {
    Serial.println("❌ Publish failed!");
  }
}

// ============================================================
//  MQTT callback
// ============================================================
void callBack(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message Arrived on topic: ");
  Serial.println(topic);

  if (strcmp(topic, signin_ack) == 0) {
    isRegistered = true;
  }
}

// ============================================================
//  Setup
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n╔══════════════════════════════════════╗");
  Serial.println("║  🏠 Room Sensor — Wemos + DHT22       ║");
  Serial.println("║  Topic: hidroponik/room               ║");
  Serial.println("╚══════════════════════════════════════╝\n");

  snprintf(mqtt_topic, sizeof(mqtt_topic), "rack/%d/data", RACK_ID);
  snprintf(client_id, sizeof(client_id), "esp32-room-sensor");
  snprintf(signin_topic, sizeof(signin_topic), "device/%d/register", RACK_ID);
  snprintf(signin_ack, sizeof(signin_ack), "device/%d/register/ack", RACK_ID);

  dht.begin();
  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback(callBack);
  connectWiFi();
  connectMQTT();
}

// ============================================================
//  Loop
// ============================================================
void loop() {
  connectWiFi();
  if (!mqtt.connected()) connectMQTT();
  mqtt.loop();
  if (!isRegistered) {
    registerDevice();
  } else {
    if (millis() - lastSend >= SEND_INTERVAL) {
      lastSend = millis();
      publishRoomData();
    }
  }
}