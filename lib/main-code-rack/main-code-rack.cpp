/*
 * ============================================================
 *  ESP32 Rack Sensor — With pH & TDS Calibration
 *  Hidroponik Dashboard — Lab Smart Farming C502
 * ============================================================
 *
 *  Added Features:
 *  - pH sensor calibration with offset storage
 *  - TDS sensor calibration with offset storage
 *  - Automatic offset calculation
 *  - Persistent storage using Preferences
 *
 *  CALIBRATION COMMANDS via MQTT:
 *  pH:  {"command":"KALIBRASI_PH","cmd_log":{"known_value":7.0}}
 *  TDS: {"command":"KALIBRASI_TDS","cmd_log":{"known_value":1330}}
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
#include <Preferences.h>  // ★ Added for persistent storage

// ============================================================
//  ⚡ CONFIG — UBAH INI PER ESP32
// ============================================================

#define TYPE_ID         "HYDROPONIC_RACKS"
#define RACK_ID         1
#define DESC_DEVICE     "Buat Rack Hydroponic"

#define WIFI_SSID       "Real"
#define WIFI_PASSWORD   "aqm3xppp"

#define MQTT_SERVER     "10.34.184.30"
#define MQTT_PORT       1883
#define MQTT_USER       "esp32-1"
#define MQTT_PASSWORD   "rack1"

#define SEND_INTERVAL   5000
#define TIME_OUT_INTERVAL   60000

// ============================================================
//  Pins Out
// ============================================================
#define SDA_PIN 21
#define SCL_PIN 22
#define ONE_WIRE_PIN 4
#define TDS_PIN 35
#define PH_PIN 33

// ============================================================
//  Calibration Constants
// ============================================================
#define CALIBRATION_SAMPLES 50    // Number of readings to average
#define SAMPLE_DELAY 100          // Delay between samples (ms)

// Conversion factors (adjust based on your sensor specs)
#define VREF 3.3                  // ESP32 ADC reference voltage
#define ADC_RESOLUTION 4096.0     // 12-bit ADC
#define PH_NEUTRAL_VOLTAGE 2.5    // Voltage at pH 7 (typical)
#define PH_VOLTAGE_PER_UNIT 0.18  // mV per pH unit (typical)

// ============================================================
//  Define Objects
// ============================================================
BH1750 luxmeter;
OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature watertemp(&oneWire);
Preferences preferences;  // ★ Preferences object for storage

// ============================================================
//  Internal variables
// ============================================================
WiFiClient espClient;
PubSubClient mqtt(espClient);

String mac_addr = "f4c1e01b-46e7-42c5-9f69-05d67a5a6a5b";
bool isRegistered = false;
char mqtt_topic[32];
char client_id[32];
unsigned long lastSend = 0;
unsigned long timeOut = 0;

char cmd_Topic[32];
char ack_cmd_Topic[48];
char signin_topic[32];
char signin_ack[32];

// Calibration offsets (loaded from Preferences)
float ph_offset = 0.0;
float tds_offset = 0.0;

// Command definitions
const char * cmd_PH_CALIBRATION = "KALIBRASI_PH";
const char * cmd_TDS_CALIBRATION = "KALIBRASI_TDS";

// ============================================================
//  Load calibration offsets from Preferences
// ============================================================
void loadCalibrationData() {
  preferences.begin("calibration", false);
  ph_offset = preferences.getFloat("ph_offset", 0.0);
  tds_offset = preferences.getFloat("tds_offset", 0.0);
  preferences.end();

  Serial.println("\n📊 Loaded Calibration Data:");
  Serial.printf("   pH Offset:  %.3f\n", ph_offset);
  Serial.printf("   TDS Offset: %.2f ppm\n\n", tds_offset);
}

// ============================================================
//  Save calibration offsets to Preferences
// ============================================================
void saveCalibrationData() {
  preferences.begin("calibration", false);
  preferences.putFloat("ph_offset", ph_offset);
  preferences.putFloat("tds_offset", tds_offset);
  preferences.end();

  Serial.println("💾 Calibration data saved!");
}

// ============================================================
//  Read raw ADC value with averaging
// ============================================================
float readADCAverage(int pin, int samples) {
  long sum = 0;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(pin);
    delay(SAMPLE_DELAY);
  }
  return (float)sum / samples;
}

// ============================================================
//  Convert raw pH ADC to pH value
// ============================================================
float convertToPH(int raw_adc) {
  // Convert ADC to voltage
  float voltage = (raw_adc / ADC_RESOLUTION) * VREF;

  // Convert voltage to pH (typical pH sensor formula)
  // pH = 7 - ((voltage - 2.5) / 0.18)
  float ph = 7.0 - ((voltage - PH_NEUTRAL_VOLTAGE) / PH_VOLTAGE_PER_UNIT);

  // Apply offset
  ph += ph_offset;

  return ph;
}

// ============================================================
//  Convert raw TDS ADC to TDS/EC value
// ============================================================
float convertToTDS(int raw_adc, float temperature) {
  // Convert ADC to voltage
  float voltage = (raw_adc / ADC_RESOLUTION) * VREF;

  // Temperature compensation coefficient
  float compensationCoefficient = 1.0 + 0.02 * (temperature - 25.0);

  // Voltage to TDS conversion (adjust based on your sensor)
  float compensationVoltage = voltage / compensationCoefficient;
  float tds = (133.42 * compensationVoltage * compensationVoltage * compensationVoltage
               - 255.86 * compensationVoltage * compensationVoltage
               + 857.39 * compensationVoltage) * 0.5;

  // Apply offset
  tds += tds_offset;

  return tds;
}

// ============================================================
//  ★ pH CALIBRATION FUNCTION ★
//  Automatically calculates offset to match known pH value
// ============================================================
bool calibratePH(float known_ph_value) {
  Serial.println("\n🧪 Starting pH Calibration...");
  Serial.printf("   Target pH: %.2f\n", known_ph_value);
  Serial.println("   Taking readings...");

  // Read current raw ADC value (averaged)
  float raw_adc = readADCAverage(PH_PIN, CALIBRATION_SAMPLES);

  // Convert to pH WITHOUT offset
  float temp_offset = ph_offset;  // Store current offset
  ph_offset = 0.0;  // Reset offset temporarily
  float measured_ph = convertToPH((int)raw_adc);

  // Calculate new offset
  float new_offset = known_ph_value - measured_ph;
  ph_offset = new_offset;

  Serial.printf("   Raw ADC: %.2f\n", raw_adc);
  Serial.printf("   Measured pH (no offset): %.2f\n", measured_ph);
  Serial.printf("   Calculated Offset: %.3f\n", new_offset);
  Serial.printf("   New pH (with offset): %.2f\n", convertToPH((int)raw_adc));

  // Save to Preferences
  saveCalibrationData();

  Serial.println("✅ pH Calibration Complete!\n");
  return true;
}

// ============================================================
//  ★ TDS CALIBRATION FUNCTION ★
//  Automatically calculates offset to match known TDS value
// ============================================================
bool calibrateTDS(float known_tds_value) {
  Serial.println("\n🧪 Starting TDS Calibration...");
  Serial.printf("   Target TDS: %.2f ppm\n", known_tds_value);
  Serial.println("   Taking readings...");

  // Get water temperature for compensation
  watertemp.requestTemperatures();
  delay(100);
  float temperature = watertemp.getTempCByIndex(0);

  // Read current raw ADC value (averaged)
  float raw_adc = readADCAverage(TDS_PIN, CALIBRATION_SAMPLES);

  // Convert to TDS WITHOUT offset
  float temp_offset = tds_offset;  // Store current offset
  tds_offset = 0.0;  // Reset offset temporarily
  float measured_tds = convertToTDS((int)raw_adc, temperature);

  // Calculate new offset
  float new_offset = known_tds_value - measured_tds;
  tds_offset = new_offset;

  Serial.printf("   Raw ADC: %.2f\n", raw_adc);
  Serial.printf("   Water Temp: %.2f°C\n", temperature);
  Serial.printf("   Measured TDS (no offset): %.2f ppm\n", measured_tds);
  Serial.printf("   Calculated Offset: %.2f ppm\n", new_offset);
  Serial.printf("   New TDS (with offset): %.2f ppm\n", convertToTDS((int)raw_adc, temperature));

  // Save to Preferences
  saveCalibrationData();

  Serial.println("✅ TDS Calibration Complete!\n");
  return true;
}

// ============================================================
//  Generate sensor data with calibration applied
// ============================================================
void generateData(JsonObject doc) {
  watertemp.requestTemperatures();
  delay(100);

  float temperature = watertemp.getTempCByIndex(0);
  int raw_ph = analogRead(PH_PIN);
  int raw_tds = analogRead(TDS_PIN);

  // Apply calibration
  float calibrated_ph = convertToPH(raw_ph);
  float calibrated_tds = convertToTDS(raw_tds, temperature);

  doc["ph"] = round(calibrated_ph * 100) / 100.0;  // Round to 2 decimals
  doc["ec"] = round(calibrated_tds * 100) / 100.0;
  doc["water_temp"] = round(temperature * 10) / 10.0;
  doc["light_intensity"] = luxmeter.readLightLevel();
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
      mqtt.subscribe(signin_ack);
      Serial.printf("📥 Subscribing to topic: %s\n\n", cmd_Topic);
    } else {
      Serial.printf("❌ MQTT failed (rc=%d). Retrying in 3s...\n", mqtt.state());
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
  PENDING_b = 0,
  SUCCESS = 1
};

statusType runCommand(const char* cmdType, JsonObject doc, StaticJsonDocument<512>& prev) {
  // ★ pH Calibration Command
  if (strcmp(cmdType, cmd_PH_CALIBRATION) == 0) {
    float known_value = doc["known_value"] | 7.0;  // Default to pH 7 if not provided

    if (calibratePH(known_value)) {
      int raw_ph = analogRead(PH_PIN);
      // Apply calibration
      float calibrated_ph = convertToPH(raw_ph);
      doc["ph"] = round(calibrated_ph * 100) / 100.0;
      return SUCCESS;
    } else {
      return FAILED;
    }
  }

  // ★ TDS Calibration Command
  else if (strcmp(cmdType, cmd_TDS_CALIBRATION) == 0) {
    float known_value = doc["known_value"] | 1330.0;  // Default to 1330 ppm if not provided

    if (calibrateTDS(known_value)) {
      watertemp.requestTemperatures();
      delay(100);
      float temperature = watertemp.getTempCByIndex(0);
      int raw_tds = analogRead(TDS_PIN);
      float calibrated_tds = convertToTDS(raw_tds, temperature);
      doc["ec"] = round(calibrated_tds * 100) / 100.0;
      return SUCCESS;
    } else {
      return FAILED;
    }
  }

  return PENDING_b;
}

// ============================================================
//  MQTT callback
// ============================================================
void callBack(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message Arrived on topic: ");
  Serial.println(topic);

  if (strcmp(topic, cmd_Topic) == 0) {
    StaticJsonDocument<512> root;
    DeserializationError error = deserializeJson(root, payload, length);
    if (error) return;

    const char* cmdType = root["command"];
    // Get a reference to the existing cmd_log object without clearing it
    JsonObject cmdLog = root["cmd_log"].as<JsonObject>();

    char payload[300];
    bool looping = true;
    timeOut = millis();  // Start timeout timer

    while(looping) {
      statusType t = runCommand(cmdType, cmdLog, root);
      switch (t) {
        case FAILED:
          root["status"] = "FAILED";
          serializeJson(root, payload);
          looping = false;
          break;
        case SUCCESS:
          root["status"] = "SUCCESS";
          serializeJson(root, payload);
          looping = false;
          break;
        case PENDING:
          // Keep looping
          break;
      }

      if (millis() - timeOut >= TIME_OUT_INTERVAL) {
        root["status"] = "TIMEOUT";
        looping = false;
      }
    }
    Serial.println(payload);
    serializeJson(root, payload);
    mqtt.publish(ack_cmd_Topic, payload);
  } else if (strcmp(topic, signin_ack) == 0) {
    isRegistered = true;
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
  snprintf(signin_topic, sizeof(signin_topic), "device/%d/register", RACK_ID);
  snprintf(signin_ack, sizeof(signin_ack), "device/%d/register/ack", RACK_ID);

  Serial.println("╔══════════════════════════════════════╗");
  Serial.println("║  🌱 ESP32 Rack Sensor — Calibrated   ║");
  Serial.printf( "║  Rack ID: %d                          ║\n", RACK_ID);
  Serial.printf( "║  Topic:   %s      ║\n", mqtt_topic);
  Serial.println("╚══════════════════════════════════════╝");

  // Load calibration data from Preferences
  loadCalibrationData();

  // Begin sensors
  Wire.begin(SDA_PIN, SCL_PIN);
  luxmeter.begin();
  watertemp.begin();
  pinMode(PH_PIN, INPUT);
  pinMode(TDS_PIN, INPUT);

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
  } else {
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
}