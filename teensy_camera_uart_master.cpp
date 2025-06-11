#include <Arduino.h>
#include <U8g2lib.h>
#include <DMAChannel.h>

// Select source and display type
// Uncomment the pair you want to use
#define USE_XIAO_UART      // XIAO on Serial7 (pins 28/29)
//#define USE_NICLA_UART   // Nicla on Serial2 (pins 7/8)
#define USE_CFA10110       // CFA10110 (ST75256, 240x128, 4-gray)
//#define USE_ST7565P      // CFA12864 (ST7565P, 128x64, mono)

// Serial port and baud rate
#if defined(USE_XIAO_UART)
  #define CAM_SERIAL   Serial7
  #define UART_BAUD    2000000
#elif defined(USE_NICLA_UART)
  #define CAM_SERIAL   Serial2
  #define UART_BAUD    2000000
#endif

// Display config
#if defined(USE_CFA10110)
  #include "display_config_teensy_cfa10110.h"
  #define DISPLAY_WIDTH 240
  #define DISPLAY_HEIGHT 128
  #define DISPLAY_GRAYSCALE_LEVELS 4
  #define DISPLAY_TYPE_ID 2
#elif defined(USE_ST7565P)
  #include "display_config_teensy_st7565p.h"
  #define DISPLAY_WIDTH 128
  #define DISPLAY_HEIGHT 64
  #define DISPLAY_GRAYSCALE_LEVELS 2
  #define DISPLAY_TYPE_ID 1
#else
  #error "No display selected!"
#endif

// U8g2 display instance
#if defined(USE_CFA10110)
  U8G2_ST75256_JLX240160_F_4W_HW_SPI u8g2(
    U8G2_R0,
    DISPLAY_CS_PIN,
    DISPLAY_DC_PIN,
    DISPLAY_RST_PIN
  );
#elif defined(USE_ST7565P)
  U8G2_ST7565_EA_DOGM128_F_4W_HW_SPI u8g2(
    U8G2_R0,
    DISPLAY_CS_PIN,
    DISPLAY_DC_PIN,
    DISPLAY_RST_PIN
  );
#endif

uint8_t *imageBuffer = nullptr;
size_t imageSize = 0;
unsigned long lastFrameTime = 0;
unsigned long frameCount = 0;
float fps = 0;
unsigned long processingTime = 0;

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("Teensy UART Camera Master");

  CAM_SERIAL.begin(UART_BAUD);
  delay(100);

  // Send display type to camera slave (XIAO or Nicla)
  CAM_SERIAL.write(0xB1); // set display target command
  CAM_SERIAL.write(DISPLAY_TYPE_ID);

  // Display setup
  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB10_tr);
  u8g2.drawStr(0, 20, "Teensy UART Master");
  u8g2.sendBuffer();
  delay(1000);

  // Allocate max buffer
#if defined(USE_CFA10110)
  imageSize = (DISPLAY_WIDTH * DISPLAY_HEIGHT) / 4;
#elif defined(USE_ST7565P)
  imageSize = (DISPLAY_WIDTH * DISPLAY_HEIGHT) / 8;
#endif
  imageBuffer = (uint8_t *)malloc(imageSize);
  if (!imageBuffer) {
    Serial.println("Failed to allocate image buffer");
    while (1);
  }
}

void loop() {
  unsigned long startTime = micros();

  // Request a frame
  CAM_SERIAL.write(0xA5); // capture and send

  // Wait for 4-byte header
  unsigned long t0 = millis();
  while (CAM_SERIAL.available() < 4 && (millis() - t0 < 100)) {}
  if (CAM_SERIAL.available() < 4) {
    Serial.println("Timeout waiting for header");
    delay(10);
    return;
  }
  size_t receivedSize = 0;
  receivedSize |= (size_t)CAM_SERIAL.read() << 24;
  receivedSize |= (size_t)CAM_SERIAL.read() << 16;
  receivedSize |= (size_t)CAM_SERIAL.read() << 8;
  receivedSize |= (size_t)CAM_SERIAL.read();

  if (receivedSize == 0 || receivedSize > imageSize) {
    Serial.printf("Invalid image size: %u\n", receivedSize);
    delay(10);
    return;
  }

  // Receive image data
  size_t idx = 0;
  t0 = millis();
  while (idx < receivedSize && (millis() - t0 < 200)) {
    if (CAM_SERIAL.available()) {
      imageBuffer[idx++] = CAM_SERIAL.read();
    }
  }
  if (idx < receivedSize) {
    Serial.println("Timeout receiving image data");
    delay(10);
    return;
  }

  // Display image
  u8g2.clearBuffer();
#if defined(USE_ST7565P)
  memcpy(u8g2.getBufferPtr(), imageBuffer, receivedSize);
#elif defined(USE_CFA10110)
  // 2-bit grayscale: convert to U8g2
  for (int y = 0; y < DISPLAY_HEIGHT; y++) {
    for (int x = 0; x < DISPLAY_WIDTH; x++) {
      int pixelIndex = y * DISPLAY_WIDTH + x;
      int byteIndex = pixelIndex / 4;
      int bitPos = (pixelIndex % 4) * 2;
      uint8_t gray = (imageBuffer[byteIndex] >> (6 - bitPos)) & 0x03;
      u8g2.setDrawColor(gray);
      u8g2.drawPixel(x, y);
    }
  }
#endif
  u8g2.sendBuffer();

  // FPS calculation
  frameCount++;
  unsigned long now = millis();
  if (now - lastFrameTime >= 1000) {
    fps = frameCount * 1000.0 / (now - lastFrameTime);
    frameCount = 0;
    lastFrameTime = now;
    Serial.printf("FPS: %.1f\n", fps);
  }

  processingTime = (micros() - startTime) / 1000;
  // Optionally display FPS/processing time as overlay
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.setDrawColor(1);
  char buf[32];
  sprintf(buf, "FPS: %.1f", fps);
  u8g2.drawStr(0, 8, buf);
  sprintf(buf, "ms: %lu", processingTime);
  u8g2.drawStr(0, DISPLAY_HEIGHT - 5, buf);
  u8g2.sendBuffer();
}
