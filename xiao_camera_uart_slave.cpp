#include <Arduino.h>
#include <HardwareSerial.h>
#include <esp_camera.h>

// UART configuration
#define UART_PORT_NUM 2    // Use Serial2 (pins 7/8 on XIAO ESP32S3)
#define UART_BAUD     2000000  // 2Mbps for fast image transfer

// Camera pin config (from memory)
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     45
#define SIOD_GPIO_NUM     40
#define SIOC_GPIO_NUM     39
#define Y9_GPIO_NUM       48
#define Y8_GPIO_NUM       10
#define Y7_GPIO_NUM       11
#define Y6_GPIO_NUM       12
#define Y5_GPIO_NUM       13
#define Y4_GPIO_NUM       14
#define Y3_GPIO_NUM       47
#define Y2_GPIO_NUM       46
#define VSYNC_GPIO_NUM    38
#define HREF_GPIO_NUM     41
#define PCLK_GPIO_NUM     42

// Display targets
#define DISPLAY_ST7565P   1
#define DISPLAY_ST75256   2

// Global variables
uint8_t displayTarget = DISPLAY_ST7565P;
camera_fb_t *fb = NULL;
uint8_t *processedImage = NULL;
size_t processedSize = 0;

HardwareSerial CamSerial(UART_PORT_NUM);

void setupCamera() {
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
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_QVGA;  // 320x240
  config.pixel_format = PIXFORMAT_GRAYSCALE;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.fb_count = 2;
  config.jpeg_quality = 12;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    while (1);
  }
}

void processImageForDisplay(camera_fb_t *fb) {
  if (displayTarget == DISPLAY_ST7565P) {
    if (processedSize != 128*64/8) {
      free(processedImage);
      processedImage = (uint8_t *)malloc(128*64/8);
      processedSize = 128*64/8;
    }
    memset(processedImage, 0, processedSize);
    for (int y = 0; y < 64; y++) {
      for (int x = 0; x < 128; x++) {
        int srcX = (x * fb->width / 128);
        int srcY = (y * fb->height / 64);
        uint8_t pixel = fb->buf[srcY * fb->width + srcX];
        if (pixel > 128) {
          processedImage[y * 16 + (x / 8)] |= (1 << (7 - (x % 8)));
        }
      }
    }
  } else if (displayTarget == DISPLAY_ST75256) {
    if (processedSize != 240*128/4) {
      free(processedImage);
      processedImage = (uint8_t *)malloc(240*128/4);
      processedSize = 240*128/4;
    }
    memset(processedImage, 0, processedSize);
    for (int y = 0; y < 128; y++) {
      for (int x = 0; x < 240; x++) {
        int srcX = (x * fb->width / 240);
        int srcY = (y * fb->height / 128);
        uint8_t pixel = fb->buf[srcY * fb->width + srcX];
        uint8_t gray = pixel >> 6;
        int bytePos = (y * 240 + x) / 4;
        int bitPos = ((y * 240 + x) % 4) * 2;
        processedImage[bytePos] |= (gray << (6 - bitPos));
      }
    }
  }
}

void sendImageUART() {
  // Send a 4-byte header for image size
  CamSerial.write((uint8_t)(processedSize >> 24));
  CamSerial.write((uint8_t)(processedSize >> 16));
  CamSerial.write((uint8_t)(processedSize >> 8));
  CamSerial.write((uint8_t)(processedSize));
  // Send the image
  CamSerial.write(processedImage, processedSize);
}

void setup() {
  Serial.begin(115200);
  CamSerial.begin(UART_BAUD, SERIAL_8N1);
  setupCamera();
  Serial.println("XIAO UART Camera Slave ready");
}

void loop() {
  // Wait for a command from Teensy
  if (CamSerial.available()) {
    uint8_t cmd = CamSerial.read();
    if (cmd == 0xA5) { // 0xA5 = capture and send
      fb = esp_camera_fb_get();
      if (fb) {
        processImageForDisplay(fb);
        sendImageUART();
        esp_camera_fb_return(fb);
      }
    } else if (cmd == 0xB1) { // 0xB1 = set display target
      while (!CamSerial.available());
      displayTarget = CamSerial.read();
    }
  }
}
