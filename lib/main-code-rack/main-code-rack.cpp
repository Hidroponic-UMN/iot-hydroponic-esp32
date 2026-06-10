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
#define RACK_ID         3
#define DESC_DEVICE     "Buat Rack Hydroponic ke-3"

#define WIFI_SSID       "Seed"
#define WIFI_PASSWORD   "seedseed"

#define MQTT_SERVER     "10.213.121.73"
#define MQTT_PORT       1883
#define MQTT_USER       "esp32-3"
#define MQTT_PASSWORD   "rack3"

#define SEND_INTERVAL   5000 // in second, 5 second
#define TIME_OUT_INTERVAL   60000

// ============================================================
//  Pins Out
// ============================================================
#define SDA_PIN 21
#define SCL_PIN 22
#define ONE_WIRE_PIN 4
#define TDS_PIN 35
#define PH_PIN 33
#define US_TRIG_PIN 13
#define US_ECHO_PIN 14
#define FLOW_SENSOR_PIN 27

// ============================================================
//  Calibration Constants
// ============================================================
#define CALIBRATION_SAMPLES 50    // Number of readings to average
#define SAMPLE_DELAY 100          // Delay between samples (ms)
#define ECHO_TIMEOUT 30000        // Timeout for echo pulse (microseconds)
#define DELAY_FLOW_RATE 5000      // 5 second

// Conversion factors (adjust based on your sensor specs)
#define VREF 3.3                  // ESP32 ADC reference voltage
#define ADC_RESOLUTION 4096.0     // 12-bit ADC
#define SOUND_SPEED 0.0343        // Speed of sound in air at 20°C = 343 m/s = 0.0343 cm/µs
#define FLOW_CALIBRATION_FACTOR 450.0  // Pulses per liter (adjust based on your sensor)
#define TANDON_HEIGHT_FROM_US_SENSOR 54 // 54 cm

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

String mac_addr = "f4c1e01b-46e7-42c5-9f69-05d67a5a6a5b-3";
bool isRegistered = false;
char mqtt_topic[32];
char client_id[32];
unsigned long lastSend = 0;
unsigned long timeOut = 0;

char cmd_Topic[32];
char ack_cmd_Topic[48];
char signin_topic[32];
char signin_ack[32];

// Command definitions
const char * cmd_PH_CALIBRATION = "KALIBRASI_PH";
const char * cmd_TDS_CALIBRATION = "KALIBRASI_TDS";

// Last Data Storage
struct LastData {
  float prev_ph = 7.0;
};

LastData ld;

// ============================================================
//  Enhanced Calibration Storage
// ============================================================

// pH calibration data (two-point)
struct PHCalibration {
  int num_points = 0;
  bool is_calibrated = false;

  float offset = 0.0;
  float slope = 1.0;

  float point1_voltage = 2515.0;
  float point1_ph = 7.0;
  float point2_voltage = 3050.0;
  float point2_ph = 4.0;
};

// TDS calibration data (two-point)
struct TDSCalibration {
  int num_points = 0;
  bool is_calibrated = false;

  float slope = 1.0;
  float offset = 0.0;

  float point1_voltage = 0.0;
  float point1_tds = 0.0;
  float point2_voltage = 0.0;
  float point2_tds = 0.0;
};

// UltraSonic calibartion data (two-point)
struct UltraSonicCalibration {
  int num_points = 0;
  bool is_calibrated = false;

  float slope = 1.0;
  float offset = 0.0;

  float point1_voltage = 0.0;
  float point1_us = 0.0;
  float point2_voltage = 0.0;
  float point2_us = 0.0;
};

PHCalibration ph_cal;
TDSCalibration tds_cal;
UltraSonicCalibration us_cal;

// ============================================================
// Global Variables for Flow Rate Calculation
// ============================================================
uint32_t pulseCount = 0;                // Total pulses counted
uint32_t lastPulseTime = 0;             // Timestamp of last pulse
float flowRate = 0.0;                   // Current flow rate in L/min
float totalVolume = 0.0;                // Total volume in liters
unsigned long flowStartTime = 0;        // Start time for flow measurement

// ============================================================
// INTERRUPT SERVICE ROUTINE (ISR)
// Called every time a pulse is detected
// ============================================================
void IRAM_ATTR flowSensorISR() {
  pulseCount++;
  lastPulseTime = millis();
}

// ============================================================
//  Load last data
// ============================================================
void loadPreviousData() {
  preferences.begin("prev", false);
  ld.prev_ph = preferences.getFloat("prev_ph", 0);
  preferences.end();
}

void savePreviousData() {
  preferences.begin("prev", false);
  preferences.putFloat("prev_ph", ld.prev_ph);
  preferences.end();
}

// ============================================================
//  Load calibration data from Preferences
// ============================================================
void loadCalibrationData() {
  preferences.begin("calibration", false);

  // Load pH calibration
  ph_cal.slope = preferences.getFloat("ph_slope", 1.0);
  ph_cal.offset = preferences.getFloat("ph_offset", 0.0);
  ph_cal.is_calibrated = preferences.getBool("ph_cal", false);
  ph_cal.num_points = preferences.getInt("ph_points", 0);
  ph_cal.point1_voltage = preferences.getFloat("ph_p1_v", 2515.0);
  ph_cal.point1_ph = preferences.getFloat("ph_p1_ph", 7.0);
  ph_cal.point2_voltage = preferences.getFloat("ph_p2_v", 3050.0);
  ph_cal.point2_ph = preferences.getFloat("ph_p2_ph", 4.0);

  // Load TDS calibration
  tds_cal.slope = preferences.getFloat("tds_slope", 1.0);
  tds_cal.offset = preferences.getFloat("tds_offset", 0.0);
  tds_cal.is_calibrated = preferences.getBool("tds_cal", false);
  tds_cal.num_points = preferences.getInt("tds_points", 0);
  tds_cal.point1_voltage = preferences.getFloat("tds_p1_v", 0.0);
  tds_cal.point1_tds = preferences.getFloat("tds_p1_tds", 0.0);
  tds_cal.point2_voltage = preferences.getFloat("tds_p2_v", 0.0);
  tds_cal.point2_tds = preferences.getFloat("tds_p2_tds", 0.0);

  // Load UltraSonic calibration
  us_cal.slope = preferences.getFloat("us_slope", 1.0);
  us_cal.offset = preferences.getFloat("us_offset", 0.0);
  us_cal.is_calibrated = preferences.getBool("us_cal", false);
  us_cal.num_points = preferences.getInt("us_points", 0);
  us_cal.point1_voltage = preferences.getFloat("us_p1_v", 0.0);
  us_cal.point1_us = preferences.getFloat("us_p1_us", 0.0);
  us_cal.point2_voltage = preferences.getFloat("us_p2_v", 0.0);
  us_cal.point2_us = preferences.getFloat("us_p2_us", 0.0);

  preferences.end();
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

  // Save UltraSonic calibration
  preferences.putFloat("us_slope", us_cal.slope);
  preferences.putFloat("us_offset", us_cal.offset);
  preferences.putBool("us_cal", us_cal.is_calibrated);
  preferences.putInt("us_points", us_cal.num_points);
  preferences.putFloat("us_p1_v", us_cal.point1_voltage);
  preferences.putFloat("us_p1_us", us_cal.point1_us);
  preferences.putFloat("us_p2_v", us_cal.point2_voltage);
  preferences.putFloat("us_p2_us", us_cal.point2_us);

  preferences.end();
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

float readADCFilterVoltage(int pin) {
    const int NUM_SAMPLES = 20;        // Increased from 10 for better stability
    const int DISCARD_SAMPLES = 4;     // Discard 4 lowest + 4 highest
    const float ALPHA = 0.30;          // EMA filter coefficient (0.1-0.3)
    static float ema_voltage = 0;      // Exponential Moving Average
    static bool ema_initialized = false;

    int samples[NUM_SAMPLES];

    // 1. Collect samples with delay for ADC settling
    for (int i = 0; i < NUM_SAMPLES; i++) {
        samples[i] = analogRead(pin);
        delay(60);  // ADC settling time
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

    // // 6. Apply Exponential Moving Average (EMA) filter
    // if (!ema_initialized) {
    //     ema_voltage = voltage;
    //     ema_initialized = true;
    // } else {
    //     ema_voltage = (ALPHA * voltage) + ((1.0 - ALPHA) * ema_voltage);
    // }

    // // 7. Optional: Apply median filter on final result
    // static float voltage_history[5] = {0};
    // static int history_index = 0;

    // voltage_history[history_index] = ema_voltage;
    // history_index = (history_index + 1) % 5;

    // // Sort history for median
    // float sorted_history[5];
    // memcpy(sorted_history, voltage_history, sizeof(voltage_history));
    // for (int i = 0; i < 4; i++) {
    //     for (int j = i + 1; j < 5; j++) {
    //         if (sorted_history[i] > sorted_history[j]) {
    //             float temp = sorted_history[i];
    //             sorted_history[i] = sorted_history[j];
    //             sorted_history[j] = temp;
    //         }
    //     }
    // }

    // Return median value (middle of 5 samples)
    return voltage;
}

// ============================================================
//  ★ IMPROVED pH CONVERSION ★
//  Uses two-point calibration (slope + offset)
// ============================================================
float convertToPH() {
  // Convert ADC to voltage
  float voltage = readADCFilterVoltage(PH_PIN);
  Serial.print("Voltage pH: ");
  Serial.println(voltage);
  float ph_base = (ph_cal.slope * voltage) + ph_cal.offset;
  return ph_base;  // Return uncalibrated if not calibrated
}


// ============================================================
//  ★ IMPROVED TDS CONVERSION ★
//  Uses two-point calibration (slope + offset)
// ============================================================
float averageTDSVoltage() {
  int sampling_number = 10;
  float total_comp_vol = 0;

  for (int i = 1; i <= sampling_number ; i++) {
    delay(60);
    float temperature = watertemp.getTempCByIndex(0);
    int raw_tds = analogRead(TDS_PIN);
    // Convert ADC to voltage
    float voltage = rawToVoltage(raw_tds);

    // Temperature compensation
    float compensationCoefficient = 1.0 + 0.02 * (temperature - 25.0);
    float compensationVoltage = voltage / compensationCoefficient;
    total_comp_vol += compensationVoltage;
  }
  total_comp_vol = total_comp_vol / sampling_number;
  return total_comp_vol;
}

float convertToTDS() {
  float compensationVoltage = averageTDSVoltage();
  float tds_base = (133.42 * compensationVoltage * compensationVoltage * compensationVoltage
                      - 255.86 * compensationVoltage * compensationVoltage
                      + 857.39 * compensationVoltage) * 0.5;
  float real_tds = (tds_cal.slope * tds_base) + tds_cal.offset;
  return real_tds;
}

// ============================================================
// Read Current Flow Rate (L/min)
// ============================================================
float readFlowRate() {
  // Read current pulse count
  uint32_t currentPulses = pulseCount;
  Serial.print("Pulse Count: ");
  Serial.println(pulseCount);

  // Calculate volume from pulses
  float volume = currentPulses / FLOW_CALIBRATION_FACTOR;

  // Calculate time elapsed in minutes
  unsigned long elapsedTime = millis() - flowStartTime;
  float elapsedMinutes = elapsedTime / 60000.0;  // Convert milliseconds to minutes

  // Avoid division by zero
  if (elapsedMinutes < 0.001) {
    return 0.0;
  }

  // Flow rate = Volume / Time
  flowRate = volume / elapsedMinutes;
  return flowRate;
}

// ============================================================
// Reset Flow Measurement
// Call this to start a new flow measurement
// ============================================================
void resetFlowMeasurement() {
  pulseCount = 0;
  totalVolume = 0.0;
  flowRate = 0.0;
  flowStartTime = millis();
  lastPulseTime = millis();
}


float readUltraSonicSensor() {
  // Send 10µs pulse on trigger pin
  digitalWrite(US_TRIG_PIN, LOW);
  delayMicroseconds(4);  // Ensure LOW for at least 2µs

  digitalWrite(US_TRIG_PIN, HIGH);
  delayMicroseconds(10);  // HIGH for 10µs
  digitalWrite(US_TRIG_PIN, LOW);

  // Measure the duration of the echo pulse
  // pulseIn() waits for pin to go HIGH, then measures how long it stays HIGH
  unsigned long echoPulse = pulseIn(US_ECHO_PIN, HIGH, ECHO_TIMEOUT);

  if (echoPulse == 0) {
    Serial.println("Error: No echo received (timeout)");
    return 0.0;
  }

  // Calculate distance using the echo pulse width
  // Distance (cm) = (Echo_time_in_µs / 2) * speed_of_sound_in_cm/µs
  // Divided by 2 because sound travels to object AND back
  float distance_in_cm = (echoPulse / 2.0) * SOUND_SPEED;
  if (distance_in_cm < 2.0 || distance_in_cm > 450.0) {
    Serial.printf("Warning: Distance out of range: %.2f cm\n", distance_in_cm);
    return 0.0;
  }
  return distance_in_cm;
}

float readUltraSonicSensorAverage() {
  float distanceSum = 0;
  uint8_t count = 0;

  float tmp = 0.0;
  for (uint8_t i=1;i<=10;i++) {
    delay(60);
    tmp = readUltraSonicSensor();
    if (tmp != 0.0) {
      distanceSum += tmp;
      count++;
    }
  }

  if (count > 0) {
    distanceSum = (distanceSum / (float)count);
    Serial.print("Distance in average: ");
    Serial.println(distanceSum);
    float distanceInCM = TANDON_HEIGHT_FROM_US_SENSOR - distanceSum;
    return distanceInCM;
  } else {
    Serial.println("❌ No valid ultrasonic readings!");
    return 0.0;  // Return 0 instead of dividing by zero
  }
}

float readLightIntensity() {
  // Try to read from BH1750
  float lux = luxmeter.readLightLevel();

  // BH1750 returns 65535 or < 0 on error or if not configured
  if (lux == 65535.0 || lux < 0.0) {
    Serial.println("⚠️ BH1750 reading error - check sensor connection");
    return -1.0;  // Return -1 to indicate error
  }

  return lux;
}


// ============================================================
//  ★ IMPROVED pH CALIBRATION ★
//  Supports both one-point and two-point calibration
// ============================================================
bool calibratePH(float known_ph_value, float known_voltage = -1) {
  Serial.println("\n🧪 Starting pH Calibration...");
  Serial.printf("   Target pH: %.2f\n", known_ph_value);
  Serial.printf("   Current calibration points: %d\n", ph_cal.num_points);
  Serial.println("   Taking readings...");

  float voltage = readADCFilterVoltage(PH_PIN);
  if (known_voltage != -1) {
    voltage = known_voltage;
  }
  float base_ph = (voltage * ph_cal.slope) + ph_cal.offset;

  Serial.printf("   Voltage: %.3f mV\n", voltage);
  Serial.printf("   Base pH (uncalibrated): %.2f\n", base_ph);

  // Determine if this is first or second calibration point
  if (ph_cal.num_points == 0 || known_ph_value == 4.0) {
    Serial.println("   → Setting as calibration point 1");

    ph_cal.point1_voltage = voltage;
    ph_cal.point1_ph = known_ph_value;
    ph_cal.num_points = 1;

    Serial.printf("   One-point calibration applied\n");
    Serial.printf("   Offset: %.3f\n", ph_cal.offset);

  } else if (ph_cal.num_points == 1 || known_ph_value == 7.0) {
    Serial.println("   → Setting as calibration point 2");

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
    return false;
  }

  // Calculate two-point calibration
  // Map voltage range to pH range
  calculateTwoPointCalibration(
    ph_cal.point1_voltage, ph_cal.point1_ph,
    ph_cal.point2_voltage, ph_cal.point2_ph,
    ph_cal.slope, ph_cal.offset
  );

  saveCalibrationData();
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

  float voltage = averageTDSVoltage();
  float base_tds = convertToTDS();

  Serial.printf("   Voltage: %.3f V\n", voltage);
  Serial.printf("   Base TDS (uncalibrated): %.2f ppm\n", base_tds);

  if (tds_cal.num_points == 0 || tds_cal.num_points == 1 ) {
    // First calibration point
    Serial.println("   → Setting as calibration point 1");

    tds_cal.point1_voltage = voltage;
    tds_cal.point1_tds = known_tds_value;
    tds_cal.num_points = 1;

    // One-point calibration
    tds_cal.slope = 1.0;
    tds_cal.offset = known_tds_value - base_tds;
    tds_cal.is_calibrated = true;

    Serial.printf("   One-point calibration applied\n");
    Serial.printf("   Offset: %.2f ppm\n", tds_cal.offset);

  }

  saveCalibrationData();
  return true;
}

// ============================================================
//  ★ IMPROVED ULTRASONIC CALIBRATION ★
//  Supports both one-point and two-point calibration
// ============================================================
bool calibrateUS(float known_us_value, float base_value_us) {
  Serial.println("\n🧪 Starting US Calibration...");
  Serial.printf("   Target US: %.2f\n", known_us_value);
  Serial.printf("   Current calibration points: %d\n", us_cal.num_points);
  Serial.println("   Taking readings...");

  float base_us = base_value_us;
  Serial.printf("   Base US (uncalibrated): %.2f cm\n", base_us);

  if (us_cal.num_points == 0) {
    Serial.println("   → Setting as calibration point 1");
    us_cal.point1_voltage = base_us;
    us_cal.point1_us = known_us_value;
    us_cal.num_points = 1;
    Serial.printf("   One-point calibration applied\n");
  } else if (us_cal.num_points == 1) {
    Serial.println("   → Setting as calibration point 2");
    us_cal.point2_voltage = base_us;
    us_cal.point2_us = known_us_value;
    us_cal.num_points = 2;
  } else {
    return false;
  }

  calculateTwoPointCalibration(
    us_cal.point1_voltage, us_cal.point1_us,
    us_cal.point2_voltage, us_cal.point2_us,
    us_cal.slope, us_cal.offset
  );
  saveCalibrationData();
  return true;
}

// ============================================================
//  Reset calibration to factory defaults
// ============================================================
bool resetCalibration(const char* sensor_type) {
  Serial.printf("\n🔄 Resetting %s calibration...\n", sensor_type);

  preferences.begin("calibration", false);

  if (strcmp(sensor_type, "PH") == 0 || strcmp(sensor_type, "ALL") == 0) {
    preferences.putFloat("ph_slope", 1.0);
    preferences.putFloat("ph_offset", 0.0);
    preferences.putBool("ph_cal", false);
    preferences.putInt("ph_points", 0);
    preferences.putFloat("ph_p1_v", 0.0);
    preferences.putFloat("ph_p1_ph", 0.0);
    preferences.putFloat("ph_p2_v", 0.0);
    preferences.putFloat("ph_p2_ph", 0.0);

    Serial.println("   ✅ pH calibration reset");
  }

  if (strcmp(sensor_type, "TDS") == 0 || strcmp(sensor_type, "ALL") == 0) {
    preferences.putFloat("tds_slope", 1.0);
    preferences.putFloat("tds_offset", 0.0);
    preferences.putBool("tds_cal", false);
    preferences.putInt("tds_points", 0);
    preferences.putFloat("tds_p1_v", 0.0);
    preferences.putFloat("tds_p1_tds", 0.0);
    preferences.putFloat("tds_p2_v", 0.0);
    preferences.putFloat("tds_p2_tds", 0.0);

    Serial.println("   ✅ TDS calibration reset");
  }

  if (strcmp(sensor_type, "US") == 0 || strcmp(sensor_type, "ALL") == 0) {
    preferences.putFloat("us_slope", 1.0);
    preferences.putFloat("us_offset", 0.0);
    preferences.putBool("us_cal", false);
    preferences.putInt("us_points", 0);
    preferences.putFloat("us_p1_v", 0.0);
    preferences.putFloat("us_p1_us", 0.0);
    preferences.putFloat("us_p2_v", 0.0);
    preferences.putFloat("us_p2_us", 0.0);

    Serial.println("   ✅ US calibration reset");
  }

  preferences.end();
  Serial.println("✅ Reset complete!\n");
  loadCalibrationData();

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
  Serial.printf("  point1_ph: %.3f\n", ph_cal.point1_ph);
  Serial.printf("  point2_ph: %.3f\n", ph_cal.point2_ph);
  Serial.printf("  point1_voltage: %.3f\n", ph_cal.point1_voltage);
  Serial.printf("  point2_voltage: %.3f\n", ph_cal.point2_voltage);
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

  // Apply calibration
  float calibrated_ph = convertToPH();
  float calibrated_tds = convertToTDS();

  resetFlowMeasurement();
  delay(DELAY_FLOW_RATE);

  float tmp = round(calibrated_ph * 100) / 100.0;
  if (tmp < 0.0) {
    tmp = 0.0;
  } else if (tmp > 14.0) {
    tmp = 14.0;
  } else if (tmp < -2.5 || tmp > 16.5) {
    loadPreviousData();
    tmp = ld.prev_ph;
  }

  ld.prev_ph = tmp;
  savePreviousData();

  doc["ph"] = tmp;
  doc["ec"] = round(calibrated_tds * 100) / 100.0;
  doc["water_temp"] = round(temperature * 10) / 10.0;
  doc["light_intensity"] = readLightIntensity();
  doc["water_level"] = readUltraSonicSensorAverage();
  doc["flow_rate"] = round(readFlowRate() * 100) / 100.0;
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
  while (WiFi.status() != WL_CONNECTED && attempts < 5) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n✅ WiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\n❌ WiFi connection failed! Retrying ...");
    delay(50);
  }
}

// ============================================================
//  MQTT connection
// ============================================================
void connectMQTT() {
  if (mqtt.connected()) return;

  Serial.printf("🔌 Connecting to MQTT: %s:%d...\n", MQTT_SERVER, MQTT_PORT);

  int attempt = 0;
  while (!mqtt.connected() && attempt < 5) {
    if (mqtt.connect(client_id, MQTT_USER, MQTT_PASSWORD)) {
      Serial.printf("✅ MQTT connected as '%s'\n", client_id);
      Serial.printf("📤 Publishing to topic: %s\n", mqtt_topic);

      mqtt.subscribe(cmd_Topic);
      mqtt.subscribe(signin_ack);
      Serial.printf("📥 Subscribing to topic: %s\n\n", cmd_Topic);
      delay(60);
    } else {
      Serial.printf("❌ MQTT failed (rc=%d). Retrying in 3s...\n", mqtt.state());
      attempt++;
      delay(60);
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
      float calibrated_tds = convertToTDS();
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


  // UltraSonic first calibration
  // calibrateUS(1.5, 23.93);
  //UltraSonic second calibration
  // calibrateUS(20.0, 20.46);

  printCalibrationStatus();
  loadCalibrationData();

  // Begin sensors
  Wire.begin(SDA_PIN, SCL_PIN);
  if (luxmeter.begin()) {
    Serial.println("✅ BH1750 initialized successfully!");
    // Set mode for continuous measurement
    luxmeter.configure(BH1750::CONTINUOUS_HIGH_RES_MODE);
  } else {
    Serial.println("❌ BH1750 initialization failed! Check I2C connection.");
    Serial.println("   - Verify SDA (GPIO 21) and SCL (GPIO 22) connections");
    Serial.println("   - Check if BH1750 address is 0x23 or 0x5C");
  }

  watertemp.begin();
  pinMode(PH_PIN, INPUT);
  pinMode(TDS_PIN, INPUT);

  pinMode(US_TRIG_PIN, OUTPUT);
  pinMode(US_ECHO_PIN, INPUT);
  digitalWrite(US_TRIG_PIN, LOW);

  pinMode(FLOW_SENSOR_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), flowSensorISR, RISING);
  // Initialize variables
  pulseCount = 0;
  lastPulseTime = 0;
  flowRate = 0.0;
  totalVolume = 0.0;
  flowStartTime = millis();

  // Connect
  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback(callBack);
  mqtt.setBufferSize(512); // <--- INCREASE BUFFER SIZE
  connectWiFi();
  connectMQTT();
}

// ============================================================
//  Main loop
// ============================================================
void loop() {
  // Ensure connections
  connectWiFi();
  connectMQTT();
  mqtt.loop();

  if (!isRegistered) {
    registerDevice();
  } else {
    // Send data at interval
    if (millis() - lastSend >= SEND_INTERVAL) {

      // Build JSON payload
      StaticJsonDocument<512> root;
      root["mac_addr"] = mac_addr;
      JsonObject data = root["data"].to<JsonObject>();
      generateData(data);

      char payload[512];
      serializeJson(root, payload);
      Serial.println("JSON Payload:");
      Serial.println(payload);
      Serial.println();

      // Publish to MQTT
      if (mqtt.publish(mqtt_topic, payload)) {
        Serial.println("✅ Publish success");
      } else {
        Serial.println("❌ Publish failed!");
      }

      // Update lastSend AFTER generateData to avoid loop starvation
      lastSend = millis();
    }
  }
}