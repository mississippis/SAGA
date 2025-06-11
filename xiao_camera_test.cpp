#include "esp_camera.h"
#include <Arduino.h>
#include "soc/soc.h"          // Disable brownout problems
#include "soc/rtc_cntl_reg.h" // Disable brownout problems

// Pin definition for CAMERA_MODEL_XIAO_ESP32S3
// Matches your xiao_esp32s3_pins.h and display_config.h
#define PWDN_GPIO_NUM -1    // XIAO_CAM_PWDN
#define RESET_GPIO_NUM -1   // XIAO_CAM_RESET
#define XCLK_GPIO_NUM 45    // XIAO_CAM_XCLK
#define SIOD_GPIO_NUM 40    // XIAO_CAM_SIOD (SDA)
#define SIOC_GPIO_NUM 39    // XIAO_CAM_SIOC (SCL)

#define Y9_GPIO_NUM 48      // XIAO_CAM_D7
#define Y8_GPIO_NUM 10      // XIAO_CAM_D6
#define Y7_GPIO_NUM 11      // XIAO_CAM_D5
#define Y6_GPIO_NUM 12      // XIAO_CAM_D4
#define Y5_GPIO_NUM 13      // XIAO_CAM_D3
#define Y4_GPIO_NUM 14      // XIAO_CAM_D2
#define Y3_GPIO_NUM 47      // XIAO_CAM_D1
#define Y2_GPIO_NUM 46      // XIAO_CAM_D0

#define VSYNC_GPIO_NUM 38   // XIAO_CAM_VSYNC
#define HREF_GPIO_NUM 41    // XIAO_CAM_HREF
#define PCLK_GPIO_NUM 42    // XIAO_CAM_PCLK

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // disable brownout detector

  Serial.begin(115200);
  while(!Serial) delay(10);
  Serial.setDebugOutput(true);
  Serial.println("XIAO ESP32S3 Camera Test");

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
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
  config.pin_sccb_sda = SIOD_GPIO_NUM;    // ESP32S3 specific, use sccb_sda
  config.pin_sccb_scl = SIOC_GPIO_NUM;    // ESP32S3 specific, use sccb_scl
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_GRAYSCALE; // For ST7565P, grayscale is a good start
  config.frame_size = FRAMESIZE_QQVGA;     // 160x120, small and fast
  config.jpeg_quality = 12;                // 0-63, lower means higher quality
  config.fb_count = 1;                     // If more than one, i2s runs in continuous mode. Use 1 for single frame mode.
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY; // Or CAMERA_GRAB_LATEST

  // Camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    return;
  } else {
    Serial.println("Camera initialized successfully.");
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s == NULL) {
    Serial.println("Failed to get camera sensor properties.");
  } else {
    Serial.printf("Camera PID: 0x%02X, VER: 0x%02X, MIDL: 0x%02X, MIDH: 0x%02X\n",
                  s->id.PID, s->id.VER, s->id.MIDL, s->id.MIDH);
    // s->set_framesize(s, FRAMESIZE_QVGA); // Example: You can change settings after init
  }
}

void loop() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    delay(1000);
    return;
  }

  Serial.printf("Frame captured! Size: %dx%d, Length: %u bytes, Format: %d\n",
                fb->width, fb->height, fb->len, fb->format);

  // For this basic test, we just print info. Later we'll process/display.

  esp_camera_fb_return(fb); // Return the frame buffer

  delay(5000); // Capture a frame every 5 seconds
}
