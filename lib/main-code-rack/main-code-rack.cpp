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
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ============================================================
//  ⚡ CONFIG — UBAH INI PER ESP32
// ============================================================

#define RACK_ID         1                    // Ubah: 1, 2, 3, 4, atau 5
#define TYPE_ID         1                    // Tipe sensor buat apa, e.g. Sensor buat ukur temp ruangan -> id = 1
#define DESC_DEVICE     "Buat Rack Hydroponic"
#define WIFI_SSID       "FUNHOUSE 1B"      // Ubah: nama WiFi
#define WIFI_PASSWORD   "T554022v23"         // Ubah: password WiFi
#define MQTT_SERVER     "192.168.3.118"       // Ubah: IP server Docker
#define MQTT_PORT       1883
#define SEND_INTERVAL   60000                  // Kirim data setiap 60 detik

#define MQTT_USER       "esp32-1"
#define MQTT_PASSWORD   "rack1"

// ============================================================
//  Internal variables — jangan diubah
// ============================================================

WiFiClient espClient;
PubSubClient mqtt(espClient);

String mac_addr = "f4c1e01b-46e7-42c5-9f69-05d67a5a6a5b";
char mqtt_topic[32];
char client_id[32];
unsigned long lastSend = 0;

const char* cmd_Topic = ("rack/"+ String(RACK_ID) + "/cmd").c_str();
const char* ack_cmd_Topic = ("rack/"+ String(RACK_ID) + "/cmd/ack").c_str();


// Simulated sensor values (drift around realistic targets)
float sim_ph          = 6.0;
float sim_ec          = 1.8;
float sim_water_temp  = 25.0;
float sim_water_level = 70.0;
float sim_water_flow  = 3.0;
float sim_light       = 20000.0;
float sim_air_temp = 24.0;

// ============================================================
//  Drift function — membuat data bergerak realistis
// ============================================================
float drift(float current, float target, float minVal, float maxVal, float volatility) {
  float range = maxVal - minVal;
  float noise = (random(-1000, 1001) / 1000.0) * volatility * range * 0.02;
  float pull  = (target - current) * 0.01;
  float result = current + noise + pull;
  return constrain(result, minVal, maxVal);
}

// ============================================================
//  Generate simulated sensor data
//  ★ GANTI FUNGSI INI dengan pembacaan sensor asli nanti ★
// ============================================================
void generateSimulatedData(JsonObject doc) {
  // Drift values around realistic targets
  sim_ph          = drift(sim_ph,          6.0,    4.0,   8.0,   0.15);
  sim_ec          = drift(sim_ec,          1.8,    0.5,   3.5,   0.15);
  sim_water_temp  = drift(sim_water_temp,  25.0,   18.0,  32.0,  0.2);
  sim_water_level = drift(sim_water_level, 70.0,   10.0,  100.0, 0.1);
  sim_water_flow  = drift(sim_water_flow,  3.0,    0.5,   6.0,   0.3);
  sim_light       = drift(sim_light,       20000,  5000,  40000, 0.2);
  sim_air_temp    = drift(sim_air_temp,    24.0,  18.0, 35.0, 0.2);

  // Round to realistic precision
  doc["ph"]               = round(sim_ph * 100) / 100.0;          // 6.02
  doc["ec"]               = round(sim_ec * 100) / 100.0;          // 1.82
  doc["water_temp"]       = round(sim_water_temp * 10) / 10.0;    // 25.1
  doc["water_level"]      = round(sim_water_level);                // 70
  doc["water_flow"]       = round(sim_water_flow * 10) / 10.0;    // 3.1
  doc["light_intensity"]  = round(sim_light);                      // 20155
  doc["air_temp"]         = round(sim_air_temp * 10) / 10.0;
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

  // Seed random with noise from analog pin
  randomSeed(analogRead(0) + millis());

  // Add per-rack offset to make each rack unique
  sim_ph          += (RACK_ID - 3) * 0.1;
  sim_ec          += (RACK_ID - 3) * 0.05;
  sim_water_temp  += (RACK_ID - 3) * 0.5;
  sim_water_level += (RACK_ID - 3) * 5;
  sim_water_flow  += (RACK_ID - 3) * 0.2;
  sim_light       += (RACK_ID - 3) * 2000;
  sim_air_temp    += (RACK_ID - 3) * 0.4;

  // Build topic and client ID
  snprintf(mqtt_topic, sizeof(mqtt_topic), "rack/%d/data", RACK_ID);
  snprintf(client_id, sizeof(client_id), "esp32-rack-%d", RACK_ID);

  Serial.println("╔══════════════════════════════════════╗");
  Serial.println("║  🌱 ESP32 Rack Sensor — Simulasi     ║");
  Serial.printf( "║  Rack ID: %d                          ║\n", RACK_ID);
  Serial.printf( "║  Topic:   %s      ║\n", mqtt_topic);
  Serial.println("╚══════════════════════════════════════╝");

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
    JsonDocument root;

    root["mac_addr"] = mac_addr;

    JsonObject data = root["data"].to<JsonObject>();

    generateSimulatedData(data);

    char payload[300];
    serializeJson(root, payload);

    // Publish to MQTT
    if (mqtt.publish(mqtt_topic, payload)) {
      Serial.printf("[%lu] ✅ Rack %d → pH=%.2f EC=%.2f T=%.1f°C WL=%.0f%% F=%.1f L=%.0f\n",
        millis() / 1000,
        RACK_ID,
        root["ph"].as<float>(),
        root["ec"].as<float>(),
        root["water_temp"].as<float>(),
        root["water_level"].as<float>(),
        root["water_flow"].as<float>(),
        root["light_intensity"].as<float>(),
        root["air_temp"].as<float>()
      );
    } else {
      Serial.println("❌ Publish failed!");
    }
  }
}