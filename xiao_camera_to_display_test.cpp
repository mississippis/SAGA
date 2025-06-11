#include "esp_camera.h"
#include <Arduino.h>
#include <U8g2lib.h>
#include "soc/soc.h"          // For disabling brownout problems
#include "soc/rtc_cntl_reg.h" // For disabling brownout problems

// Display Pins (from display_config.h & xiao_esp32s3_pins.h via previous context)
#define DISPLAY_CS_PIN    44  // XIAO_D7 -> GPIO44
#define DISPLAY_DC_PIN    43  // XIAO_D6 -> GPIO43
#define DISPLAY_RST_PIN   5   // XIAO_D4 -> GPIO5

// Camera Pins (consistent with xiao_camera_test.cpp)
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

// U8g2 constructor for ST7565 display with hardware SPI.
// Uses default SPI pins for XIAO ESP32S3 (SCK: GPIO7, MOSI: GPIO9).
U8G2_ST7565_EA_DOGM128_F_4W_HW_SPI u8g2(U8G2_R0, /* cs=*/ DISPLAY_CS_PIN, /* dc=*/ DISPLAY_DC_PIN, /* reset=*/ DISPLAY_RST_PIN);

camera_config_t camera_config;

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
  camera_config.fb_count = 1; // Use 1 frame buffer for this simple test
  camera_config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

  esp_err_t err = esp_camera_init(&camera_config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    return;
  }
  Serial.println("Camera initialized successfully.");
}

void setup_display() {
  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr); // Choose a suitable font
  u8g2.drawStr(0, 10, "Cam->Disp Test");
  u8g2.sendBuffer();
  delay(1000); // Show initial message
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // Disable brownout detector

  Serial.begin(115200);
  while(!Serial) delay(10); // Wait for serial connection
  Serial.println("XIAO ESP32S3 Camera to Display Test");

  setup_camera();
  setup_display();
}

void loop() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    delay(200); // Shorter delay on failure to retry sooner
    return;
  }

  // Serial.printf("Frame: %dx%d, Len: %d, Format: %d\n", fb->width, fb->height, fb->len, fb->format);

  if (fb->format != PIXFORMAT_GRAYSCALE) {
    Serial.println("Error: Unexpected camera pixel format!");
    esp_camera_fb_return(fb);
    return;
  }

  u8g2.clearBuffer(); // Clear display's internal buffer

  const int display_width = u8g2.getDisplayWidth();   // Should be 128 for DOGM128
  const int display_height = u8g2.getDisplayHeight(); // Should be 64 for DOGM128

  const int cam_width = fb->width;   // 160 for QQVGA
  const int cam_height = fb->height; // 120 for QQVGA

  // Calculate offsets to crop a display_width x display_height region from the center of the camera image
  int cam_x_offset = (cam_width > display_width) ? (cam_width - display_width) / 2 : 0;
  int cam_y_offset = (cam_height > display_height) ? (cam_height - display_height) / 2 : 0;

  for (int dy = 0; dy < display_height; dy++) {
    for (int dx = 0; dx < display_width; dx++) {
      int cam_x = dx + cam_x_offset;
      int cam_y = dy + cam_y_offset;

      // Ensure we are within the camera frame buffer bounds
      if (cam_x < cam_width && cam_y < cam_height) {
        uint8_t pixel_value = fb->buf[cam_y * cam_width + cam_x];
        
        // Simple thresholding for monochrome display
        if (pixel_value > 127) { // Adjust threshold as needed (0-255)
          u8g2.drawPixel(dx, dy); // Draw a "set" pixel
        }
        // else: pixel is "clear", u8g2.clearBuffer() already handled this.
      }
    }
  }

  u8g2.sendBuffer(); // Transfer internal buffer to the physical display

  esp_camera_fb_return(fb); // Return the frame buffer to be reused

  // delay(100); // Optional delay to control frame rate
}
