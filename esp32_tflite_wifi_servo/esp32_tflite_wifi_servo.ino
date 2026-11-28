/**
 * ============================================================================
 * Smart Trash Sorting System - ESP32 Biasa (TFLite AI + WiFi Web + Servo)
 * ============================================================================
 * Alur Sistem:
 * 1. ESP32 menyalakan WiFi (Hotspot sendiri "SmartTrash-ESP32" atau gabung WiFi).
 * 2. iPhone membuka halaman web dari ESP32 (http://192.168.4.1/ atau IP lokal).
 * 3. iPhone mengambil foto via kamera -> resize 96x96 grayscale -> kirim ke ESP32.
 * 4. ESP32 menjalankan inferensi model TensorFlow Lite (tf-lite/) di chip ESP32.
 * 5. Jika terdeteksi "organic" (WET) atau "anorganic" (DRY) >= threshold:
 *    ESP32 langsung menggerakkan Servo SG90 di GPIO 18!
 *
 * Library Wajib di Arduino IDE (Install via Library Manager):
 * 1. ESP32Servo (by Kevin Harrington)
 * 2. TensorFlowLite_ESP32 (by tanakamasayuki)
 *
 * Pengaturan Arduino IDE:
 * - Board            : ESP32 Dev Module
 * - Partition Scheme : Huge APP (3MB No OTA / 1MB SPIFFS) -> WAJIB!
 * ============================================================================
 */

#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>
#include "esp_heap_caps.h"

// TensorFlow Lite Micro (menggunakan library EloquentTinyML yang sudah terinstal)
#include <EloquentTinyML.h>
#include "tensorflow/lite/micro/kernels/all_ops_resolver.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"

// File model dari folder tf-lite/
#include "model_settings.h"
#include "person_detect_model_data.h"

// ============================================================================
// 1. KONFIGURASI WIFI
// ============================================================================
// Mode 1 (Default / Paling Praktis): ESP32 membuat Hotspot WiFi sendiri
//   -> Sambungkan WiFi iPhone ke "SmartTrash-ESP32" (Pass: 12345678)
//   -> Buka Safari iPhone ke: http://192.168.4.1
// Mode 2 (Opsional): Jika ingin ESP32 ikut WiFi rumah/kampus, isi SSID & Pass:
const char* STA_SSID     = "";   // Kosongkan jika pakai Hotspot ESP32
const char* STA_PASSWORD = "";

const char* AP_SSID      = "defis_esp";
const char* AP_PASSWORD  = "defis2303";

// ============================================================================
// 2. KONFIGURASI SERVO & AI THRESHOLD
// ============================================================================
const int SERVO_PIN = 18;
const int LED_PIN   = 2;

const int ANGLE_DRY   = 30;    // anorganic (Kiri)
const int ANGLE_RESET = 90;    // standby / background (Netral)
const int ANGLE_WET   = 150;   // organic (Kanan)

const float CONFIDENCE_THRESHOLD = 0.70f; // Minimal 70% (bisa dinaikkan ke 0.85f)
const unsigned long SORT_HOLD_MS = 2500;

Servo trashServo;
WebServer server(80);

int currentAngle = ANGLE_RESET;
bool isSortingActive = false;
unsigned long sortStartTime = 0;

// ============================================================================
// 3. VARIABEL TENSORFLOW LITE MICRO
// ============================================================================
namespace {
  tflite::ErrorReporter* error_reporter = nullptr;
  const tflite::Model* model = nullptr;
  tflite::MicroInterpreter* interpreter = nullptr;
  TfLiteTensor* input = nullptr;
  TfLiteTensor* output = nullptr;

  constexpr int kTensorArenaSize = 136 * 1024;
  uint8_t* tensor_arena = nullptr; // Dialokasikan di heap sebelum WiFi aktif
}

bool isModelReady = false;

// ============================================================================
// FUNGSI SERVO
// ============================================================================
void moveToAngleSmooth(int target, int stepDelayMs = 8) {
  if (target == currentAngle) return;
  int step = (target > currentAngle) ? 1 : -1;
  while (currentAngle != target) {
    currentAngle += step;
    trashServo.write(currentAngle);
    delay(stepDelayMs);
  }
}

void triggerServo(int targetAngle) {
  digitalWrite(LED_PIN, (targetAngle == ANGLE_RESET) ? LOW : HIGH);
  moveToAngleSmooth(targetAngle, 7);
  if (targetAngle != ANGLE_RESET) {
    isSortingActive = true;
    sortStartTime = millis();
  } else {
    isSortingActive = false;
  }
}

// ============================================================================
// HALAMAN WEB MOBILE-FRIENDLY UNTUK SAFARI IPHONE (/)
// ============================================================================
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="id">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <title>Smart Trash ESP32 AI</title>
  <style>
    * { box-sizing: border-box; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; }
    body { margin: 0; padding: 16px; background: #0b0f19; color: #f8fafc; text-align: center; }
    .card { max-width: 440px; margin: 0 auto 14px; background: #131b2e; border: 1px solid #1e293b; border-radius: 16px; padding: 18px; }
    h1 { font-size: 1.25rem; margin: 0 0 4px; color: #60a5fa; }
    .sub { font-size: 0.78rem; color: #94a3b8; margin-bottom: 14px; }
    .btn-cam {
      display: block; width: 100%; padding: 16px; border-radius: 12px; border: none;
      background: linear-gradient(135deg, #2563eb, #0284c7); color: #fff;
      font-size: 1.05rem; font-weight: 700; cursor: pointer;
    }
    input[type="file"] { display: none; }
    .preview-wrap { display: flex; justify-content: center; gap: 12px; margin-top: 14px; align-items: center; }
    canvas { border: 2px solid #334155; border-radius: 10px; background: #000; }
    #previewOrig { width: 120px; height: 120px; object-fit: cover; border-radius: 10px; border: 2px solid #334155; display: none; }
    .result-box { margin-top: 14px; padding: 14px; border-radius: 12px; background: #0f172a; border: 1px solid #1e293b; }
    .res-title { font-size: 1.35rem; font-weight: 800; margin: 4px 0; }
    .res-dry { color: #38bdf8; }
    .res-wet { color: #34d399; }
    .res-bg  { color: #94a3b8; }
    .scores { font-family: monospace; font-size: 0.82rem; color: #cbd5e1; margin-top: 8px; line-height: 1.6; }
    .manual-grid { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 8px; margin-top: 10px; }
    .btn-m { padding: 10px 6px; border-radius: 8px; border: 1px solid #334155; background: #0f172a; color: #fff; font-weight: 600; font-size: 0.78rem; cursor: pointer; }
    .status { font-size: 0.8rem; color: #fbbf24; margin-top: 10px; min-height: 20px; }
  </style>
</head>
<body>
  <div class="card">
    <h1>Smart Trash ESP32 On-Edge</h1>
    <div class="sub">Kamera iPhone &rarr; Proses AI TFLite di ESP32 &rarr; Servo GPIO 18</div>

    <label class="btn-cam" for="camInput">Ambil Foto Sampah (Kamera)</label>
    <input id="camInput" type="file" accept="image/*" capture="environment">

    <div style="margin-top:10px;display:flex;gap:8px;justify-content:center;align-items:center;flex-wrap:wrap;">
      <button class="btn-m" id="btnLive" onclick="toggleLive()" style="flex:1;min-width:140px;background:#0f766e;border-color:#14b8a6;">Mode Live (Tiap 3 Detik)</button>
      <select id="intervalSel" class="btn-m" style="width:auto;">
        <option value="3000" selected>3 Detik</option>
        <option value="5000">5 Detik</option>
      </select>
    </div>

    <div class="preview-wrap">
      <video id="liveVideo" autoplay playsinline muted style="width:140px;height:140px;object-fit:cover;border-radius:10px;border:2px solid #14b8a6;display:none;"></video>
      <img id="previewOrig" alt="Foto">
      <div>
        <canvas id="canvas96" width="96" height="96"></canvas>
        <div style="font-size:0.7rem;color:#64748b;margin-top:4px;">Input Model (96x96)</div>
      </div>
    </div>

    <div class="status" id="statusText">Siap. Ketuk tombol di atas untuk memotret sampah.</div>

    <div class="result-box" id="resultBox">
      <div style="font-size:0.7rem;color:#64748b;letter-spacing:0.06em;">HASIL KLASIFIKASI ESP32</div>
      <div class="res-title res-bg" id="resLabel">Menunggu Foto...</div>
      <div class="scores" id="resScores">-</div>
    </div>
  </div>

  <div class="card">
    <div style="font-size:0.8rem;color:#94a3b8;font-weight:600;">Kontrol Servo Manual</div>
    <div class="manual-grid">
      <button class="btn-m" onclick="sendServo('DRY')">Kiri 30&deg; (DRY)</button>
      <button class="btn-m" onclick="sendServo('RESET')">Netral 90&deg;</button>
      <button class="btn-m" onclick="sendServo('WET')">Kanan 150&deg; (WET)</button>
    </div>
  </div>

  <script>
    const camInput = document.getElementById('camInput');
    const previewOrig = document.getElementById('previewOrig');
    const liveVideo = document.getElementById('liveVideo');
    const btnLive = document.getElementById('btnLive');
    const intervalSel = document.getElementById('intervalSel');
    const canvas = document.getElementById('canvas96');
    const ctx = canvas.getContext('2d');
    const statusText = document.getElementById('statusText');
    const resLabel = document.getElementById('resLabel');
    const resScores = document.getElementById('resScores');

    let liveStream = null;
    let liveTimer = null;
    let isSending = false;

    async function sendCanvasToESP32() {
      if (isSending) return;
      isSending = true;

      // Preview tetap berwarna di canvas, tapi data yang dikirim ke model tf-lite/ adalah 96x96 grayscale (1 channel)
      const imgData = ctx.getImageData(0, 0, 96, 96);
      const d = imgData.data;
      let hexStr = '';
      for (let i = 0; i < d.length; i += 4) {
        const gray = Math.round(0.299 * d[i] + 0.587 * d[i + 1] + 0.114 * d[i + 2]);
        hexStr += gray.toString(16).padStart(2, '0');
      }

      statusText.textContent = 'Mengirim ke ESP32 & menjalankan AI...';
      try {
        const resp = await fetch('/predict', {
          method: 'POST',
          headers: { 'Content-Type': 'text/plain' },
          body: hexStr
        });
        const data = await resp.json();
        if (data.error) {
          statusText.textContent = 'Error: ' + data.error;
        } else {
          updateResultUI(data);
        }
      } catch (err) {
        statusText.textContent = 'Gagal terhubung ke ESP32: ' + err.message;
      } finally {
        isSending = false;
      }
    }

    async function toggleLive() {
      if (liveTimer) {
        clearInterval(liveTimer);
        liveTimer = null;
        if (liveStream) {
          liveStream.getTracks().forEach(t => t.stop());
          liveStream = null;
        }
        liveVideo.style.display = 'none';
        btnLive.textContent = 'Mode Live (Tiap ' + (intervalSel.value / 1000) + ' Detik)';
        statusText.textContent = 'Mode Live dihentikan.';
        return;
      }

      if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
        statusText.innerHTML =
          'Kamera Live diblokir Chrome/Safari karena alamat masih <b>http://</b> (bukan https).<br>' +
          '<b>Solusi Android Chrome:</b> Buka tab baru &rarr; ketik <code>chrome://flags/#unsafely-treat-insecure-origin-as-secure</code> &rarr; isi <code>http://192.168.4.1</code> &rarr; pilih <b>Enabled</b> &rarr; <b>Relaunch</b>.';
        return;
      }

      try {
        liveStream = await navigator.mediaDevices.getUserMedia({ video: { facingMode: 'environment' }, audio: false });
        liveVideo.srcObject = liveStream;
        liveVideo.style.display = 'block';
        previewOrig.style.display = 'none';
        btnLive.textContent = 'Stop Mode Live';

        const ms = parseInt(intervalSel.value, 10);
        statusText.textContent = 'Mode Live aktif: kirim otomatis tiap ' + (ms / 1000) + ' detik...';

        const captureAndSend = () => {
          if (!liveVideo.videoWidth || isSending) return;
          const minSide = Math.min(liveVideo.videoWidth, liveVideo.videoHeight);
          const sx = (liveVideo.videoWidth - minSide) / 2;
          const sy = (liveVideo.videoHeight - minSide) / 2;
          ctx.drawImage(liveVideo, sx, sy, minSide, minSide, 0, 0, 96, 96);
          sendCanvasToESP32();
        };

        setTimeout(captureAndSend, 800);
        liveTimer = setInterval(captureAndSend, ms);
      } catch (err) {
        statusText.textContent = 'Kamera Live tidak diizinkan (' + err.message + '). Gunakan tombol Ambil Foto.';
      }
    }

    camInput.addEventListener('change', (e) => {
      const file = e.target.files[0];
      if (!file) return;

      statusText.textContent = 'Memproses gambar ke 96x96...';
      const reader = new FileReader();
      reader.onload = (ev) => {
        const img = new Image();
        img.onload = async () => {
          previewOrig.src = ev.target.result;
          previewOrig.style.display = 'block';
          liveVideo.style.display = 'none';

          const minSide = Math.min(img.width, img.height);
          const sx = (img.width - minSide) / 2;
          const sy = (img.height - minSide) / 2;
          ctx.drawImage(img, sx, sy, minSide, minSide, 0, 0, 96, 96);

          await sendCanvasToESP32();
        };
        img.src = ev.target.result;
      };
      reader.readAsDataURL(file);
    });

    function updateResultUI(data) {
      statusText.textContent = data.action || 'Selesai diproses di ESP32.';
      resLabel.textContent = data.top_label.toUpperCase() + ' (' + (data.top_score * 100).toFixed(1) + '%)';

      resLabel.className = 'res-title ' + (
        data.top_label === 'anorganic' ? 'res-dry' :
        data.top_label === 'organic'   ? 'res-wet' : 'res-bg'
      );

      let html = '';
      for (const item of data.scores) {
        html += item.label + ': ' + (item.score * 100).toFixed(1) + '%<br>';
      }
      html += 'Waktu inferensi ESP32: ' + data.time_ms + ' ms';
      resScores.innerHTML = html;
    }

    async function sendServo(cmd) {
      statusText.textContent = 'Menggerakkan servo (' + cmd + ')...';
      try {
        const resp = await fetch('/servo?cmd=' + cmd);
        const txt = await resp.text();
        statusText.textContent = txt;
      } catch (err) {
        statusText.textContent = 'Error servo: ' + err.message;
      }
    }
  </script>
</body>
</html>
)rawliteral";

// ============================================================================
// HANDLER HTTP ENDPOINTS
// ============================================================================
void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleServo() {
  String cmd = server.arg("cmd");
  cmd.toUpperCase();
  if (cmd == "DRY") {
    triggerServo(ANGLE_DRY);
    server.send(200, "text/plain", "Servo bergerak ke KIRI 30 deg (DRY)");
  } else if (cmd == "WET") {
    triggerServo(ANGLE_WET);
    server.send(200, "text/plain", "Servo bergerak ke KANAN 150 deg (WET)");
  } else {
    triggerServo(ANGLE_RESET);
    server.send(200, "text/plain", "Servo di posisi NETRAL 90 deg");
  }
}

static inline uint8_t hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  return 0;
}

void handlePredict() {
  Serial.printf("[HTTP] /predict diterima | Free Heap: %u bytes\n", ESP.getFreeHeap());
  Serial.flush();
  if (!isModelReady) {
    server.send(500, "application/json", "{\"error\":\"Model TFLite belum siap (RAM tidak cukup)\"}");
    return;
  }

  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"Body kosong\"}");
    return;
  }

  const String& hexBody = server.arg("plain");
  if ((int)hexBody.length() != kMaxImageSize * 2) {
    server.send(400, "application/json", "{\"error\":\"Ukuran gambar bukan 96x96\"}");
    return;
  }

  // Isi input tensor dari hex grayscale
  for (int i = 0; i < kMaxImageSize; i++) {
    uint8_t pix = (hexVal(hexBody[i * 2]) << 4) | hexVal(hexBody[i * 2 + 1]);
    if (input->type == kTfLiteInt8) {
      input->data.int8[i] = (int8_t)((int)pix - 128);
    } else {
      input->data.uint8[i] = pix;
    }
  }

  // Jalankan inferensi TFLite di ESP32
  unsigned long t0 = millis();
  TfLiteStatus invoke_status = interpreter->Invoke();
  unsigned long elapsed = millis() - t0;

  if (invoke_status != kTfLiteOk) {
    server.send(500, "application/json", "{\"error\":\"Invoke TFLite gagal\"}");
    return;
  }

  // Dequantize skor output
  float scores[kCategoryCount];
  int topIdx = 0;
  float topScore = -1.0f;

  float scale = output->params.scale;
  int32_t zp  = output->params.zero_point;

  for (int i = 0; i < kCategoryCount; i++) {
    float val = 0.0f;
    if (output->type == kTfLiteInt8) {
      val = (output->data.int8[i] - zp) * scale;
    } else if (output->type == kTfLiteUInt8) {
      val = (output->data.uint8[i] - zp) * scale;
    } else {
      val = output->data.f[i];
    }
    if (val < 0.0f) val = 0.0f;
    if (val > 1.0f) val = 1.0f;
    scores[i] = val;

    if (val > topScore) {
      topScore = val;
      topIdx = i;
    }
  }

  const char* topLabel = kCategoryLabels[topIdx];
  String actionMsg = "Tidak ada aksi (Background / Confidence di bawah threshold)";
  int pendingAngle = ANGLE_RESET;

  Serial.printf("[AI ESP32] Hasil: %s (%.1f%%) dalam %lu ms\n", topLabel, topScore * 100.0f, elapsed);

  // Tentukan aksi servo sesuai label di model_settings.cpp: {"background", "organic", "anorganic"}
  if (topScore >= CONFIDENCE_THRESHOLD) {
    if (strcmp(topLabel, "anorganic") == 0) {
      actionMsg = "Terdeteksi ANORGANIC (DRY) -> Servo KIRI 30 deg";
      pendingAngle = ANGLE_DRY;
    } else if (strcmp(topLabel, "organic") == 0) {
      actionMsg = "Terdeteksi ORGANIC (WET) -> Servo KANAN 150 deg";
      pendingAngle = ANGLE_WET;
    }
  }

  // Susun & KIRIM JSON response DULU sebelum servo bergerak
  // (Mencegah koneksi WiFi putus/timeout akibat delay atau tarikan arus servo)
  String json = "{";
  json += "\"top_label\":\"" + String(topLabel) + "\",";
  json += "\"top_score\":" + String(topScore, 4) + ",";
  json += "\"time_ms\":" + String(elapsed) + ",";
  json += "\"action\":\"" + actionMsg + "\",";
  json += "\"scores\":[";
  for (int i = 0; i < kCategoryCount; i++) {
    if (i > 0) json += ",";
    json += "{\"label\":\"" + String(kCategoryLabels[i]) + "\",\"score\":" + String(scores[i], 4) + "}";
  }
  json += "]}";

  server.send(200, "application/json", json);
  delay(30); // Beri waktu paket TCP WiFi terkirim ke HP sebelum servo menarik arus

  // Baru gerakkan servo setelah hasil terkirim ke HP
  if (pendingAngle != ANGLE_RESET) {
    triggerServo(pendingAngle);
  }
}

// ============================================================================
// SETUP & LOOP
// ============================================================================
void setup() {
  disableLoopWDT(); // Cegah reset Watchdog (TWDT 5 detik) saat inferensi TFLite berjalan
  Serial.begin(115200);
  delay(1000);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.println("\n==================================================");
  Serial.println("  SMART TRASH ESP32 (ON-EDGE TFLITE + WIFI + SERVO)");
  Serial.println("==================================================");
  Serial.flush();

  // 1. Alokasikan Tensor Arena SEBELUM WiFi aktif (agar dapat blok DRAM kontinyu)
  size_t freeHeap = ESP.getFreeHeap();
  size_t maxBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  Serial.printf("[INFO] Free Heap: %u bytes | Max Contiguous Block: %u bytes\n",
                (unsigned)freeHeap, (unsigned)maxBlock);
  Serial.flush();

  int allocSize = kTensorArenaSize;
  if ((size_t)allocSize > maxBlock) {
    allocSize = (maxBlock > 8192) ? (int)(maxBlock - 4096) : 0;
    Serial.printf("[WARN] Menyesuaikan tensor_arena ke %d bytes\n", allocSize);
    Serial.flush();
  }

  if (allocSize > 0) {
    tensor_arena = (uint8_t*)heap_caps_malloc(allocSize, MALLOC_CAP_8BIT);
  }

  if (!tensor_arena) {
    Serial.println("[ERROR] Gagal alokasi tensor_arena!");
    Serial.flush();
  } else {
    Serial.println("[STEP 1] tensor_arena OK, memuat model TFLite...");
    Serial.flush();
    static tflite::MicroErrorReporter micro_error_reporter;
    error_reporter = &micro_error_reporter;

    model = tflite::GetModel(g_person_detect_model_data);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
      Serial.println("[ERROR] Versi schema model TFLite tidak cocok!");
    } else {
      Serial.println("[STEP 2] Menyiapkan OpResolver & Interpreter...");
      Serial.flush();
      static tflite::ops::micro::AllOpsResolver resolver;
      static tflite::MicroInterpreter static_interpreter(
          model, resolver, tensor_arena, allocSize, error_reporter);
      interpreter = &static_interpreter;

      Serial.println("[STEP 3] Memanggil AllocateTensors()...");
      Serial.flush();
      if (interpreter->AllocateTensors() != kTfLiteOk) {
        Serial.println("[ERROR] AllocateTensors() gagal!");
      } else {
        input  = interpreter->input(0);
        output = interpreter->output(0);
        isModelReady = true;
        Serial.println("[OK] Model TFLite Micro siap di ESP32!");
      }
    }
    Serial.flush();
  }

  // 2. Inisialisasi Servo + Self-Test (180 -> 0 -> 90)
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  trashServo.setPeriodHertz(50);
  trashServo.attach(SERVO_PIN, 500, 2400);

  Serial.println("[INFO] Self-Test Servo: 180 -> 0 -> 90...");
  moveToAngleSmooth(180, 6); delay(600);
  moveToAngleSmooth(0, 6);   delay(600);
  moveToAngleSmooth(ANGLE_RESET, 6);
  Serial.println("[OK] Servo standby di 90 deg.");

  // 3. Jalankan WiFi (STA jika diisi, jika tidak buat AP Hotspot sendiri)
  if (strlen(STA_SSID) > 0) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(STA_SSID, STA_PASSWORD);
    Serial.printf("[INFO] Menghubungkan ke WiFi '%s'...", STA_SSID);
    int retry = 0;
    while (WiFi.status() != WL_CONNECTED && retry < 20) {
      delay(500); Serial.print("."); retry++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("\n[OK] Terhubung! Buka di Safari iPhone: http://");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("\n[WARN] Gagal konek STA, beralih ke Hotspot AP...");
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    WiFi.setSleep(false);
    Serial.printf("[OK] Hotspot ESP32 Aktif!\n");
    Serial.printf("     WiFi SSID : %s\n", AP_SSID);
    Serial.printf("     Password  : %s\n", AP_PASSWORD);
    Serial.print ("     Buka URL  : http://");
    Serial.println(WiFi.softAPIP());
  }

  // 4. Jalankan Web Server
  server.on("/", HTTP_GET, handleRoot);
  server.on("/predict", HTTP_POST, handlePredict);
  server.on("/servo", HTTP_GET, handleServo);
  server.begin();
  Serial.println("[OK] Web Server aktif di port 80.");
  Serial.printf("[INFO] Free Heap akhir: %u bytes\n", ESP.getFreeHeap());
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
