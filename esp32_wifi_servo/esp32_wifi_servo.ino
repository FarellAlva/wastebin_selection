/**
 * ============================================================================
 * Smart Trash Sorting System - ESP32 WiFi Servo Receiver (Plan A)
 * ============================================================================
 * Arsitektur:
 * - Browser (Laptop / HP) menjalankan AI Teachable Machine (3 kelas RGB 224x224)
 * - Browser mengirim perintah HTTP via WiFi:
 *     GET /servo?cmd=DRY   -> Servo Kiri  (30 deg), tahan 2.5s, kembali ke 90 deg
 *     GET /servo?cmd=WET   -> Servo Kanan (150 deg), tahan 2.5s, kembali ke 90 deg
 *     GET /servo?cmd=RESET -> Servo Netral (90 deg)
 *     GET /status          -> Cek status koneksi & sudut servo (JSON + CORS)
 *
 * Library Wajib: ESP32Servo (by Kevin Harrington)
 * Board        : ESP32 Dev Module (Partition default 4MB sudah cukup!)
 * ============================================================================
 */

#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>

// ============================================================================
// 1. KONFIGURASI WIFI
// ============================================================================
// Opsi A (Default): ESP32 membuat Hotspot sendiri (tinggal konek laptop/HP ke WiFi ini)
const char* AP_SSID     = "defis_esp";
const char* AP_PASSWORD = "defis2303";

// Opsi B (Opsional): Jika ingin ESP32 gabung ke WiFi rumah/hotspot HP, isi di bawah ini:
const char* STA_SSID     = "";
const char* STA_PASSWORD = "";

// ============================================================================
// 2. KONFIGURASI HARDWARE SERVO (GPIO 18)
// ============================================================================
const int SERVO_PIN = 18;
const int LED_PIN   = 2;

const int ANGLE_DRY   = 30;    // anorganic / Kiri
const int ANGLE_RESET = 90;    // background / Netral
const int ANGLE_WET   = 150;   // organic / Kanan

const unsigned long SORT_HOLD_MS = 2500;

Servo trashServo;
WebServer server(80);

int currentAngle = ANGLE_RESET;
bool isSortingActive = false;
unsigned long sortStartTime = 0;

void moveToAngleSmooth(int target, int stepDelayMs = 8) {
  if (target == currentAngle) return;
  int step = (target > currentAngle) ? 1 : -1;
  while (currentAngle != target) {
    currentAngle += step;
    trashServo.write(currentAngle);
    delay(stepDelayMs);
  }
}

void sendCorsHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void handleOptions() {
  sendCorsHeaders();
  server.send(204);
}

void handleStatus() {
  sendCorsHeaders();
  String ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  String json = "{\"status\":\"ok\",\"angle\":" + String(currentAngle) + ",\"ip\":\"" + ip + "\"}";
  server.send(200, "application/json", json);
}

void handleServo() {
  sendCorsHeaders();
  String cmd = server.arg("cmd");
  cmd.toUpperCase();
  cmd.trim();

  if (cmd == "DRY") {
    server.send(200, "application/json", "{\"ok\":true,\"cmd\":\"DRY\",\"angle\":30}");
    delay(20);
    digitalWrite(LED_PIN, HIGH);
    Serial.println("[WiFi] DRY diterima -> Gerak KIRI (30 deg)");
    moveToAngleSmooth(ANGLE_DRY, 7);
    isSortingActive = true;
    sortStartTime = millis();
  } else if (cmd == "WET") {
    server.send(200, "application/json", "{\"ok\":true,\"cmd\":\"WET\",\"angle\":150}");
    delay(20);
    digitalWrite(LED_PIN, HIGH);
    Serial.println("[WiFi] WET diterima -> Gerak KANAN (150 deg)");
    moveToAngleSmooth(ANGLE_WET, 7);
    isSortingActive = true;
    sortStartTime = millis();
  } else if (cmd == "RESET") {
    server.send(200, "application/json", "{\"ok\":true,\"cmd\":\"RESET\",\"angle\":90}");
    delay(20);
    digitalWrite(LED_PIN, LOW);
    Serial.println("[WiFi] RESET diterima -> NETRAL (90 deg)");
    moveToAngleSmooth(ANGLE_RESET, 7);
    isSortingActive = false;
  } else {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"Gunakan cmd=DRY, WET, atau RESET\"}");
  }
}

void setup() {
  Serial.begin(115200);
  delay(400);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // 1. Inisialisasi Servo + Self-Test (180 -> 0 -> 90)
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  trashServo.setPeriodHertz(50);
  trashServo.attach(SERVO_PIN, 500, 2400);

  Serial.println("\n==================================================");
  Serial.println("   SMART TRASH ESP32 - WIFI SERVO RECEIVER (PLAN A)");
  Serial.println("==================================================");
  Serial.println("[INFO] Self-Test Servo: 180 -> 0 -> 90...");
  moveToAngleSmooth(180, 6); delay(600);
  moveToAngleSmooth(0, 6);   delay(600);
  moveToAngleSmooth(ANGLE_RESET, 6);
  Serial.println("[OK] Servo standby di 90 deg.");

  // 2. Jalankan WiFi (STA jika diisi, jika tidak buat Hotspot AP)
  if (strlen(STA_SSID) > 0) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(STA_SSID, STA_PASSWORD);
    Serial.printf("[INFO] Menghubungkan ke WiFi '%s'...", STA_SSID);
    int retry = 0;
    while (WiFi.status() != WL_CONNECTED && retry < 20) {
      delay(500); Serial.print("."); retry++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("\n[OK] WiFi Terhubung! IP ESP32: http://");
      Serial.println(WiFi.localIP());
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    WiFi.setSleep(false);
    Serial.println("[OK] Hotspot ESP32 Aktif!");
    Serial.printf("     SSID     : %s\n", AP_SSID);
    Serial.printf("     Password : %s\n", AP_PASSWORD);
    Serial.print ("     IP ESP32 : http://");
    Serial.println(WiFi.softAPIP());
  }

  server.on("/status", HTTP_GET, handleStatus);
  server.on("/status", HTTP_OPTIONS, handleOptions);
  server.on("/servo", HTTP_GET, handleServo);
  server.on("/servo", HTTP_OPTIONS, handleOptions);
  server.begin();
  Serial.println("[OK] HTTP Servo API siap di port 80.");
}

void loop() {
  server.handleClient();

  if (isSortingActive && (millis() - sortStartTime >= SORT_HOLD_MS)) {
    Serial.println("[ESP32] Selesai memilah -> Kembali ke Netral (90 deg)");
    moveToAngleSmooth(ANGLE_RESET, 7);
    isSortingActive = false;
    digitalWrite(LED_PIN, LOW);
  }
}
