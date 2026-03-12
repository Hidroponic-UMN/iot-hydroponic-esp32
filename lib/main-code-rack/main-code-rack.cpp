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
#define WIFI_SSID       "Real"      // Ubah: nama WiFi
#define WIFI_PASSWORD   "aqm3xppp"         // Ubah: password WiFi
#define MQTT_SERVER     "10.246.96.73"       // Ubah: IP server Docker
#define MQTT_PORT       1883
#define SEND_INTERVAL   5000                  // Kirim data setiap 5 detik

// ============================================================
//  Internal variables — jangan diubah
// ============================================================

WiFiClient espClient;
PubSubClient mqtt(espClient);

char mqtt_topic[32];
char client_id[32];
unsigned long lastSend = 0;

// Simulated sensor values (drift around realistic targets)
float sim_ph          = 6.0;
float sim_ec          = 1.8;
float sim_water_temp  = 25.0;
float sim_water_level = 70.0;
float sim_water_flow  = 3.0;
float sim_light       = 20000.0;

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
void generateSimulatedData(JsonDocument& doc) {
  // Drift values around realistic targets
  sim_ph          = drift(sim_ph,          6.0,    4.0,   8.0,   0.15);
  sim_ec          = drift(sim_ec,          1.8,    0.5,   3.5,   0.15);
  sim_water_temp  = drift(sim_water_temp,  25.0,   18.0,  32.0,  0.2);
  sim_water_level = drift(sim_water_level, 70.0,   10.0,  100.0, 0.1);
  sim_water_flow  = drift(sim_water_flow,  3.0,    0.5,   6.0,   0.3);
  sim_light       = drift(sim_light,       20000,  5000,  40000, 0.2);

  // Round to realistic precision
  doc["ph"]               = round(sim_ph * 100) / 100.0;          // 6.02
  doc["ec"]               = round(sim_ec * 100) / 100.0;          // 1.82
  doc["water_temp"]       = round(sim_water_temp * 10) / 10.0;    // 25.1
  doc["water_level"]      = round(sim_water_level);                // 70
  doc["water_flow"]       = round(sim_water_flow * 10) / 10.0;    // 3.1
  doc["light_intensity"]  = round(sim_light);                      // 20155
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
    if (mqtt.connect(client_id)) {
      Serial.printf("✅ MQTT connected as '%s'\n", client_id);
      Serial.printf("📤 Publishing to topic: %s\n\n", mqtt_topic);
    } else {
      Serial.printf("❌ MQTT failed (rc=%d). Retrying in 3s...\n", mqtt.state());
      delay(3000);
    }
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

  // Build topic and client ID
  snprintf(mqtt_topic, sizeof(mqtt_topic), "hidroponik/rack/%d", RACK_ID);
  snprintf(client_id, sizeof(client_id), "esp32-rack-%d", RACK_ID);

  Serial.println("╔══════════════════════════════════════╗");
  Serial.println("║  🌱 ESP32 Rack Sensor — Simulasi     ║");
  Serial.printf( "║  Rack ID: %d                          ║\n", RACK_ID);
  Serial.printf( "║  Topic:   %s      ║\n", mqtt_topic);
  Serial.println("╚══════════════════════════════════════╝");

  // Connect
  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
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

  // Send data at interval
  if (millis() - lastSend >= SEND_INTERVAL) {
    lastSend = millis();

    // Build JSON payload
    JsonDocument doc;
    generateSimulatedData(doc);

    char payload[256];
    serializeJson(doc, payload);

    // Publish to MQTT
    if (mqtt.publish(mqtt_topic, payload)) {
      Serial.printf("[%lu] ✅ Rack %d → pH=%.2f EC=%.2f T=%.1f°C WL=%.0f%% F=%.1f L=%.0f\n",
        millis() / 1000,
        RACK_ID,
        doc["ph"].as<float>(),
        doc["ec"].as<float>(),
        doc["water_temp"].as<float>(),
        doc["water_level"].as<float>(),
        doc["water_flow"].as<float>(),
        doc["light_intensity"].as<float>()
      );
    } else {
      Serial.println("❌ Publish failed!");
    }
  }
}