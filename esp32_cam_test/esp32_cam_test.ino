/*
  ESP32-CAM quick test (AI-Thinker module)

  What it does:
    1. Initializes the camera and prints the result on Serial (115200).
    2. Captures a test frame and prints its size/resolution.
    3. Connects to WiFi and serves:
         http://<ip>/          -> simple page with a refresh button
         http://<ip>/capture   -> a fresh JPEG each request
    4. Blinks the onboard flash LED (GPIO 4) once when ready.

  Arduino IDE setup:
    - Board: "AI Thinker ESP32-CAM" (or "ESP32 Wrover Module")
    - Partition scheme: "Huge APP (3MB No OTA)"
    - Upload: hold/connect GPIO0 to GND while uploading, then remove it and press RESET.
*/

#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>

// ====== EDIT THESE ======
const char* WIFI_SSID = "B6.15 (1) Dekostel_5G";
const char* WIFI_PASS = "godbless";
// ========================

// AI-Thinker ESP32-CAM pin map
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

#define FLASH_LED_PIN      4

WebServer server(80);

bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 10000000;           // 10MHz is more tolerant than 20MHz
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

  if (psramFound()) {
    config.frame_size   = FRAMESIZE_SVGA;   // 800x600
    config.jpeg_quality = 12;               // lower = better quality
    config.fb_count     = 1;
    config.fb_location  = CAMERA_FB_IN_PSRAM;
    Serial.println("PSRAM found");
  } else {
    config.frame_size   = FRAMESIZE_QVGA;   // 320x240
    config.jpeg_quality = 15;
    config.fb_count     = 1;
    config.fb_location  = CAMERA_FB_IN_DRAM;
    Serial.println("No PSRAM - using small frame size");
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init FAILED, error 0x%x\n", err);
    return false;
  }

  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    Serial.printf("Sensor PID: 0x%02x\n", s->id.PID);  // 0x26 = OV2640
  }
  Serial.println("Camera init OK");
  return true;
}

bool testCapture() {
  delay(500);  // let the sensor settle after init
  for (int attempt = 1; attempt <= 3; attempt++) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) {
      Serial.printf("Capture OK (try %d): %dx%d, %u bytes\n", attempt, fb->width, fb->height, (unsigned)fb->len);
      esp_camera_fb_return(fb);
      return true;
    }
    Serial.printf("Capture try %d failed\n", attempt);
    delay(300);
  }
  Serial.println("Capture FAILED. Check: GPIO0 jumper removed, 5V/500mA+ power, ribbon cable latched.");
  return false;
}

void handleRoot() {
  String html =
    "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>ESP32-CAM Test</title></head><body style='font-family:sans-serif;text-align:center'>"
    "<h2>ESP32-CAM Test</h2>"
    "<img id='img' src='/capture' style='max-width:100%'><br><br>"
    "<button onclick=\"document.getElementById('img').src='/capture?t='+Date.now()\">Refresh</button>"
    "</body></html>";
  server.send(200, "text/html", html);
}

void handleCapture() {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    server.send(500, "text/plain", "Capture failed");
    return;
  }
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(fb->len);
  server.send(200, "image/jpeg", "");
  server.client().write((const char*)fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n--- ESP32-CAM test ---");

  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW);

  if (!initCamera()) {
    Serial.println("Check: camera ribbon cable seated, power supply (5V, >=500mA), correct board selected.");
    return;
  }
  testCapture();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi FAILED - camera test still passed above.");
    return;
  }

  Serial.print("WiFi OK. Open: http://");
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.on("/capture", handleCapture);
  server.begin();

  // Blink flash LED once = ready
  digitalWrite(FLASH_LED_PIN, HIGH);
  delay(150);
  digitalWrite(FLASH_LED_PIN, LOW);
}

void loop() {
  server.handleClient();
}