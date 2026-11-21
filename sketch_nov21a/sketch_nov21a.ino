/**
 * ============================================================================
 * Smart Trash Sorting System - ESP32 Servo Actuator Firmware
 * ============================================================================
 * Board       : ESP32 Dev Module / NodeMCU-32S
 * Library     : ESP32Servo by Kevin Harrington (Install via Arduino Library Manager)
 * Servo Pin   : GPIO 18 (PWM Output)
 * Serial Baud : 115200 bps
 * Power       : External 5V for Servo (Common GND with ESP32)
 *
 * Perintah Serial:
 * - DRY\n   : Gerak ke 30° (Kiri - Sampah Kering), jeda 2.5s, lalu kembali ke 90°
 * - WET\n   : Gerak ke 150° (Kanan - Sampah Basah), jeda 2.5s, lalu kembali ke 90°
 * - RESET\n : Kembali langsung ke 90° (Posisi Netral)
 * ============================================================================
 */

#include <ESP32Servo.h>

// Definisi Pin & Parameter Hardware
const int SERVO_PIN = 18;            // Sesuai spesifikasi proyek (GPIO 18)
const int LED_PIN = 2;               // Built-in LED ESP32 untuk indikator visual

// Konfigurasi Sudut Servo
const int ANGLE_DRY   = 30;          // Kiri: Mengarahkan sampah kering
const int ANGLE_RESET = 90;          // Posisi awal / Netral
const int ANGLE_WET   = 150;         // Kanan: Mengarahkan sampah basah

// Durasi Tahan Pemilahan (Milidetik) sebelum otomatis kembali ke netral
const unsigned long SORT_HOLD_DURATION_MS = 2500;

// Objek Servo
Servo trashServo;

// Variabel State Mesin Non-blocking
int currentAngle = ANGLE_RESET;
int targetAngle = ANGLE_RESET;
bool isSortingActive = false;
unsigned long sortStartTime = 0;
String inputStringBuffer = "";

/**
 * Menggerakkan servo secara halus (smooth stepping)
 * Mencegah lonjakan arus mendadak (current spike) yang dapat me-reset ESP32.
 */
void moveToAngleSmooth(int target, int stepDelayMs = 12) {
  if (target == currentAngle) return;

  int step = (target > currentAngle) ? 1 : -1;
  while (currentAngle != target) {
    currentAngle += step;
    trashServo.write(currentAngle);
    delay(stepDelayMs);
  }
}

void setup() {
  // Inisialisasi Komunikasi Serial dengan Laptop
  Serial.begin(115200);
  delay(500);

  // Inisialisasi LED Indikator
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Alokasi timer PWM ESP32 untuk library ESP32Servo
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  // Konfigurasi frekuensi standar SG90 (50Hz) & lebar pulsa (500us - 2400us)
  trashServo.setPeriodHertz(50);
  trashServo.attach(SERVO_PIN, 500, 2400);

  // Posisikan awal ke 90 derajat (Netral)
  currentAngle = ANGLE_RESET;
  trashServo.write(currentAngle);

  // Kirim sinyal siap ke Serial Monitor / Web Serial Browser
  Serial.println("\n==================================================");
  Serial.println("[ESP32] Smart Trash Sorting Firmware Started");
  Serial.println("[ESP32] Pin: GPIO18 | Baud: 115200 bps");
  Serial.println("[ESP32] Commands: DRY, WET, RESET");
  Serial.println("[ESP32] Status: Servo siap di posisi netral (90 deg)");
  Serial.println("==================================================");

  // Kedipkan LED 2x sebagai tanda inisialisasi selesai
  digitalWrite(LED_PIN, HIGH); delay(150); digitalWrite(LED_PIN, LOW); delay(150);
  digitalWrite(LED_PIN, HIGH); delay(150); digitalWrite(LED_PIN, LOW);
}

void loop() {
  // 1. Membaca data serial yang masuk dari Browser JavaScript
  while (Serial.available() > 0) {
    char inChar = (char)Serial.read();

    if (inChar == '\n' || inChar == '\r') {
      inputStringBuffer.trim(); // Hapus whitespace/newline tersisa
      if (inputStringBuffer.length() > 0) {
        processCommand(inputStringBuffer);
        inputStringBuffer = "";
      }
    } else {
      // Simpan karakter ke buffer jika belum newline
      inputStringBuffer += inChar;
      // Batasi buffer agar tidak terjadi overflow jika data anomali
      if (inputStringBuffer.length() > 32) {
        inputStringBuffer = "";
      }
    }
  }

  // 2. State Machine Non-blocking untuk Otomatis Kembali ke Posisi Netral (90°)
  if (isSortingActive) {
    if (millis() - sortStartTime >= SORT_HOLD_DURATION_MS) {
      Serial.println("[ESP32] Status: Waktu pemilahan selesai. Mengembalikan servo ke posisi netral (90 deg)...");
      moveToAngleSmooth(ANGLE_RESET, 10);
      isSortingActive = false;
      digitalWrite(LED_PIN, LOW);
      Serial.println("[ESP32] OK: Posisi netral tercapai (90 deg). Siap menerima sampah baru.");
    }
  }
}

/**
 * Parsing dan eksekusi perintah serial
 */
void processCommand(String cmd) {
  cmd.toUpperCase(); // Konversi ke huruf besar agar tidak sensitif huruf

  if (cmd == "DRY") {
    digitalWrite(LED_PIN, HIGH);
    Serial.println("[ESP32] Command received: DRY -> Mengarahkan ke KIRI (30 deg)...");
    moveToAngleSmooth(ANGLE_DRY, 8);
    isSortingActive = true;
    sortStartTime = millis();
    Serial.println("[ESP32] OK: Servo pada posisi 30 deg (DRY). Menahan posisi...");
  } 
  else if (cmd == "WET") {
    digitalWrite(LED_PIN, HIGH);
    Serial.println("[ESP32] Command received: WET -> Mengarahkan ke KANAN (150 deg)...");
    moveToAngleSmooth(ANGLE_WET, 8);
    isSortingActive = true;
    sortStartTime = millis();
    Serial.println("[ESP32] OK: Servo pada posisi 150 deg (WET). Menahan posisi...");
  } 
  else if (cmd == "RESET") {
    digitalWrite(LED_PIN, LOW);
    Serial.println("[ESP32] Command received: RESET -> Kembali ke NETRAL (90 deg)...");
    moveToAngleSmooth(ANGLE_RESET, 8);
    isSortingActive = false;
    Serial.println("[ESP32] OK: Servo siap di posisi netral (90 deg).");
  } 
  else {
    Serial.print("[ESP32] ERROR: Perintah tidak dikenal: '");
    Serial.print(cmd);
    Serial.println("'. Gunakan: DRY, WET, atau RESET.");
  }
}
