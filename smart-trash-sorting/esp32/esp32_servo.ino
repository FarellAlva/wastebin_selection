/**
 * ============================================================================
 * Smart Trash Sorting System - ESP32 Firmware (DUAL MODE: USB + BLUETOOTH)
 * ============================================================================
 * Board           : ESP32 Dev Module
 * Partition Scheme: Huge APP (3MB No OTA / 1MB SPIFFS) -> Wajib untuk Bluetooth!
 * Library Wajib   : ESP32Servo by Kevin Harrington (Install via Arduino Library Manager)
 * Library Bawaan  : BluetoothSerial (Bawaan ESP32)
 *
 * FITUR:
 * 1. Startup Self-Test: Servo berputar 180° -> 0° -> 90° (Standby) saat pertama menyala
 * 2. Dual Mode: Menerima perintah dari USB Serial (115200) & Bluetooth ("SmartTrash_ESP32")
 * 3. Logika Sudut:
 *    - DRY   : 30° (Kiri), tahan 2.5 detik, lalu otomatis kembali ke 90°
 *    - WET   : 150° (Kanan), tahan 2.5 detik, lalu otomatis kembali ke 90°
 *    - RESET : Langsung kembali ke 90° (Posisi Netral)
 * ============================================================================
 */

#include <ESP32Servo.h>
#include "BluetoothSerial.h"

// Inisialisasi Bluetooth Serial
BluetoothSerial SerialBT;

// Definisi Pin Hardware
const int SERVO_PIN = 18;            // Output PWM Servo pada GPIO 18
const int LED_PIN = 2;               // Built-in LED ESP32

// Konfigurasi Sudut Servo
const int ANGLE_DRY   = 30;          // Kiri: Sampah Kering (30°)
const int ANGLE_RESET = 90;          // Posisi Netral / Standby (90°)
const int ANGLE_WET   = 150;         // Kanan: Sampah Basah (150°)

// Durasi tahan pemilahan sebelum kembali ke netral (milidetik)
const unsigned long SORT_HOLD_DURATION_MS = 2500;

// Objek Servo
Servo trashServo;

// State Mesin Non-blocking
int currentAngle = ANGLE_RESET;
bool isSortingActive = false;
unsigned long sortStartTime = 0;

// Buffer perintah
String usbBuffer = "";
String btBuffer = "";

/**
 * Kirim pesan feedback ke USB Serial dan Bluetooth sekaligus
 */
void broadcastMessage(String msg) {
  Serial.println(msg);
  if (SerialBT.hasClient()) {
    SerialBT.println(msg);
  }
}

/**
 * Menggerakkan servo secara halus (smooth stepping)
 */
void moveToAngleSmooth(int target, int stepDelayMs = 8) {
  if (target == currentAngle) return;

  int step = (target > currentAngle) ? 1 : -1;
  while (currentAngle != target) {
    currentAngle += step;
    trashServo.write(currentAngle);
    delay(stepDelayMs);
  }
}

void setup() {
  // 1. Inisialisasi USB Serial
  Serial.begin(115200);
  delay(300);

  // 2. Inisialisasi LED Indikator
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  // 3. Inisialisasi Bluetooth
  SerialBT.begin("SmartTrash_ESP32");

  // 4. Inisialisasi Timer PWM untuk Servo
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  trashServo.setPeriodHertz(50);             // Standar 50Hz SG90
  trashServo.attach(SERVO_PIN, 500, 2400);    // Pulsa 500us - 2400us

  // ==============================================================
  // 5. STARTUP SELF-TEST SERVO MOTOR: 180° -> 0° -> 90° (STANDBY)
  // ==============================================================
  Serial.println("\n========================================================");
  Serial.println("[ESP32] Memulai Startup Self-Test Servo Motor...");
  
  // Test A: Putar ke 180 derajat
  Serial.println("[ESP32] [Self-Test] Gerak ke 180 derajat...");
  moveToAngleSmooth(180, 6);
  delay(800);

  // Test B: Putar ke 0 derajat
  Serial.println("[ESP32] [Self-Test] Gerak ke 0 derajat...");
  moveToAngleSmooth(0, 6);
  delay(800);

  // Test C: Kembali ke posisi Standby Netral (90 derajat)
  Serial.println("[ESP32] [Self-Test] Kembali ke Standby (90 derajat)...");
  moveToAngleSmooth(ANGLE_RESET, 6);
  delay(500);

  digitalWrite(LED_PIN, LOW);
  Serial.println("[ESP32] Self-Test Selesai! Servo siap di 90 derajat.");
  Serial.println("[ESP32] Bluetooth: 'SmartTrash_ESP32' | Baud: 115200");
  Serial.println("========================================================\n");
}

void loop() {
  // -------------------------------------------------------------
  // 1. BACA DARI USB SERIAL
  // -------------------------------------------------------------
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      usbBuffer.trim();
      if (usbBuffer.length() > 0) {
        processCommand(usbBuffer, "USB");
        usbBuffer = "";
      }
    } else {
      usbBuffer += c;
      if (usbBuffer.length() > 32) usbBuffer = "";
    }
  }

  // -------------------------------------------------------------
  // 2. BACA DARI BLUETOOTH WIRELESS
  // -------------------------------------------------------------
  while (SerialBT.available() > 0) {
    char c = (char)SerialBT.read();
    if (c == '\n' || c == '\r') {
      btBuffer.trim();
      if (btBuffer.length() > 0) {
        processCommand(btBuffer, "Bluetooth");
        btBuffer = "";
      }
    } else {
      btBuffer += c;
      if (btBuffer.length() > 32) btBuffer = "";
    }
  }

  // -------------------------------------------------------------
  // 3. TIMER OTOMATIS KEMBALI KE POSISI NETRAL (90°)
  // -------------------------------------------------------------
  if (isSortingActive) {
    if (millis() - sortStartTime >= SORT_HOLD_DURATION_MS) {
      broadcastMessage("[ESP32] Selesai memilah -> Kembali ke Netral (90 deg)...");
      moveToAngleSmooth(ANGLE_RESET, 8);
      isSortingActive = false;
      digitalWrite(LED_PIN, LOW);
      broadcastMessage("[ESP32] OK: Posisi netral siap (90 deg).");
    }
  }
}

/**
 * Eksekusi perintah DRY, WET, RESET
 */
void processCommand(String cmd, String source) {
  cmd.toUpperCase();

  if (cmd == "DRY") {
    digitalWrite(LED_PIN, HIGH);
    broadcastMessage("[ESP32] [" + source + "] DRY diterima -> Gerak KIRI (30 deg)...");
    moveToAngleSmooth(ANGLE_DRY, 7);
    isSortingActive = true;
    sortStartTime = millis();
    broadcastMessage("[ESP32] OK: Servo pada 30 deg (DRY).");
  }
  else if (cmd == "WET") {
    digitalWrite(LED_PIN, HIGH);
    broadcastMessage("[ESP32] [" + source + "] WET diterima -> Gerak KANAN (150 deg)...");
    moveToAngleSmooth(ANGLE_WET, 7);
    isSortingActive = true;
    sortStartTime = millis();
    broadcastMessage("[ESP32] OK: Servo pada 150 deg (WET).");
  }
  else if (cmd == "RESET") {
    digitalWrite(LED_PIN, LOW);
    broadcastMessage("[ESP32] [" + source + "] RESET diterima -> Kembali ke NETRAL (90 deg)...");
    moveToAngleSmooth(ANGLE_RESET, 7);
    isSortingActive = false;
    broadcastMessage("[ESP32] OK: Posisi 90 deg (NETRAL) aktif.");
  }
  else {
    broadcastMessage("[ESP32] ERROR [" + source + "]: Perintah '" + cmd + "' tidak dikenal. Gunakan: DRY, WET, RESET.");
  }
}
