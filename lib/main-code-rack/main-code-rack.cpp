/*
 * ============================================================
 *  ESP32 Rack Sensor — Simulation Mode
 *  Hidroponik Dashboard — Lab Smart Farming C502
 * ============================================================
 *
 *  Firmware ini untuk 5 ESP32 yang masing-masing merepresentasikan
 *  1 rak hidroponik. Data sensor disimulasikan (random realistis).
 *
 *  CARA PAKAI:
 *  1. Install library di Arduino IDE:
 *     - PubSubClient (by Nick O'Leary)
 *     - ArduinoJson (by Benoit Blanchon)
 *
 *  2. Pilih board: "ESP32 Dev Module"
 *
 *  3. UBAH 3 CONFIG DI BAWAH sebelum upload ke tiap ESP32:
 *     - RACK_ID      → 1, 2, 3, 4, atau 5 (beda per ESP32)
 *     - WIFI_SSID    → nama WiFi lab
 *     - MQTT_SERVER  → IP komputer server (yang jalanin Docker)
 *
 *  4. Upload ke ESP32, buka Serial Monitor (115200 baud)
 *
 *  NANTI KALAU SENSOR FISIK SUDAH ADA:
 *  Ganti fungsi generateSimulatedData() dengan pembacaan sensor asli.
 *  Struktur JSON yang dikirim tetap sama.
 * ============================================================
 */
#if defined(ESP8266)
    #include <ESP8266WiFi.h>
#elif defined(ESP32)
    #include <WiFi.h>
#endif
#include <Wire.h>
#include <BH1750.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ============================================================
//  ⚡ CONFIG — UBAH INI PER ESP32
// ============================================================

// mosquitto_pub -h localhost -t device/register -m '{"mac_addr":"abdgb1-378ahb","type_id":"HYDROPONIC_RACKS","desc":"Buat Rack Hydroponic","attr":{"about":"ini esp32 untuk rack 1","rack_id":"1"}}' -u admin_lab -P admin123

#define TYPE_ID         "HYDROPONIC_RACKS"                    // Tipe sensor buat apa, e.g. HYDROPONIC_RACKS
#define RACK_ID         1                    // Ubah: 1, 2, 3, 4, atau 5
#define DESC_DEVICE     "Buat Rack Hydroponic"

#define WIFI_SSID       "ACES"      // Ubah: nama WiFi
#define WIFI_PASSWORD   "bukanuntukifdansi"         // Ubah: password WiFi

#define MQTT_SERVER     "192.168.1.121"       // Ubah: IP server Docker
#define MQTT_PORT       1883
#define MQTT_USER       "esp32-1"
#define MQTT_PASSWORD   "rack1"

#define SEND_INTERVAL   5000                  // Kirim data setiap 60 detik


// ============================================================
//  Pins Out
// ============================================================
#define SDA_PIN 21
#define SCL_PIN 22
#define ONE_WIRE_PIN 4
#define TDS_PIN 35
#define PH_PIN 33


// ============================================================
//  define Object
// ============================================================
BH1750 luxmeter;
OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature watertemp(&oneWire);

// ============================================================
//  Internal variables — jangan diubah
// ============================================================

WiFiClient espClient;
PubSubClient mqtt(espClient);

String mac_addr = "f4c1e01b-46e7-42c5-9f69-05d67a5a6a5b";
char mqtt_topic[32];
char client_id[32];
unsigned long lastSend = 0;

char cmd_Topic[32];
char ack_cmd_Topic[48];

// ============================================================
//  Generate simulated sensor data
//  ★ GANTI FUNGSI INI dengan pembacaan sensor asli nanti ★
// ============================================================
void generateData(JsonObject doc) {
  watertemp.requestTemperatures();
  delay(100);
  // Round to realistic precision
  doc["ph"]               = analogRead(PH_PIN);          // 6.02
  doc["ec"]               = analogRead(TDS_PIN);          // 1.82
  doc["water_temp"]       = watertemp.getTempCByIndex(0);    // 25.1
  doc["light_intensity"]  = luxmeter.readLightLevel();                      // 20155
}

// ============================================================
//  WiFi connection
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
    Serial.println("\n❌ WiFi connection failed! Retrying in 5s...");
    delay(5000);
  }
}

// ============================================================
//  MQTT connection
// ============================================================
void connectMQTT() {
  if (mqtt.connected()) return;

  Serial.printf("🔌 Connecting to MQTT: %s:%d...\n", MQTT_SERVER, MQTT_PORT);

  while (!mqtt.connected()) {
    if (mqtt.connect(client_id, MQTT_USER, MQTT_PASSWORD)) {
      Serial.printf("✅ MQTT connected as '%s'\n", client_id);
      Serial.printf("📤 Publishing to topic: %s\n", mqtt_topic);

      mqtt.subscribe(cmd_Topic);
      Serial.printf("SUbscribing to topic: %s\n\n", cmd_Topic);
    } else {
      Serial.printf("❌ MQTT failed (rc=%d). Retrying in 3s...\n", mqtt.state());
      delay(3000);
    }
  }
}

bool isRegistered = false;
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

  if (mqtt.publish("device/register", payload)) {
    Serial.println("✅ Device registration sent");
    Serial.println(payload);
    isRegistered = true;
  } else {
    Serial.println("❌ Device registration failed");
  }
}

// ============================================================
//  Deserialize String to JSON
// ============================================================
bool parseJSON(char* json_obj, JsonDocument& doc) {
  DeserializationError err = deserializeJson(doc, json_obj);

  if (err) {
    Serial.println("deserialized JSON failed");
    return false;
  }
  return true;
}

// ============================================================
//  Run Command for Actuator
// ============================================================
enum statusType {
  FAILED = -1,
  SUCCESS = 1
};

statusType runCommand(const char* cmdType) {
  return SUCCESS;
}

// ============================================================
//  MQTT callback
// ============================================================
void callBack(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message Arrived on topic: ");
  Serial.println(topic);

  JsonDocument doc;
  char* string_json = (char*) payload;
  bool checkTopic = (strcmp(topic, cmd_Topic) == 0);

  parseJSON(string_json, doc);
  const char* cmdType = doc["command"];

  // IF Topic equals to rack/{RACK_ID}/cmd
  // OR ...
  Serial.println(cmdType);
  if (checkTopic) {
    char payload[300];
    statusType t = runCommand(cmdType);

    switch (t) {
      case -1:
        doc["status"] = "FAILED";
        serializeJson(doc, payload);
        break;
      case 1:
        doc["status"] = "SUCCESS";
        serializeJson(doc, payload);
        break;
      default:
        break;
    }

    mqtt.publish(ack_cmd_Topic, payload);
  }
}

// ============================================================
//  Setup
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  // Build topic and client ID
  snprintf(mqtt_topic, sizeof(mqtt_topic), "rack/%d/data", RACK_ID);
  snprintf(client_id, sizeof(client_id), "esp32-rack-%d", RACK_ID);
  snprintf(cmd_Topic,      sizeof(cmd_Topic),      "rack/%d/cmd",     RACK_ID);
  snprintf(ack_cmd_Topic,  sizeof(ack_cmd_Topic),  "rack/%d/cmd/ack", RACK_ID);

  Serial.println("╔══════════════════════════════════════╗");
  Serial.println("║  🌱 ESP32 Rack Sensor — Simulasi     ║");
  Serial.printf( "║  Rack ID: %d                          ║\n", RACK_ID);
  Serial.printf( "║  Topic:   %s      ║\n", mqtt_topic);
  Serial.println("╚══════════════════════════════════════╝");

  // Begin sensor
  Wire.begin(SDA_PIN, SCL_PIN);
  luxmeter.begin();
  watertemp.begin();
  pinMode(PH_PIN, INPUT); // pH Sensor
  pinMode(TDS_PIN, INPUT); // TDS Sensor

  // Connect
  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback(callBack);
  connectWiFi();
  connectMQTT();
}

// ============================================================
//  Main loop
// ============================================================
void loop() {
  // Ensure connections
  connectWiFi();
  if (!mqtt.connected()) connectMQTT();
  mqtt.loop();

  if (!isRegistered) {
    registerDevice();
  }

  // Send data at interval
  if (millis() - lastSend >= SEND_INTERVAL) {
    lastSend = millis();

    // Build JSON payload
    StaticJsonDocument<256> root;
    root["mac_addr"] = mac_addr;
    JsonObject data = root["data"].to<JsonObject>();
    generateData(data);

    char payload[300];
    serializeJsonPretty(root, payload);
    Serial.println("JSON Payload:");
    Serial.println(payload);
    Serial.println();

    // Publish to MQTT
    if (mqtt.publish(mqtt_topic, payload)) {
      Serial.println("✅ Publish success");
    } else {
      Serial.println("❌ Publish failed!");
    }
  }
}