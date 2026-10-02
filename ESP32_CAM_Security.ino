/**
 * ======================================================================================
 * AI ARENA - ESP32-CAM (GC2145) - LOCAL DEMO VERSION
 * ======================================================================================
 *
 * LOCAL ARCHITECTURE:
 *
 *   ESP32-CAM
 *       |
 *       | Wi-Fi / same phone hotspot
 *       v
 *   Laptop: 10.216.115.50
 *       |
 *       | Port 3000
 *       v
 *   AI ARENA LOCAL SERVER
 *       |
 *       v
 *   http://localhost:3000
 *
 * Camera:
 *   GC2145
 *   RGB565 capture -> JPEG conversion for WebSocket
 *
 * Motion:
 *   PIR GPIO 13
 *   Flash GPIO 4
 *   Motion -> Flash ON -> Capture -> Flash OFF
 *   -> Telegram
 *   -> Local AI ARENA server
 *
 * BOARD:
 *   AI Thinker ESP32-CAM
 *   ESP32 Core 2.0.17
 * ======================================================================================
 */

#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>

// ======================================================================================
// AI THINKER ESP32-CAM PIN DEFINITIONS
// ======================================================================================

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
#define STATUS_LED_PIN    33
#define PIR_SENSOR_PIN    13

// ======================================================================================
// CONFIGURATION
// ======================================================================================

// --------------------------------------------------------------------------------------
// 1. PHONE HOTSPOT
// --------------------------------------------------------------------------------------

const char *WIFI_SSID     = "Dhelari Beti";
const char *WIFI_PASSWORD = "123456789";

// --------------------------------------------------------------------------------------
// 2. LOCAL AI ARENA SERVER
// --------------------------------------------------------------------------------------
//
// IMPORTANT:
// Laptop IPv4 = 10.216.115.50
// Local server = port 3000
//
// ESP32 and laptop MUST be connected to the same phone hotspot.
//

const char *RELAY_HOST = "10.216.115.50";
const uint16_t RELAY_PORT = 3000;
const bool USE_SSL = false;

// Authentication token
const char *DEVICE_TOKEN = "ai_arena_sec_token_esp32_2026";
const char *DEVICE_ID = "CAM-01-ARENA";

// --------------------------------------------------------------------------------------
// 3. TELEGRAM
// --------------------------------------------------------------------------------------
//
// KEEP YOUR EXISTING TELEGRAM VALUES HERE.
//

const char *TELEGRAM_BOT_TOKEN = "8815061879:AAGOVvk-OC9TBiw9ypmx5mg39UrtlHUSznU";
const char *TELEGRAM_CHAT_ID   = "6554902488";

// --------------------------------------------------------------------------------------
// 4. CAMERA / STREAM SETTINGS
// --------------------------------------------------------------------------------------

const int TARGET_STREAM_FPS = 10;

// IMPORTANT:
// GC2145 is working with RGB565 in your current setup.
const framesize_t STREAM_FRAME_SIZE = FRAMESIZE_VGA;

// JPEG quality used ONLY while converting RGB565 -> JPEG
const int JPEG_QUALITY = 75;

// ======================================================================================
// GLOBAL OBJECTS
// ======================================================================================

WebSocketsClient webSocket;
WiFiClientSecure secureClient;

bool isWsConnected = false;

unsigned long lastFrameSentMs = 0;
const unsigned long frameIntervalMs = 1000 / TARGET_STREAM_FPS;

// ======================================================================================
// MOTION STATE
// ======================================================================================

volatile bool motionDetectedFlag = false;

unsigned long lastMotionTriggerMs = 0;

const unsigned long motionCooldownMs = 8000;

// ======================================================================================
// PIR INTERRUPT
// ======================================================================================

void IRAM_ATTR pirMotionISR() {
  motionDetectedFlag = true;
}

// ======================================================================================
// CAMERA INITIALIZATION
// ======================================================================================

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

  config.pin_xclk  = XCLK_GPIO_NUM;
  config.pin_pclk  = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href  = HREF_GPIO_NUM;

  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;

  config.pin_pwdn  = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  config.xclk_freq_hz = 20000000;

  // ============================================================
  // IMPORTANT:
  // GC2145 working configuration
  // ============================================================

  config.pixel_format = PIXFORMAT_RGB565;

  config.frame_size   = STREAM_FRAME_SIZE;

  config.jpeg_quality = JPEG_QUALITY;

  config.fb_count = 2;

  config.grab_mode = CAMERA_GRAB_LATEST;

  // ============================================================

  if (psramFound()) {

    Serial.println(
      "[CAM] PSRAM detected. Allocating dual framebuffers."
    );

    config.fb_count = 2;

  } else {

    Serial.println(
      "[CAM] WARNING: No PSRAM detected. Restricting frame size to CIF."
    );

    config.frame_size = FRAMESIZE_CIF;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);

  if (err != ESP_OK) {

    Serial.printf(
      "[CAM] ERROR: Camera init failed with error 0x%x\n",
      err
    );

    return false;
  }

  sensor_t *s = esp_camera_sensor_get();

  if (s != NULL) {

    s->set_brightness(s, 1);
    s->set_contrast(s, 1);
    s->set_saturation(s, 0);
    s->set_special_effect(s, 0);

    s->set_whitebal(s, 1);
    s->set_awb_gain(s, 1);
    s->set_exposure_ctrl(s, 1);
  }

  Serial.println("[CAM] Camera initialized successfully.");

  return true;
}

// ======================================================================================
// WEBSOCKET EVENT HANDLER
// ======================================================================================

void webSocketEvent(
  WStype_t type,
  uint8_t *payload,
  size_t length
) {

  switch (type) {

    // ----------------------------------------------------------------
    case WStype_DISCONNECTED:

      isWsConnected = false;

      Serial.println(
        "[WS] Disconnected from local AI ARENA relay."
      );

      break;

    // ----------------------------------------------------------------
    case WStype_CONNECTED:

      isWsConnected = true;

      Serial.println(
        "[WS] Connected to LOCAL AI ARENA relay!"
      );

      Serial.println(
        "[WS] Local stream pipe OPEN."
      );

      {

        StaticJsonDocument<200> doc;

        doc["type"] = "HEARTBEAT";
        doc["deviceId"] = DEVICE_ID;
        doc["pirArmed"] = true;
        doc["telegramStatus"] = "CONNECTED";
        doc["wifiRssi"] = WiFi.RSSI();

        String jsonStr;

        serializeJson(doc, jsonStr);

        webSocket.sendTXT(jsonStr);
      }

      break;

    // ----------------------------------------------------------------
    case WStype_TEXT:

      Serial.printf(
        "[WS] Command received: %s\n",
        payload
      );

      break;

    // ----------------------------------------------------------------
    case WStype_BIN:

      break;

    // ----------------------------------------------------------------
    case WStype_ERROR:

      Serial.println(
        "[WS] WebSocket error."
      );

      break;

    // ----------------------------------------------------------------
    default:

      break;
  }
}

// ======================================================================================
// TELEGRAM PHOTO
// ======================================================================================

bool sendPhotoToTelegram(
  uint8_t *imageBuffer,
  size_t imageLength
) {

  // If token has not been configured
  if (String(TELEGRAM_BOT_TOKEN).indexOf("YOUR_") >= 0) {

    Serial.println(
      "[TELEGRAM] Bot token unconfigured. Skipping Telegram upload."
    );

    return false;
  }

  Serial.println(
    "[TELEGRAM] Transmitting snapshot to Telegram..."
  );

  secureClient.setInsecure();

  HTTPClient https;

  String url =
    "https://api.telegram.org/bot" +
    String(TELEGRAM_BOT_TOKEN) +
    "/sendPhoto";

  if (!https.begin(secureClient, url)) {

    Serial.println(
      "[TELEGRAM] HTTPS connection failed."
    );

    return false;
  }

  String boundary =
    "----AiArenaBoundary" +
    String(millis());

  https.addHeader(
    "Content-Type",
    "multipart/form-data; boundary=" + boundary
  );

  String bodyStart = "";

  bodyStart +=
    "--" + boundary + "\r\n";

  bodyStart +=
    "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n";

  bodyStart +=
    String(TELEGRAM_CHAT_ID) + "\r\n";

  bodyStart +=
    "--" + boundary + "\r\n";

  bodyStart +=
    "Content-Disposition: form-data; name=\"caption\"\r\n\r\n";

  bodyStart +=
    "AI ARENA: Motion detected at " +
    String(DEVICE_ID) +
    "!\r\n";

  bodyStart +=
    "--" + boundary + "\r\n";

  bodyStart +=
    "Content-Disposition: form-data; name=\"photo\"; filename=\"snapshot.jpg\"\r\n";

  bodyStart +=
    "Content-Type: image/jpeg\r\n\r\n";

  String bodyEnd =
    "\r\n--" +
    boundary +
    "--\r\n";

  size_t totalLen =
    bodyStart.length() +
    imageLength +
    bodyEnd.length();

  uint8_t *fullPayload =
    (uint8_t *)malloc(totalLen);

  if (!fullPayload) {

    Serial.println(
      "[TELEGRAM] Memory allocation error."
    );

    https.end();

    return false;
  }

  memcpy(
    fullPayload,
    bodyStart.c_str(),
    bodyStart.length()
  );

  memcpy(
    fullPayload + bodyStart.length(),
    imageBuffer,
    imageLength
  );

  memcpy(
    fullPayload +
      bodyStart.length() +
      imageLength,
    bodyEnd.c_str(),
    bodyEnd.length()
  );

  int httpCode =
    https.POST(
      fullPayload,
      totalLen
    );

  free(fullPayload);

  if (
    httpCode == HTTP_CODE_OK ||
    httpCode == 200
  ) {

    Serial.println(
      "[TELEGRAM] Snapshot delivered successfully!"
    );

    https.end();

    return true;
  }

  Serial.printf(
    "[TELEGRAM] Failed. HTTP response: %d\n",
    httpCode
  );

  https.end();

  return false;
}

// ======================================================================================
// SEND MOTION EVENT TO LOCAL SERVER
// ======================================================================================

void sendMotionEventToCloud(
  uint8_t *imageBuffer,
  size_t imageLength,
  bool telegramSuccess
) {

  Serial.println(
    "[RELAY] Posting motion alert to LOCAL AI ARENA server..."
  );

  WiFiClient client;

  HTTPClient http;

  String protocol =
    USE_SSL ? "https://" : "http://";

  String url =
    protocol +
    String(RELAY_HOST) +
    ":" +
    String(RELAY_PORT) +
    "/api/device/events";

  Serial.print("[RELAY] URL: ");
  Serial.println(url);

  if (!http.begin(client, url)) {

    Serial.println(
      "[RELAY] HTTP connection failed."
    );

    return;
  }

  String boundary =
    "----AiArenaMotionEvent" +
    String(millis());

  http.addHeader(
    "Content-Type",
    "multipart/form-data; boundary=" +
    boundary
  );

  http.addHeader(
    "x-device-token",
    DEVICE_TOKEN
  );

  String bodyStart = "";

  bodyStart +=
    "--" + boundary + "\r\n";

  bodyStart +=
    "Content-Disposition: form-data; name=\"source\"\r\n\r\n";

  bodyStart +=
    "ESP32-PIR\r\n";

  bodyStart +=
    "--" + boundary + "\r\n";

  bodyStart +=
    "Content-Disposition: form-data; name=\"pirPin\"\r\n\r\n";

  bodyStart +=
    "13\r\n";

  bodyStart +=
    "--" + boundary + "\r\n";

  bodyStart +=
    "Content-Disposition: form-data; name=\"telegramSent\"\r\n\r\n";

  bodyStart +=
    String(
      telegramSuccess ? "true" : "false"
    ) +
    "\r\n";

  bodyStart +=
    "--" + boundary + "\r\n";

  bodyStart +=
    "Content-Disposition: form-data; name=\"snapshot\"; filename=\"motion.jpg\"\r\n";

  bodyStart +=
    "Content-Type: image/jpeg\r\n\r\n";

  String bodyEnd =
    "\r\n--" +
    boundary +
    "--\r\n";

  size_t totalLen =
    bodyStart.length() +
    imageLength +
    bodyEnd.length();

  uint8_t *fullPayload =
    (uint8_t *)malloc(totalLen);

  if (fullPayload) {

    memcpy(
      fullPayload,
      bodyStart.c_str(),
      bodyStart.length()
    );

    memcpy(
      fullPayload + bodyStart.length(),
      imageBuffer,
      imageLength
    );

    memcpy(
      fullPayload +
        bodyStart.length() +
        imageLength,
      bodyEnd.c_str(),
      bodyEnd.length()
    );

    int code =
      http.POST(
        fullPayload,
        totalLen
      );

    free(fullPayload);

    Serial.printf(
      "[RELAY] Motion event HTTP Code: %d\n",
      code
    );

  } else {

    Serial.println(
      "[RELAY] Memory allocation failed."
    );
  }

  http.end();
}

// ======================================================================================
// MOTION CAPTURE PIPELINE
// ======================================================================================

void processMotionCapture() {

  Serial.println(
    "================================================="
  );

  Serial.println(
    "[PIR] MOTION DETECTED!"
  );

  Serial.println(
    "================================================="
  );

  // ----------------------------------------------------------------
  // FLASH ON
  // ----------------------------------------------------------------

  digitalWrite(
    FLASH_LED_PIN,
    HIGH
  );

  delay(120);

  // ----------------------------------------------------------------
  // CAPTURE
  // ----------------------------------------------------------------

  camera_fb_t *fb =
    esp_camera_fb_get();

  // ----------------------------------------------------------------
  // FLASH OFF IMMEDIATELY
  // ----------------------------------------------------------------

  digitalWrite(
    FLASH_LED_PIN,
    LOW
  );

  if (!fb) {

    Serial.println(
      "[PIR] ERROR: Failed to capture snapshot."
    );

    return;
  }

  // ----------------------------------------------------------------
  // RGB565 -> JPEG
  // ----------------------------------------------------------------

  uint8_t *jpegBuffer =
    NULL;

  size_t jpegLen = 0;

  bool converted =
    frame2jpg(
      fb,
      80,
      &jpegBuffer,
      &jpegLen
    );

  if (!converted) {

    Serial.println(
      "[PIR] ERROR: RGB565 -> JPEG conversion failed."
    );

    esp_camera_fb_return(fb);

    return;
  }

  Serial.printf(
    "[PIR] JPEG created: %u bytes\n",
    (unsigned int)jpegLen
  );

  // ----------------------------------------------------------------
  // TELEGRAM
  // ----------------------------------------------------------------

  bool telegramSuccess =
    sendPhotoToTelegram(
      jpegBuffer,
      jpegLen
    );

  // ----------------------------------------------------------------
  // LOCAL AI ARENA SERVER
  // ----------------------------------------------------------------

  sendMotionEventToCloud(
    jpegBuffer,
    jpegLen,
    telegramSuccess
  );

  // ----------------------------------------------------------------
  // FREE JPEG
  // ----------------------------------------------------------------

  if (jpegBuffer) {

    free(jpegBuffer);
  }

  esp_camera_fb_return(fb);

  Serial.println(
    "[PIR] Capture pipeline complete."
  );

  Serial.println(
    "[PIR] Resuming live stream."
  );
}

// ======================================================================================
// SETUP
// ======================================================================================

void setup() {

  Serial.begin(115200);

  delay(500);

  Serial.println(
    "\n============================================="
  );

  Serial.println(
    "   AI ARENA - ESP32-CAM LOCAL DEMO"
  );

  Serial.println(
    "============================================="
  );

  // ----------------------------------------------------------------
  // FLASH
  // ----------------------------------------------------------------

  pinMode(
    FLASH_LED_PIN,
    OUTPUT
  );

  digitalWrite(
    FLASH_LED_PIN,
    LOW
  );

  // ----------------------------------------------------------------
  // STATUS LED
  // ----------------------------------------------------------------

  pinMode(
    STATUS_LED_PIN,
    OUTPUT
  );

  digitalWrite(
    STATUS_LED_PIN,
    HIGH
  );

  // ----------------------------------------------------------------
  // PIR
  // ----------------------------------------------------------------

  pinMode(
    PIR_SENSOR_PIN,
    INPUT_PULLDOWN
  );

  attachInterrupt(
    digitalPinToInterrupt(PIR_SENSOR_PIN),
    pirMotionISR,
    RISING
  );

  // ----------------------------------------------------------------
  // CAMERA
  // ----------------------------------------------------------------

  if (!initCamera()) {

    Serial.println(
      "[CAM] FATAL: Camera initialization failed."
    );

    while (true) {

      digitalWrite(
        STATUS_LED_PIN,
        LOW
      );

      delay(200);

      digitalWrite(
        STATUS_LED_PIN,
        HIGH
      );

      delay(200);
    }
  }

  // ----------------------------------------------------------------
  // WIFI
  // ----------------------------------------------------------------

  Serial.printf(
    "[WIFI] Connecting to SSID: %s\n",
    WIFI_SSID
  );

  WiFi.mode(WIFI_STA);

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );

  int retries = 0;

  while (
    WiFi.status() != WL_CONNECTED &&
    retries < 30
  ) {

    delay(500);

    Serial.print(".");

    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {

    Serial.println(
      "\n[WIFI] Connected successfully!"
    );

    Serial.printf(
      "[WIFI] ESP32 IP: %s\n",
      WiFi.localIP().toString().c_str()
    );

    Serial.printf(
      "[WIFI] RSSI: %d dBm\n",
      WiFi.RSSI()
    );

  } else {

    Serial.println(
      "\n[WIFI] Connection failed."
    );
  }

  // ----------------------------------------------------------------
  // LOCAL WEBSOCKET
  // ----------------------------------------------------------------

  String wsUrl =
    "/ws/camera?token=" +
    String(DEVICE_TOKEN);

  Serial.println(
    "[WS] Connecting to LOCAL AI ARENA server..."
  );

  Serial.printf(
    "[WS] Target: %s:%d%s\n",
    RELAY_HOST,
    RELAY_PORT,
    wsUrl.c_str()
  );

  if (USE_SSL) {

    webSocket.beginSSL(
      RELAY_HOST,
      RELAY_PORT,
      wsUrl.c_str()
    );

  } else {

    webSocket.begin(
      RELAY_HOST,
      RELAY_PORT,
      wsUrl.c_str()
    );
  }

  webSocket.onEvent(
    webSocketEvent
  );

  webSocket.setReconnectInterval(
    2500
  );

  webSocket.enableHeartbeat(
    15000,
    3000,
    2
  );

  Serial.println(
    "[SYSTEM] LOCAL DEMO READY."
  );
}

// ======================================================================================
// MAIN LOOP
// ======================================================================================

void loop() {

  // ----------------------------------------------------------------
  // WebSocket processing
  // ----------------------------------------------------------------

  webSocket.loop();

  // ----------------------------------------------------------------
  // PIR motion processing
  // ----------------------------------------------------------------

  if (motionDetectedFlag) {

    motionDetectedFlag = false;

    unsigned long now =
      millis();

    if (
      now - lastMotionTriggerMs >
      motionCooldownMs
    ) {

      lastMotionTriggerMs =
        now;

      processMotionCapture();
    }
  }

  // ----------------------------------------------------------------
  // LIVE STREAM
  // ----------------------------------------------------------------

  if (isWsConnected) {

    unsigned long now =
      millis();

    if (
      now - lastFrameSentMs >=
      frameIntervalMs
    ) {

      lastFrameSentMs =
        now;

      camera_fb_t *fb =
        esp_camera_fb_get();

      if (fb) {

        uint8_t *jpgBuf =
          NULL;

        size_t jpgLen =
          0;

        // RGB565 -> JPEG
        if (
          frame2jpg(
            fb,
            JPEG_QUALITY,
            &jpgBuf,
            &jpgLen
          )
        ) {

          webSocket.sendBIN(
            jpgBuf,
            jpgLen
          );

          free(jpgBuf);
        }

        esp_camera_fb_return(
          fb
        );
      }
    }
  }
}
