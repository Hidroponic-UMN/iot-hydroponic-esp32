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

#define WIFI_SSID       "seedlab"
#define WIFI_PASSWORD   "davidbun"

#define MQTT_SERVER     "10.229.237.57"
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

/*
 * ============================================================
 *  IMPROVED CALIBRATION ALGORITHM
 *  Two-point calibration with slope + offset for better accuracy
 * ============================================================
 */

// ============================================================
//  Enhanced Calibration Storage
// ============================================================

// pH calibration data (two-point)
struct PHCalibration {
    // Calibration mode
  int num_points = 0;  // 0, 1, 2, or 3 points
  bool is_calibrated = false;

  // For 1-point: use offset only
  float offset = -147.00000077;

  // For 2-point: use linear (slope + offset)
  float slope = -0.066666667;

  // Store three calibration points
  float point1_voltage = 2310.0;
  float point1_ph = 7.0;

  float point2_voltage = 2355.0;
  float point2_ph = 4.0;
};

// TDS calibration data (two-point)
struct TDSCalibration {
  float slope = 1.0;
  float offset = 0.0;
  bool is_calibrated = false;

  float point1_voltage = 0.0;
  float point1_tds = 0.0;
  float point2_voltage = 0.0;
  float point2_tds = 1330.0;
  int num_points = 0;
};

PHCalibration ph_cal;
TDSCalibration tds_cal;


// ============================================================
//  Load calibration data from Preferences
// ============================================================
void loadCalibrationData() {
  preferences.begin("calibration", false);

  // Load pH calibration
  ph_cal.slope = preferences.getFloat("ph_slope", -0.066666667);
  ph_cal.offset = preferences.getFloat("ph_offset", -147.00000077);
  ph_cal.is_calibrated = preferences.getBool("ph_cal", false);
  ph_cal.num_points = preferences.getInt("ph_points", 0);
  ph_cal.point1_voltage = preferences.getFloat("ph_p1_v", 2310.0);
  ph_cal.point1_ph = preferences.getFloat("ph_p1_ph", 7.0);
  ph_cal.point2_voltage = preferences.getFloat("ph_p2_v", 2355.0);
  ph_cal.point2_ph = preferences.getFloat("ph_p2_ph", 4.0);

  // Load TDS calibration
  tds_cal.slope = preferences.getFloat("tds_slope", 1.0);
  tds_cal.offset = preferences.getFloat("tds_offset", 0.0);
  tds_cal.is_calibrated = preferences.getBool("tds_cal", false);
  tds_cal.num_points = preferences.getInt("tds_points", 0);
  tds_cal.point1_voltage = preferences.getFloat("tds_p1_v", 0.0);
  tds_cal.point1_tds = preferences.getFloat("tds_p1_tds", 0.0);
  tds_cal.point2_voltage = preferences.getFloat("tds_p2_v", 0.0);
  tds_cal.point2_tds = preferences.getFloat("tds_p2_tds", 1330.0);

  preferences.end();

  Serial.println("\n📊 Loaded Calibration Data:");
  Serial.printf("   pH - Slope: %.4f, Offset: %.3f, Points: %d, Calibrated: %s\n",
                ph_cal.slope, ph_cal.offset, ph_cal.num_points,
                ph_cal.is_calibrated ? "YES" : "NO");
  Serial.printf("   TDS - Slope: %.4f, Offset: %.2f, Points: %d, Calibrated: %s\n\n",
                tds_cal.slope, tds_cal.offset, tds_cal.num_points,
                tds_cal.is_calibrated ? "YES" : "NO");
}


// ============================================================
//  Save calibration data to Preferences
// ============================================================
void saveCalibrationData() {
  preferences.begin("calibration", false);

  // Save pH calibration
  preferences.putFloat("ph_slope", ph_cal.slope);
  preferences.putFloat("ph_offset", ph_cal.offset);
  preferences.putBool("ph_cal", ph_cal.is_calibrated);
  preferences.putInt("ph_points", ph_cal.num_points);
  preferences.putFloat("ph_p1_v", ph_cal.point1_voltage);
  preferences.putFloat("ph_p1_ph", ph_cal.point1_ph);
  preferences.putFloat("ph_p2_v", ph_cal.point2_voltage);
  preferences.putFloat("ph_p2_ph", ph_cal.point2_ph);

  // Save TDS calibration
  preferences.putFloat("tds_slope", tds_cal.slope);
  preferences.putFloat("tds_offset", tds_cal.offset);
  preferences.putBool("tds_cal", tds_cal.is_calibrated);
  preferences.putInt("tds_points", tds_cal.num_points);
  preferences.putFloat("tds_p1_v", tds_cal.point1_voltage);
  preferences.putFloat("tds_p1_tds", tds_cal.point1_tds);
  preferences.putFloat("tds_p2_v", tds_cal.point2_voltage);
  preferences.putFloat("tds_p2_tds", tds_cal.point2_tds);

  preferences.end();

  Serial.println("💾 Calibration data saved to flash!");
}


// ============================================================
//  Calculate 3-point quadratic calibration
//  Solves system of equations for: pH = a*V^2 + b*V + c
// ============================================================
bool calculateThreePointCalibration(
  float v1, float ph1,
  float v2, float ph2,
  float v3, float ph3,
  float &a, float &b, float &c
) {
  Serial.println("\n   Calculating 3-point quadratic fit...");

  // Check for duplicate points
  if (abs(v1 - v2) < 0.001 || abs(v2 - v3) < 0.001 || abs(v1 - v3) < 0.001) {
    Serial.println("   ⚠️ Error: Calibration points too close!");
    return false;
  }

  // Using Lagrange interpolation formula for quadratic
  // More numerically stable than solving matrix equations

  float v1_sq = v1 * v1;
  float v2_sq = v2 * v2;
  float v3_sq = v3 * v3;

  // Calculate determinants using Cramer's rule
  float denom = (v1 - v2) * (v1 - v3) * (v2 - v3);

  if (abs(denom) < 0.0001) {
    Serial.println("   ⚠️ Error: Points are collinear!");
    return false;
  }

  // Coefficient a (quadratic term)
  a = (ph1 * (v2 - v3) + ph2 * (v3 - v1) + ph3 * (v1 - v2)) / denom;

  // Coefficient b (linear term)
  b = (ph1 * (v3_sq - v2_sq) + ph2 * (v1_sq - v3_sq) + ph3 * (v2_sq - v1_sq)) / denom;

  // Coefficient c (constant term)
  c = (ph1 * (v2 * v3_sq - v3 * v2_sq) +
       ph2 * (v3 * v1_sq - v1 * v3_sq) +
       ph3 * (v1 * v2_sq - v2 * v1_sq)) / denom;

  Serial.printf("   a = %.6f (quadratic)\n", a);
  Serial.printf("   b = %.6f (linear)\n", b);
  Serial.printf("   c = %.6f (constant)\n", c);

  // Verify the fit by checking all three points
  float error1 = abs((a * v1_sq + b * v1 + c) - ph1);
  float error2 = abs((a * v2_sq + b * v2 + c) - ph2);
  float error3 = abs((a * v3_sq + b * v3 + c) - ph3);

  Serial.printf("   Fit errors: %.4f, %.4f, %.4f pH\n", error1, error2, error3);

  if (error1 > 0.01 || error2 > 0.01 || error3 > 0.01) {
    Serial.println("   ⚠️ Warning: Large fitting errors detected!");
  }

  return true;
}


// ============================================================
//  Calculate two-point calibration (slope & offset)
// ============================================================
void calculateTwoPointCalibration(
  float v1, float val1,   // First point (voltage, value)
  float v2, float val2,   // Second point (voltage, value)
  float &slope,           // Output: calculated slope
  float &offset           // Output: calculated offset
) {
  // Avoid division by zero
  if (abs(v2 - v1) < 0.001) {
    Serial.println("⚠️ Warning: Calibration points too close, using default");
    return;
  }

  // Calculate slope: (y2 - y1) / (x2 - x1)
  slope = (val1 - val2) / (v1 - v2);

  // Calculate offset: y = slope * x + offset  →  offset = y - slope * x
  offset = val1 - (slope * v1);

  Serial.printf("   Calculated Slope: %.4f\n", slope);
  Serial.printf("   Calculated Offset: %.4f\n", offset);
}


// ============================================================
//  Convert raw ADC to voltage
// ============================================================
float rawToVoltage(int raw_adc) {
  return (raw_adc / ADC_RESOLUTION) * VREF;
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

float readVoltage() {
    const int NUM_SAMPLES = 20;        // Increased from 10 for better stability
    const int DISCARD_SAMPLES = 4;     // Discard 4 lowest + 4 highest
    const float ALPHA = 0.30;          // EMA filter coefficient (0.1-0.3)
    static float ema_voltage = 0;      // Exponential Moving Average
    static bool ema_initialized = false;

    int samples[NUM_SAMPLES];

    // 1. Collect samples with delay for ADC settling
    for (int i = 0; i < NUM_SAMPLES; i++) {
        samples[i] = analogRead(PH_PIN);
        delay(20);  // ADC settling time
    }

    // 2. Sort samples (bubble sort)
    for (int i = 0; i < NUM_SAMPLES - 1; i++) {
        for (int j = i + 1; j < NUM_SAMPLES; j++) {
            if (samples[i] > samples[j]) {
                int temp = samples[i];
                samples[i] = samples[j];
                samples[j] = temp;
            }
        }
    }

    // 3. Remove outliers - discard lowest and highest values
    int sum = 0;
    int count = 0;
    for (int i = DISCARD_SAMPLES; i < NUM_SAMPLES - DISCARD_SAMPLES; i++) {
        sum += samples[i];
        count++;
    }

    // 4. Calculate average of middle values
    float avgValue = sum / (float)count;

    // 5. Convert to voltage (mV)
    float voltage = avgValue * (3300.0 / 4095.0);

    // 6. Apply Exponential Moving Average (EMA) filter
    if (!ema_initialized) {
        ema_voltage = voltage;
        ema_initialized = true;
    } else {
        ema_voltage = (ALPHA * voltage) + ((1.0 - ALPHA) * ema_voltage);
    }

    // 7. Optional: Apply median filter on final result
    static float voltage_history[5] = {0};
    static int history_index = 0;

    voltage_history[history_index] = ema_voltage;
    history_index = (history_index + 1) % 5;

    // Sort history for median
    float sorted_history[5];
    memcpy(sorted_history, voltage_history, sizeof(voltage_history));
    for (int i = 0; i < 4; i++) {
        for (int j = i + 1; j < 5; j++) {
            if (sorted_history[i] > sorted_history[j]) {
                float temp = sorted_history[i];
                sorted_history[i] = sorted_history[j];
                sorted_history[j] = temp;
            }
        }
    }

    // Return median value (middle of 5 samples)
    return sorted_history[2];
}

// ============================================================
//  ★ IMPROVED pH CONVERSION ★
//  Uses two-point calibration (slope + offset)
// ============================================================
float convertToPH() {
  // Convert ADC to voltage
  float voltage = readVoltage();
  float ph_base = (voltage * ph_cal.slope) + ph_cal.offset;
  return ph_base;  // Return uncalibrated if not calibrated
}


// ============================================================
//  ★ IMPROVED TDS CONVERSION ★
//  Uses two-point calibration (slope + offset)
// ============================================================
float convertToTDS(int raw_adc, float temperature) {
  // Convert ADC to voltage
  float voltage = rawToVoltage(raw_adc);

  // Temperature compensation
  float compensationCoefficient = 1.0 + 0.02 * (temperature - 25.0);
  float compensationVoltage = voltage / compensationCoefficient;

  // Base TDS calculation (polynomial formula)
  float tds_base = (133.42 * compensationVoltage * compensationVoltage * compensationVoltage
                    - 255.86 * compensationVoltage * compensationVoltage
                    + 857.39 * compensationVoltage) * 0.5;

  // Apply calibration: TDS_calibrated = slope * TDS_base + offset
  if (!tds_cal.is_calibrated) {
    return tds_base;
  }

  if (tds_cal.num_points == 1) {
    return tds_base + tds_cal.offset;
  }

  if (tds_cal.num_points == 2) {
    return (tds_cal.slope * tds_base) + tds_cal.offset;
  }

  return tds_base;  // Return uncalibrated if not calibrated
}


// ============================================================
//  ★ IMPROVED pH CALIBRATION ★
//  Supports both one-point and two-point calibration
// ============================================================
bool calibratePH(float known_ph_value) {
  Serial.println("\n🧪 Starting pH Calibration...");
  Serial.printf("   Target pH: %.2f\n", known_ph_value);
  Serial.printf("   Current calibration points: %d\n", ph_cal.num_points);
  Serial.println("   Taking readings...");

  // Read raw ADC (averaged for stability)
  float voltage = readVoltage();
  float voltage_per_unit = (7.0 - 4.0) / (ph_cal.point1_voltage - ph_cal.point2_voltage);

  // Calculate base pH (without calibration)
  float base_ph = (voltage * ph_cal.slope) + ph_cal.offset;

  Serial.printf("   Voltage: %.3f mV\n", voltage);
  Serial.printf("   Base pH (uncalibrated): %.2f\n", base_ph);

  // Determine if this is first or second calibration point
  if (ph_cal.num_points == 0) {
    // First calibration point
    Serial.println("   → Setting as calibration point 1");

    ph_cal.point1_voltage = voltage;
    ph_cal.point1_ph = known_ph_value;
    ph_cal.num_points = 1;

    Serial.printf("   One-point calibration applied\n");
    Serial.printf("   Offset: %.3f\n", ph_cal.offset);

  } else if (ph_cal.num_points == 1 || known_ph_value == 7.0) {
    // Second calibration point - enable two-point calibration
    Serial.println("   → Setting as calibration point 2");

    // Check if pH values are different enough
    if (abs(known_ph_value - ph_cal.point1_ph) < 1.0) {
      Serial.println("⚠️ Warning: Calibration points should be at least 1 pH unit apart!");
      Serial.println("   (Recommended: pH 4 and pH 7, or pH 7 and pH 10)");
    }

    ph_cal.point2_voltage = voltage;
    ph_cal.point2_ph = known_ph_value;
    ph_cal.num_points = 2;

    ph_cal.is_calibrated = true;
    Serial.println("   Two-point calibration applied!");

  } else {
    // First calibration point
    Serial.println("   → Setting as calibration point 1");

    ph_cal.point1_voltage = voltage;
    ph_cal.point1_ph = known_ph_value;
    ph_cal.num_points = 1;

    ph_cal.is_calibrated = true;

    Serial.printf("   One-point calibration applied\n");
    Serial.printf("   Offset: %.3f\n", ph_cal.offset);
  }

  // Calculate two-point calibration
  // Map voltage range to pH range
  calculateTwoPointCalibration(
    ph_cal.point1_voltage, ph_cal.point1_ph,
    ph_cal.point2_voltage, ph_cal.point2_ph,
    ph_cal.slope, ph_cal.offset
  );

  // Save to flash
  saveCalibrationData();

  // Test the calibration
  float calibrated_ph = convertToPH();
  Serial.printf("   ✅ New calibrated pH: %.2f (target: %.2f)\n", calibrated_ph, known_ph_value);
  Serial.printf("   Error: %.3f pH units\n", abs(calibrated_ph - known_ph_value));
  Serial.println("✅ pH Calibration Complete!\n");

  return true;
}


// ============================================================
//  ★ IMPROVED TDS CALIBRATION ★
//  Supports both one-point and two-point calibration
// ============================================================
bool calibrateTDS(float known_tds_value) {
  Serial.println("\n🧪 Starting TDS Calibration...");
  Serial.printf("   Target TDS: %.2f ppm\n", known_tds_value);
  Serial.printf("   Current calibration points: %d\n", tds_cal.num_points);
  Serial.println("   Taking readings...");

  // Get water temperature for compensation
  watertemp.requestTemperatures();
  delay(100);
  float temperature = watertemp.getTempCByIndex(0);

  // Read raw ADC (averaged)
  float raw_adc = readADCAverage(TDS_PIN, CALIBRATION_SAMPLES);
  float voltage = rawToVoltage((int)raw_adc);

  // Calculate base TDS (without calibration)
  float compensationCoefficient = 1.0 + 0.02 * (temperature - 25.0);
  float compensationVoltage = voltage / compensationCoefficient;
  float base_tds = (133.42 * compensationVoltage * compensationVoltage * compensationVoltage
                    - 255.86 * compensationVoltage * compensationVoltage
                    + 857.39 * compensationVoltage) * 0.5;

  Serial.printf("   Raw ADC: %.2f\n", raw_adc);
  Serial.printf("   Voltage: %.3f V\n", voltage);
  Serial.printf("   Water Temp: %.2f°C\n", temperature);
  Serial.printf("   Base TDS (uncalibrated): %.2f ppm\n", base_tds);

  // Determine calibration point
  if ((tds_cal.num_points == 0) || (tds_cal.num_points == 1) || tds_cal.is_calibrated) {
    // First calibration point
    Serial.println("   → Setting as calibration point 1");

    tds_cal.point1_voltage = compensationVoltage;  // Store compensated voltage
    tds_cal.point1_tds = known_tds_value;
    tds_cal.num_points = 1;

    // One-point calibration
    tds_cal.slope = 1.0;
    tds_cal.offset = known_tds_value - base_tds;
    tds_cal.is_calibrated = true;

    Serial.printf("   One-point calibration applied\n");
    Serial.printf("   Offset: %.2f ppm\n", tds_cal.offset);

  }

  // Save to flash
  saveCalibrationData();

  // Test the calibration
  float calibrated_tds = convertToTDS((int)raw_adc, temperature);
  Serial.printf("   ✅ New calibrated TDS: %.2f ppm (target: %.2f ppm)\n",
                calibrated_tds, known_tds_value);
  Serial.printf("   Error: %.2f ppm\n", abs(calibrated_tds - known_tds_value));
  Serial.println("✅ TDS Calibration Complete!\n");

  return true;
}


// ============================================================
//  Reset calibration to factory defaults
// ============================================================
bool resetCalibration(const char* sensor_type) {
  Serial.printf("\n🔄 Resetting %s calibration...\n", sensor_type);

  preferences.begin("calibration", false);

  if (strcmp(sensor_type, "PH") == 0 || strcmp(sensor_type, "ALL") == 0) {
    ph_cal.slope = -0.066666667;
    ph_cal.offset = -147.00000077;
    ph_cal.is_calibrated = false;
    ph_cal.num_points = 0;

    preferences.putFloat("ph_slope", -0.066666667);
    preferences.putFloat("ph_offset", -147.00000077);
    preferences.putBool("ph_cal", false);
    preferences.putInt("ph_points", 0);

    Serial.println("   ✅ pH calibration reset");
  }

  if (strcmp(sensor_type, "TDS") == 0 || strcmp(sensor_type, "ALL") == 0) {
    tds_cal.slope = 1.0;
    tds_cal.offset = 0.0;
    tds_cal.is_calibrated = false;
    tds_cal.num_points = 0;

    preferences.putFloat("tds_slope", 1.0);
    preferences.putFloat("tds_offset", 0.0);
    preferences.putBool("tds_cal", false);
    preferences.putInt("tds_points", 0);

    Serial.println("   ✅ TDS calibration reset");
  }

  preferences.end();
  Serial.println("✅ Reset complete!\n");

  return true;
}


// ============================================================
//  Print calibration status
// ============================================================
void printCalibrationStatus() {
  Serial.println("\n📊 CALIBRATION STATUS");
  Serial.println("========================");

  // pH Status
  Serial.println("pH Sensor:");
  Serial.printf("  Calibrated: %s\n", ph_cal.is_calibrated ? "YES" : "NO");
  Serial.printf("  Points: %d\n", ph_cal.num_points);
  Serial.printf("  Slope: %.4f\n", ph_cal.slope);
  Serial.printf("  Offset: %.3f\n", ph_cal.offset);
  if (ph_cal.num_points >= 1) {
    Serial.printf("  Point 1: pH %.2f @ %.3fV\n", ph_cal.point1_ph, ph_cal.point1_voltage);
  }
  if (ph_cal.num_points >= 2) {
    Serial.printf("  Point 2: pH %.2f @ %.3fV\n", ph_cal.point2_ph, ph_cal.point2_voltage);
  }

  Serial.println("\nTDS Sensor:");
  Serial.printf("  Calibrated: %s\n", tds_cal.is_calibrated ? "YES" : "NO");
  Serial.printf("  Points: %d\n", tds_cal.num_points);
  Serial.printf("  Slope: %.4f\n", tds_cal.slope);
  Serial.printf("  Offset: %.2f ppm\n", tds_cal.offset);
  if (tds_cal.num_points >= 1) {
    Serial.printf("  Point 1: %.0f ppm @ %.3fV\n", tds_cal.point1_tds, tds_cal.point1_voltage);
  }
  if (tds_cal.num_points >= 2) {
    Serial.printf("  Point 2: %.0f ppm @ %.3fV\n", tds_cal.point2_tds, tds_cal.point2_voltage);
  }

  Serial.println("========================\n");
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
  float calibrated_ph = convertToPH();
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

const char * cmd_RESET_CALIBRATION = "RESET_CALIBRATION";

statusType runCommand(const char* cmdType, JsonObject doc, StaticJsonDocument<512>& prev) {
  // ★ pH Calibration Command
  if (strcmp(cmdType, cmd_PH_CALIBRATION) == 0) {
    float known_value = doc["known_value"] | 7.0;  // Default to pH 7 if not provided

    if (calibratePH(known_value)) {
      // Apply calibration
      float calibrated_ph = convertToPH();
      doc["ph"] = round(calibrated_ph * 100) / 100.0;
      return SUCCESS;
    } else {
      return FAILED;
    }
  }

  // ★ TDS Calibration Command
  else if (strcmp(cmdType, cmd_TDS_CALIBRATION) == 0) {
    float known_value = doc["known_value"] | 1382.0;  // Default to 1382 ppm

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

  // ★ Reset Calibration Command
  else if (strcmp(cmdType, cmd_RESET_CALIBRATION) == 0) {
    bool ph_ok = resetCalibration("PH");
    bool tds_ok = resetCalibration("TDS");

    if (ph_ok && tds_ok) {
      doc["reset"] = "ALL";
      doc["ph_calibrated"] = false;
      doc["tds_calibrated"] = false;
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