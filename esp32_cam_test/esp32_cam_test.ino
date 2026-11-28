/**
 * ============================================================================
 * ESP32-CAM (AI-Thinker) Hardware & Stream Test Sketch
 * ============================================================================
 * Tujuan: Mengetes apakah modul ESP32-CAM (sensor OV2640, PSRAM, WiFi, & stream)
 *         berfungsi dengan baik sebelum diintegrasikan ke sistem pemilah sampah.
 *
 * Pengaturan Arduino IDE:
 * - Board            : AI Thinker ESP32-CAM
 * - CPU Frequency    : 240MHz (WiFi/BT)
 * - Flash Frequency  : 80MHz
 * - Flash Mode       : QIO
 * - Partition Scheme : Huge APP (3MB No OTA / 1MB SPIFFS)
 * - PSRAM            : Enabled (jika tersedia di menu)
 *
 * Cara Upload (jika pakai FTDI / USB-to-TTL):
 * - Hubungkan IO0 ke GND sebelum colok/reset untuk masuk mode Flash.
 * - Setelah selesai "Hard resetting...", lepaskan kabel IO0-GND lalu tekan RST.
 * - Jika pakai board ESP32-CAM-MB (dock USB), tinggal klik Upload langsung.
 * ============================================================================
 */

#include "esp_camera.h"
#include <WiFi.h>
#include "esp_http_server.h"

// ============================================================================
// 1. KONFIGURASI WIFI (Ganti sesuai WiFi / Hotspot Anda)
// ============================================================================
const char* WIFI_SSID     = "NAMA_WIFI_ANDA";
const char* WIFI_PASSWORD = "PASSWORD_WIFI_ANDA";

// ============================================================================
// 2. PINOUT KAMERA AI-THINKER ESP32-CAM (OV2640)
// ============================================================================
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

#define FLASH_LED_PIN      4   // LED Flash terang (putih)
#define STATUS_LED_PIN    33   // LED merah kecil di belakang board (Active LOW)

httpd_handle_t stream_httpd = NULL;

// ============================================================================
// HANDLER HALAMAN TEST UTAMA (/)
// ============================================================================
static esp_err_t index_handler(httpd_req_t *req) {
  const char* html = R"rawliteral(
<!DOCTYPE html>
<html lang="id">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32-CAM Test</title>
  <style>
    body { font-family: sans-serif; background: #0f172a; color: #f8fafc; text-align: center; padding: 20px; }
    h1 { color: #38bdf8; }
    img { max-width: 100%; border: 3px solid #38bdf8; border-radius: 12px; margin-top: 12px; background: #000; }
    .status { display: inline-block; padding: 6px 14px; background: #065f46; color: #6ee7b7; border-radius: 999px; font-weight: bold; }
  </style>
</head>
<body>
  <h1>ESP32-CAM Live Test</h1>
  <div class="status">Kamera OV2640 Aktif &amp; Mengalirkan Video</div>
  <br>
  <img src="/stream" alt="ESP32-CAM Stream">
</body>
</html>
)rawliteral";
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, html, strlen(html));
}

// ============================================================================
// HANDLER MJPEG STREAM (/stream)
// ============================================================================
#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t * fb = NULL;
  esp_err_t res = ESP_OK;
  char part_buf[64];

  httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("[ERROR] Gagal mengambil frame dari kamera!");
      res = ESP_FAIL;
    } else {
      res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
      if (res == ESP_OK) {
        size_t hlen = snprintf(part_buf, sizeof(part_buf), _STREAM_PART, fb->len);
        res = httpd_resp_send_chunk(req, part_buf, hlen);
      }
      if (res == ESP_OK) {
        res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
      }
      esp_camera_fb_return(fb);
      fb = NULL;
    }
    if (res != ESP_OK) {
      break;
    }
  }
  return res;
}

void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;

  httpd_uri_t index_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = index_handler,
    .user_ctx  = NULL
  };

  httpd_uri_t stream_uri = {
    .uri       = "/stream",
    .method    = HTTP_GET,
    .handler   = stream_handler,
    .user_ctx  = NULL
  };

  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &index_uri);
    httpd_register_uri_handler(stream_httpd, &stream_uri);
    Serial.println("[OK] Web Server kamera berjalan.");
  } else {
    Serial.println("[ERROR] Gagal menjalankan Web Server!");
  }
}

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(false);
  delay(1000);

  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, HIGH); // Matikan LED merah (active LOW)

  Serial.println("\n==================================================");
  Serial.println("         ESP32-CAM HARDWARE & STREAM TEST         ");
  Serial.println("==================================================");

  // 1. Cek PSRAM
  if (psramFound()) {
    Serial.printf("[OK] PSRAM terdeteksi: %d bytes\n", ESP.getPsramSize());
  } else {
    Serial.println("[WARN] PSRAM tidak terdeteksi! Resolusi akan dibatasi.");
  }

  // 2. Konfigurasi Pin & Parameter Kamera
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size   = FRAMESIZE_VGA;    // 640x480 (ringan & jelas)
    config.jpeg_quality = 12;               // 0-63 (lebih kecil = lebih jernih)
    config.fb_count     = 2;
  } else {
    config.frame_size   = FRAMESIZE_CIF;    // 400x296
    config.jpeg_quality = 14;
    config.fb_count     = 1;
  }

  // 3. Inisialisasi Kamera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[FAIL] Inisialisasi kamera GAGAL! Kode error: 0x%x\n", err);
    Serial.println("Cek: 1) Soket kamera OV2640 terpasang rapat.");
    Serial.println("     2) Catu daya 5V cukup (min 1A, jangan kabel USB tipis).");
    return;
  }
  Serial.println("[OK] Sensor kamera OV2640 berhasil diinisialisasi!");

  // 4. Tes Ambil 1 Frame (uji hardware langsung tanpa WiFi)
  camera_fb_t * test_fb = esp_camera_fb_get();
  if (!test_fb) {
    Serial.println("[FAIL] Kamera aktif tapi gagal menangkap gambar!");
    return;
  }
  Serial.printf("[OK] Tes tangkap gambar berhasil! Ukuran JPEG: %u bytes (%dx%d)\n",
                test_fb->len, test_fb->width, test_fb->height);
  esp_camera_fb_return(test_fb);

  // 5. Koneksi WiFi untuk Live Stream Test
  Serial.printf("[INFO] Menghubungkan ke WiFi '%s'...", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.setSleep(false);

  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 30) {
    delay(500);
    Serial.print(".");
    retry++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[OK] WiFi Terhubung!");
    startCameraServer();
    digitalWrite(STATUS_LED_PIN, LOW); // Nyalakan LED merah tanda siap

    Serial.println("\n--------------------------------------------------");
    Serial.print("Buka di browser: http://");
    Serial.println(WiFi.localIP());
    Serial.print("URL Stream     : http://");
    Serial.print(WiFi.localIP());
    Serial.println("/stream");
    Serial.println("--------------------------------------------------");
  } else {
    Serial.println("\n[WARN] WiFi gagal terhubung (cek SSID/Password).");
    Serial.println("Namun tes hardware kamera & capture gambar SUDAH BERHASIL!");
  }
}

void loop() {
  delay(10000);
}
