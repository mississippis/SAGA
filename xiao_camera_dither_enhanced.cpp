#include "esp_camera.h"
#include <Arduino.h>
#include <U8g2lib.h>
#include "soc/soc.h"          // For disabling brownout problems
#include "soc/rtc_cntl_reg.h" // For disabling brownout problems

// Display Pins (ST7565P)
#define DISPLAY_CS_PIN    44  // XIAO_D7
#define DISPLAY_DC_PIN    43  // XIAO_D6
#define DISPLAY_RST_PIN   5   // XIAO_D4

// Camera Pins
#define PWDN_GPIO_NUM -1    // Not used
#define RESET_GPIO_NUM -1   // Not used
#define XCLK_GPIO_NUM 45
#define SIOD_GPIO_NUM 40    // SCCB D (I2C SDA)
#define SIOC_GPIO_NUM 39    // SCCB C (I2C SCL)
#define Y9_GPIO_NUM 48      // D7
#define Y8_GPIO_NUM 10      // D6
#define Y7_GPIO_NUM 11      // D5
#define Y6_GPIO_NUM 12      // D4
#define Y5_GPIO_NUM 13      // D3
#define Y4_GPIO_NUM 14      // D2
#define Y3_GPIO_NUM 47      // D1
#define Y2_GPIO_NUM 46      // D0
#define VSYNC_GPIO_NUM 38
#define HREF_GPIO_NUM 41
#define PCLK_GPIO_NUM 42

// XIAO ESP32S3 Built-in LED
#define LED_PIN 21

// U8g2 constructor for ST7565 display with hardware SPI
// Uses default SPI pins for XIAO ESP32S3 (SCK: GPIO7, MOSI: GPIO9)
U8G2_ST7565_EA_DOGM128_F_4W_HW_SPI u8g2(U8G2_R0, DISPLAY_CS_PIN, DISPLAY_DC_PIN, DISPLAY_RST_PIN);
camera_config_t camera_config;

// Dithering constants
const int DITHER_DISPLAY_WIDTH = 128;
const int DITHER_DISPLAY_HEIGHT = 64;

// Performance metrics
unsigned long lastFrameTime = 0;
unsigned long frameCount = 0;
float fps = 0;
unsigned long processingTime = 0;

// Buffers for Floyd-Steinberg dithering (two-line approach)
// +2 for padding (1 pixel on each side)
float current_line_error[DITHER_DISPLAY_WIDTH + 2];
float next_line_error[DITHER_DISPLAY_WIDTH + 2];

void setup_camera() {
  camera_config.ledc_channel = LEDC_CHANNEL_0;
  camera_config.ledc_timer = LEDC_TIMER_0;
  camera_config.pin_d0 = Y2_GPIO_NUM;
  camera_config.pin_d1 = Y3_GPIO_NUM;
  camera_config.pin_d2 = Y4_GPIO_NUM;
  camera_config.pin_d3 = Y5_GPIO_NUM;
  camera_config.pin_d4 = Y6_GPIO_NUM;
  camera_config.pin_d5 = Y7_GPIO_NUM;
  camera_config.pin_d6 = Y8_GPIO_NUM;
  camera_config.pin_d7 = Y9_GPIO_NUM;
  camera_config.pin_xclk = XCLK_GPIO_NUM;
  camera_config.pin_pclk = PCLK_GPIO_NUM;
  camera_config.pin_vsync = VSYNC_GPIO_NUM;
  camera_config.pin_href = HREF_GPIO_NUM;
  camera_config.pin_sccb_sda = SIOD_GPIO_NUM;
  camera_config.pin_sccb_scl = SIOC_GPIO_NUM;
  camera_config.pin_pwdn = PWDN_GPIO_NUM;
  camera_config.pin_reset = RESET_GPIO_NUM;
  camera_config.xclk_freq_hz = 20000000;
  camera_config.pixel_format = PIXFORMAT_GRAYSCALE;
  camera_config.frame_size = FRAMESIZE_QQVGA; // 160x120
  camera_config.jpeg_quality = 12;
  camera_config.fb_count = 2;  // Use 2 frame buffers for better performance
  camera_config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

  esp_err_t err = esp_camera_init(&camera_config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    return;
  }
  Serial.println("Camera initialized successfully.");
  
  // Optional: Adjust camera settings for better image quality
  sensor_t * s = esp_camera_sensor_get();
  if (s) {
    s->set_brightness(s, 1);     // -2 to 2
    s->set_contrast(s, 1);       // -2 to 2
    s->set_saturation(s, 0);     // -2 to 2
    s->set_special_effect(s, 0); // 0 to 6 (0 - No Effect, 1 - Negative, 2 - Grayscale, 3 - Red Tint, 4 - Green Tint, 5 - Blue Tint, 6 - Sepia)
    s->set_whitebal(s, 1);       // 0 = disable , 1 = enable
    s->set_awb_gain(s, 1);       // 0 = disable , 1 = enable
    s->set_wb_mode(s, 0);        // 0 to 4 - if awb_gain enabled (0 - Auto, 1 - Sunny, 2 - Cloudy, 3 - Office, 4 - Home)
    s->set_exposure_ctrl(s, 1);  // 0 = disable , 1 = enable
    s->set_aec2(s, 0);           // 0 = disable , 1 = enable
    s->set_gain_ctrl(s, 1);      // 0 = disable , 1 = enable
    s->set_agc_gain(s, 0);       // 0 to 30
    s->set_gainceiling(s, (gainceiling_t)6);  // 0 to 6
    s->set_bpc(s, 0);            // 0 = disable , 1 = enable
    s->set_wpc(s, 1);            // 0 = disable , 1 = enable
    s->set_raw_gma(s, 1);        // 0 = disable , 1 = enable
    s->set_lenc(s, 1);           // 0 = disable , 1 = enable
    s->set_hmirror(s, 0);        // 0 = disable , 1 = enable
    s->set_vflip(s, 0);          // 0 = disable , 1 = enable
    s->set_dcw(s, 1);            // 0 = disable , 1 = enable
  }
}

void setup_display() {
  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 10, "XIAO ESP32S3");
  u8g2.drawStr(0, 25, "Dither Test");
  u8g2.sendBuffer();
  delay(1000);
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // Disable brownout detector
  
  // Initialize LED pin
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  Serial.begin(115200);
  while(!Serial) delay(10);
  Serial.println("\n\nXIAO ESP32S3 Camera to Dithered Display Test");

  // Initialize error buffers for dithering
  for (int i = 0; i < DITHER_DISPLAY_WIDTH + 2; ++i) {
    current_line_error[i] = 0.0f;
    next_line_error[i] = 0.0f;
  }

  setup_display();
  setup_camera();
}

void loop() {
  digitalWrite(LED_PIN, HIGH); // LED on during frame capture
  
  unsigned long startTime = millis();
  
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    digitalWrite(LED_PIN, LOW); // Turn LED off on failure
    delay(200);
    return;
  }

  if (fb->format != PIXFORMAT_GRAYSCALE) {
    Serial.println("Error: Unexpected camera pixel format!");
    esp_camera_fb_return(fb);
    digitalWrite(LED_PIN, LOW);
    return;
  }

  u8g2.clearBuffer();

  const int cam_width = fb->width;   // 160 for QQVGA
  const int cam_height = fb->height; // 120 for QQVGA

  int cam_x_offset = (cam_width > DITHER_DISPLAY_WIDTH) ? (cam_width - DITHER_DISPLAY_WIDTH) / 2 : 0;
  int cam_y_offset = (cam_height > DITHER_DISPLAY_HEIGHT) ? (cam_height - DITHER_DISPLAY_HEIGHT) / 2 : 0;

  digitalWrite(LED_PIN, LOW); // LED off during image processing

  // Reset error buffers for each new frame
  for (int i = 0; i < DITHER_DISPLAY_WIDTH + 2; ++i) {
    current_line_error[i] = 0.0f;
    next_line_error[i] = 0.0f;
  }

  // Floyd-Steinberg Dithering
  for (int y = 0; y < DITHER_DISPLAY_HEIGHT; y++) {
    // Swap error lines: current becomes previous, next becomes current
    for(int i = 0; i < DITHER_DISPLAY_WIDTH + 2; ++i) {
        current_line_error[i] = next_line_error[i];
        next_line_error[i] = 0.0f;
    }

    for (int x = 0; x < DITHER_DISPLAY_WIDTH; x++) {
      int cam_x = x + cam_x_offset;
      int cam_y = y + cam_y_offset;
      
      uint8_t original_pixel_val = 0;
      if (cam_x < cam_width && cam_y < cam_height) {
         original_pixel_val = fb->buf[cam_y * cam_width + cam_x];
      }

      // Add error from previous pixels (current_line_error[x+1] corresponds to current pixel x)
      float adjusted_pixel_val = (float)original_pixel_val + current_line_error[x + 1]; // x+1 due to padding

      // Quantize to black (0) or white (255)
      float quantized_val = (adjusted_pixel_val < 128.0f) ? 0.0f : 255.0f;
      
      if (quantized_val == 0.0f) { // Assuming 0 is black, and U8g2 draws black pixels
        u8g2.drawPixel(x, y);
      }

      float quant_error = adjusted_pixel_val - quantized_val;

      // Distribute error (indices are relative to the error buffer's padding)
      // Current pixel is at error buffer index x+1
      current_line_error[x + 2] += quant_error * 7.0f / 16.0f; // Pixel to the right
      next_line_error[x]     += quant_error * 3.0f / 16.0f; // Pixel below-left
      next_line_error[x + 1]   += quant_error * 5.0f / 16.0f; // Pixel directly below
      next_line_error[x + 2]   += quant_error * 1.0f / 16.0f; // Pixel below-right
    }
  }

  // Calculate and display performance metrics
  frameCount++;
  unsigned long currentTime = millis();
  processingTime = currentTime - startTime;
  
  if (currentTime - lastFrameTime >= 1000) { // Update FPS every second
    fps = frameCount * 1000.0 / (currentTime - lastFrameTime);
    frameCount = 0;
    lastFrameTime = currentTime;
  }

  // Draw FPS and processing time in a small corner of the display
  char buffer[16];
  u8g2.setFont(u8g2_font_5x7_tr); // Use a small font
  sprintf(buffer, "FPS:%.1f", fps);
  u8g2.drawStr(0, 7, buffer);
  sprintf(buffer, "ms:%lu", processingTime);
  u8g2.drawStr(64, 7, buffer);

  digitalWrite(LED_PIN, HIGH); // LED on during display update
  u8g2.sendBuffer();
  digitalWrite(LED_PIN, LOW);  // LED off after update

  esp_camera_fb_return(fb); // Return the frame buffer to be reused
}
