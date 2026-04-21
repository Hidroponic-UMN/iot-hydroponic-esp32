#include "main-code-rack.hpp"

// #if defined(ESP8266)
//     #include <ESP8266WiFi.h>
// #elif defined(ESP32)
//     #include <WiFi.h>
// #endif
// #include <Wire.h>
// #include <BH1750.h>
// #include <PubSubClient.h>
// #include <ArduinoJson.h>
// #include <OneWire.h>
// #include <DallasTemperature.h>
// #include <Preferences.h>

// // Preferences untuk menyimpan kalibrasi di flash memory
// Preferences preferences;

// // Pin sensor pH
// #define PH_SENSOR_PIN 33

// // Variabel kalibrasi
// float acidVoltage = 2355.0;      // Voltage untuk pH 4.0 (default)
// float neutralVoltage = 2310.0;    // Voltage untuk pH 7.0 (default)
// float slope = 0.0;                // Slope untuk perhitungan pH
// float intercept = 0.0;            // Intercept untuk perhitungan pH

// /*
// pH 7 voltage 2300 mV
// --- Kalibrasi pH 7.0 ---
// Masukkan sensor ke larutan buffer pH 7.0
// Tunggu 30 detik untuk stabilisasi...
// Membaca voltage...
// Voltage pH 7.0: 2316.15 mV

// pH 4.01 voltage 2360 mV
// Voltage: 2354.19 mV | pH: 7.40
// Voltage: 2355.80 mV | pH: 7.42
// Voltage: 2358.35 mV | pH: 7.45
// Voltage: 2359.96 mV | pH: 7.46
// Voltage: 2357.01 mV | pH: 7.43
// Voltage: 2361.31 mV | pH: 7.48
// Voltage: 2355.93 mV | pH: 7.42
// Voltage: 2359.02 mV | pH: 7.45
// Voltage: 2361.98 mV | pH: 7.48
// Voltage: 2352.84 mV | pH: 7.39
// Voltage: 2357.14 mV | pH: 7.43
// Voltage: 2360.77 mV | pH: 7.47
// Voltage: 2359.02 mV | pH: 7.45
// Voltage: 2372.05 mV | pH: 7.59
// Voltage: 2354.19 mV | pH: 7.40
// Voltage: 2357.81 mV | pH: 7.44
// Voltage: 2359.69 mV | pH: 7.46
// Voltage: 2355.93 mV | pH: 7.42
// Voltage: 2355.26 mV | pH: 7.41

// ph
// */

// // Variabel pembacaan
// unsigned long int avgval;
// int buffer_arr[10], temp;
// float ph_act;

// // Hitung slope dan intercept dari 2 titik kalibrasi
// void calculateSlopeIntercept() {
//     // pH 4.0 -> acidVoltage
//     // pH 7.0 -> neutralVoltage
//     // Formula linear: pH = slope * voltage + intercept

//     slope = (7.0 - 4.0) / (neutralVoltage - acidVoltage);
//     intercept = 7.0 - (slope * neutralVoltage);

//     Serial.println("\n--- Parameter Kalibrasi ---");
//     Serial.print("Slope: ");
//     Serial.println(slope, 6);
//     Serial.print("Intercept: ");
//     Serial.println(intercept, 6);
//     Serial.println();
// }

// // Simpan kalibrasi ke flash memory
// void saveCalibration() {
//     preferences.begin("ph-sensor", false);
//     preferences.putFloat("acidVolt", acidVoltage);
//     preferences.putFloat("neutralVolt", neutralVoltage);
//     preferences.putFloat("slope", slope);
//     preferences.putFloat("intercept", intercept);
//     preferences.end();

//     Serial.println("Kalibrasi disimpan ke memory!");
// }

// // Fungsi untuk membaca voltage sensor
// float readVoltage() {
//     const int NUM_SAMPLES = 20;        // Increased from 10 for better stability
//     const int DISCARD_SAMPLES = 4;     // Discard 4 lowest + 4 highest
//     const float ALPHA = 0.30;          // EMA filter coefficient (0.1-0.3)
//     static float ema_voltage = 0;      // Exponential Moving Average
//     static bool ema_initialized = false;

//     int samples[NUM_SAMPLES];

//     // 1. Collect samples with delay for ADC settling
//     for (int i = 0; i < NUM_SAMPLES; i++) {
//         samples[i] = analogRead(PH_SENSOR_PIN);
//         delay(20);  // ADC settling time
//     }

//     // 2. Sort samples (bubble sort)
//     for (int i = 0; i < NUM_SAMPLES - 1; i++) {
//         for (int j = i + 1; j < NUM_SAMPLES; j++) {
//             if (samples[i] > samples[j]) {
//                 int temp = samples[i];
//                 samples[i] = samples[j];
//                 samples[j] = temp;
//             }
//         }
//     }

//     // 3. Remove outliers - discard lowest and highest values
//     int sum = 0;
//     int count = 0;
//     for (int i = DISCARD_SAMPLES; i < NUM_SAMPLES - DISCARD_SAMPLES; i++) {
//         sum += samples[i];
//         count++;
//     }

//     // 4. Calculate average of middle values
//     float avgValue = sum / (float)count;

//     // 5. Convert to voltage (mV)
//     float voltage = avgValue * (3300.0 / 4095.0);

//     // 6. Apply Exponential Moving Average (EMA) filter
//     if (!ema_initialized) {
//         ema_voltage = voltage;
//         ema_initialized = true;
//     } else {
//         ema_voltage = (ALPHA * voltage) + ((1.0 - ALPHA) * ema_voltage);
//     }

//     // 7. Optional: Apply median filter on final result
//     static float voltage_history[5] = {0};
//     static int history_index = 0;

//     voltage_history[history_index] = ema_voltage;
//     history_index = (history_index + 1) % 5;

//     // Sort history for median
//     float sorted_history[5];
//     memcpy(sorted_history, voltage_history, sizeof(voltage_history));
//     for (int i = 0; i < 4; i++) {
//         for (int j = i + 1; j < 5; j++) {
//             if (sorted_history[i] > sorted_history[j]) {
//                 float temp = sorted_history[i];
//                 sorted_history[i] = sorted_history[j];
//                 sorted_history[j] = temp;
//             }
//         }
//     }

//     // Return median value (middle of 5 samples)
//     return sorted_history[2];
// }

// // Fungsi untuk menghitung pH dari voltage
// float calculatePH(float voltage) {
//     static float tempSlope = (7.0 - 4.0) / (neutralVoltage - acidVoltage);
//     intercept = 7.0 - (tempSlope * neutralVoltage);
//     slope = tempSlope;
//     return (tempSlope * voltage) + intercept;
// }

// // Fungsi kalibrasi untuk larutan pH 4.0
// void calibratePH4() {
//     Serial.println("\n--- Kalibrasi pH 4.0 ---");
//     Serial.println("Masukkan sensor ke larutan buffer pH 4.0");
//     Serial.println("Tunggu 30 detik untuk stabilisasi...");

//     delay(30000);

//     Serial.println("Membaca voltage...");
//     float totalVoltage = 0;
//     for (int i = 0; i < 10; i++) {
//         totalVoltage += readVoltage();
//         delay(500);
//     }

//     acidVoltage = totalVoltage / 10.0;

//     Serial.print("Voltage pH 4.0: ");
//     Serial.print(acidVoltage, 2);
//     Serial.println(" mV");

//     // Hitung ulang slope dan intercept
//     calculateSlopeIntercept();

//     // Simpan ke memory
//     saveCalibration();

//     Serial.println("Kalibrasi pH 4.0 selesai!\n");
// }

// // Fungsi kalibrasi untuk larutan pH 7.0
// void calibratePH7() {
//     Serial.println("\n--- Kalibrasi pH 7.0 ---");
//     Serial.println("Masukkan sensor ke larutan buffer pH 7.0");
//     Serial.println("Tunggu 30 detik untuk stabilisasi...");

//     delay(30000);

//     Serial.println("Membaca voltage...");
//     float totalVoltage = 0;
//     for (int i = 0; i < 10; i++) {
//         totalVoltage += readVoltage();
//         delay(500);
//     }

//     neutralVoltage = totalVoltage / 10.0;

//     Serial.print("Voltage pH 7.0: ");
//     Serial.print(neutralVoltage, 2);
//     Serial.println(" mV");

//     // Hitung ulang slope dan intercept
//     calculateSlopeIntercept();

//     // Simpan ke memory
//     saveCalibration();

//     Serial.println("Kalibrasi pH 7.0 selesai!\n");
// }

// // Load kalibrasi dari flash memory
// void loadCalibration() {
//     preferences.begin("ph-sensor", true);
//     // acidVoltage = preferences.getFloat("acidVolt", 2032.44);
//     // neutralVoltage = preferences.getFloat("neutralVolt", 1500.0);
//     // slope = preferences.getFloat("slope", 0.0);
//     // intercept = preferences.getFloat("intercept", 0.0);
//     preferences.end();

//     Serial.println("Kalibrasi dimuat dari memory:");
//     Serial.print("  pH 4.0 Voltage: ");
//     Serial.print(acidVoltage, 2);
//     Serial.println(" mV");
//     Serial.print("  pH 7.0 Voltage: ");
//     Serial.print(neutralVoltage, 2);
//     Serial.println(" mV");
//     Serial.print("  Slope: ");
//     Serial.println(slope, 6);
//     Serial.print("  Intercept: ");
//     Serial.println(intercept, 6);
//     Serial.println();
// }

// // Reset kalibrasi ke default
// void resetCalibration() {
//     Serial.println("\nReset kalibrasi...");
//     acidVoltage = 2360.0;
//     neutralVoltage = 2300.0;
//     slope = 0.0;
//     intercept = 0.0;
//     saveCalibration();
//     Serial.println("Kalibrasi direset!\n");
// }

// // Tampilkan menu
// void printMenu() {
//     Serial.println("========== MENU ==========");
//     Serial.println("4 - Kalibrasi pH 4.0");
//     Serial.println("7 - Kalibrasi pH 7.0");
//     Serial.println("s - Lihat status kalibrasi");
//     Serial.println("r - Reset kalibrasi");
//     Serial.println("m - Tampilkan menu ini");
//     Serial.println("==========================\n");
// }

// // Handle command dari Serial
// void handleCommand(char cmd) {
//     switch(cmd) {
//         case '4':
//             calibratePH4();
//             printMenu();
//             break;
//         case '7':
//             calibratePH7();
//             printMenu();
//             break;
//         case 's':
//             Serial.println("\n--- Status Kalibrasi ---");
//             Serial.print("pH 4.0 Voltage: ");
//             Serial.print(acidVoltage, 2);
//             Serial.println(" mV");
//             Serial.print("pH 7.0 Voltage: ");
//             Serial.print(neutralVoltage, 2);
//             Serial.println(" mV");
//             Serial.print("Slope: ");
//             Serial.println(slope, 6);
//             Serial.print("Intercept: ");
//             Serial.println(intercept, 6);
//             Serial.println();
//             break;
//         case 'r':
//             resetCalibration();
//             printMenu();
//             break;
//         case 'm':
//             printMenu();
//             break;
//         default:
//             // Abaikan karakter lain (seperti newline)
//             break;
//     }
// }

// void setup() {
//     Wire.begin();
//     Serial.begin(115200);

//     delay(1000);
//     Serial.println("\n\n=================================");
//     Serial.println("pH Sensor Calibration System");
//     Serial.println("=================================\n");

//     // Load kalibrasi dari memory
//     // loadCalibration();

//     // Tampilkan menu
//     printMenu();
// }

// void loop() {
//     // Cek apakah ada input dari Serial
//     if (Serial.available() > 0) {
//         char cmd = Serial.read();
//         handleCommand(cmd);
//     }

//     // Baca dan tampilkan pH setiap 2 detik
//     static unsigned long lastRead = 0;
//     if (millis() - lastRead > 2000) {
//         lastRead = millis();
//         float voltage = readVoltage();
//         ph_act = calculatePH(voltage);

//         Serial.print("Voltage: ");
//         Serial.print(voltage, 2);
//         Serial.print(" mV | pH: ");
//         Serial.println(ph_act, 2);
//     }
// }