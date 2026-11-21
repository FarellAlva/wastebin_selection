/**
 * ============================================================================
 * Smart Trash Sorting System - ESP32 Firmware (DUAL MODE: USB + BLUETOOTH)
 * ============================================================================
 * Board           : ESP32 Dev Module / NodeMCU-32S
 * Library Wajib   : ESP32Servo by Kevin Harrington (Install via Arduino Library Manager)
 * Library Bawaan  : BluetoothSerial (Sudah bawaan core ESP32)
 *
 * FITUR DUAL MODE:
 * 1. USB Serial      : Baud rate 115200 bps (pakai kabel USB)
 * 2. Wireless BT     : Bluetooth Classic SPP dengan nama "SmartTrash_ESP32"
 *                      -> ESP32 tidak perlu colok kabel ke laptop!
 *                      -> Cukup colok ke power bank / adaptor 5V di tempat sampah.
 *
 * PENGATURAN ARDUINO IDE SEBELUM UPLOAD:
 * - Board            : ESP32 Dev Module
 * - Upload Speed     : 115200 atau 921600
 * - Partition Scheme : Huge APP (3MB No OTA / 1MB SPIFFS) -> PENTING untuk Bluetooth!
 *
 * PENGKABELAN:
 * - Servo Signal     : GPIO 18 (PWM Output)
 * - Servo VCC        : External 5V (+) (Dilarang dari 3.3V ESP32)
 * - Servo GND        : External GND (-)
 * - Common GND       : GND External disatukan dengan pin GND ESP32
 * ============================================================================
 */

#include <ESP32Servo.h>
#include "BluetoothSerial.h"

// Cek apakah Bluetooth diaktifkan di core ESP32
#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error "Bluetooth tidak aktif pada konfigurasi core ESP32 ini!"
#endif

// Objek Bluetooth Serial
BluetoothSerial SerialBT;

// Definisi Pin & Parameter Hardware
const int SERVO_PIN = 18;            // Output PWM Servo pada GPIO 18
const int LED_PIN = 2;               // Built-in LED ESP32

// Konfigurasi Sudut Servo
const int ANGLE_DRY   = 30;          // Kiri: Sampah Kering
const int ANGLE_RESET = 90;          // Posisi awal / Netral
const int ANGLE_WET   = 150;         // Kanan: Sampah Basah

// Durasi Tahan Pemilahan (Milidetik) sebelum otomatis kembali ke netral
const unsigned long SORT_HOLD_DURATION_MS = 2500;

// Objek Servo
Servo trashServo;

// Variabel State Mesin Non-blocking
int currentAngle = ANGLE_RESET;
bool isSortingActive = false;
unsigned long sortStartTime = 0;

// Buffer string terpisah untuk USB dan Bluetooth
String usbBuffer = "";
String btBuffer = "";

/**
 * Kirim feedback ke USB Serial dan Bluetooth sekaligus
 */
void broadcastMessage(String msg) {
  Serial.println(msg);
  if (SerialBT.hasClient()) {
    SerialBT.println(msg);
  }
}

/**
 * Menggerakkan servo secara halus (smooth stepping)
 * Mencegah lonjakan arus mendadak yang dapat mereset ESP32.
 */
void moveToAngleSmooth(int target, int stepDelayMs = 10) {
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
  digitalWrite(LED_PIN, LOW);

  // 3. Inisialisasi Wireless Bluetooth Serial
  // Nama Bluetooth yang akan muncul saat dipindai dari laptop/HP:
  SerialBT.begin("SmartTrash_ESP32");

  // 4. Alokasi timer PWM ESP32 untuk library ESP32Servo
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  // 5. Konfigurasi frekuensi standar SG90 (50Hz) & lebar pulsa (500us - 2400us)
  trashServo.setPeriodHertz(50);
  trashServo.attach(SERVO_PIN, 500, 2400);

  // 6. Posisikan servo ke 90 derajat (Netral)
  currentAngle = ANGLE_RESET;
  trashServo.write(currentAngle);

  // Tampilkan pesan inisialisasi
  Serial.println("\n========================================================");
  Serial.println("[ESP32] Smart Trash Sorting - DUAL MODE READY");
  Serial.println("[ESP32] 1. USB Serial : 115200 bps");
  Serial.println("[ESP32] 2. Bluetooth  : 'SmartTrash_ESP32' (Aktif)");
  Serial.println("[ESP32] Servo Pin     : GPIO 18");
  Serial.println("[ESP32] Status        : Posisi netral 90 deg siap!");
  Serial.println("========================================================");

  // Kedipkan LED 3x tanda siap
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, HIGH); delay(100);
    digitalWrite(LED_PIN, LOW); delay(100);
  }
}

void loop() {
  // -------------------------------------------------------------
  // A. BACA DARI USB SERIAL (Jika terhubung kabel USB ke laptop)
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
  // B. BACA DARI WIRELESS BLUETOOTH (Tanpa kabel ke laptop)
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
  // C. TIMER NON-BLOCKING UNTUK KEMBALI OTOMATIS KE NETRAL (90°)
  // -------------------------------------------------------------
  if (isSortingActive) {
    if (millis() - sortStartTime >= SORT_HOLD_DURATION_MS) {
      broadcastMessage("[ESP32] Status: Selesai memilah -> Kembali ke Netral (90 deg)...");
      moveToAngleSmooth(ANGLE_RESET, 10);
      isSortingActive = false;
      digitalWrite(LED_PIN, LOW);
      broadcastMessage("[ESP32] OK: Posisi netral siap (90 deg).");
    }
  }
}

/**
 * Pemrosesan Perintah DRY, WET, dan RESET
 */
void processCommand(String cmd, String source) {
  cmd.toUpperCase();

  if (cmd == "DRY") {
    digitalWrite(LED_PIN, HIGH);
    broadcastMessage("[ESP32] [" + source + "] DRY diterima -> Gerak KIRI (30 deg)...");
    moveToAngleSmooth(ANGLE_DRY, 8);
    isSortingActive = true;
    sortStartTime = millis();
    broadcastMessage("[ESP32] OK: Posisi 30 deg (DRY) aktif.");
  }
  else if (cmd == "WET") {
    digitalWrite(LED_PIN, HIGH);
    broadcastMessage("[ESP32] [" + source + "] WET diterima -> Gerak KANAN (150 deg)...");
    moveToAngleSmooth(ANGLE_WET, 8);
    isSortingActive = true;
    sortStartTime = millis();
    broadcastMessage("[ESP32] OK: Posisi 150 deg (WET) aktif.");
  }
  else if (cmd == "RESET") {
    digitalWrite(LED_PIN, LOW);
    broadcastMessage("[ESP32] [" + source + "] RESET diterima -> Kembali ke NETRAL (90 deg)...");
    moveToAngleSmooth(ANGLE_RESET, 8);
    isSortingActive = false;
    broadcastMessage("[ESP32] OK: Posisi 90 deg (NETRAL) aktif.");
  }
  else {
    broadcastMessage("[ESP32] ERROR [" + source + "]: Perintah '" + cmd + "' tidak dikenal. Gunakan: DRY, WET, RESET.");
  }
}
