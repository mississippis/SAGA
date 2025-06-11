#include <Arduino.h>
#include <U8g2lib.h>
#include <SPI.h>
#include "display_config_teensy_st7565p.h"

#if defined(__IMXRT1062__) // Teensy 4.x
#include <DMAChannel.h>
#include <SPI_MSTransfer_T4.h>
#endif

// U8g2 constructor for ST7565 display with hardware SPI
// Using EA_DOGM128 as it's a common ST7565 variant that works well
U8G2_ST7565_EA_DOGM128_F_4W_HW_SPI u8g2(
  U8G2_R0, 
  DISPLAY_CS_PIN, 
  DISPLAY_DC_PIN, 
  DISPLAY_RST_PIN
);

// Performance metrics
unsigned long lastFrameTime = 0;
unsigned long frameCount = 0;
float fps = 0;
unsigned long processingTime = 0;

// For optimized SPI transfers on Teensy 4.x
#if defined(__IMXRT1062__)
SPISettings optimizedSPISettings(30000000, MSBFIRST, SPI_MODE0); // 30 MHz SPI speed for Teensy 4.1
DMAChannel dma;
bool dmaInitialized = false;
#endif

void setup() {
  // Initialize serial for debugging
  Serial.begin(115200);
  delay(500); // Give serial port time to connect
  Serial.println("Teensy 4.1 ST7565P Display Test - Optimized");
  
  // Initialize built-in LED for visual feedback
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);  // Turn on LED during initialization
  
  // Manually initialize SPI for optimal performance on Teensy 4.1
  SPI.begin();
  
#if defined(__IMXRT1062__)
  // Optimize SPI for Teensy 4.1
  SPI.beginTransaction(optimizedSPISettings);
  
  // Check if we can set up DMA for even faster transfers
  if (!dmaInitialized) {
    dma.begin(true); // Allocate DMA channel
    if (dma.allocate()) {
      // DMA channel allocated successfully
      dmaInitialized = true;
      Serial.println("DMA initialized for SPI transfers");
    } else {
      Serial.println("Failed to allocate DMA channel");
    }
  }
#endif

  // Initialize display
  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB10_tr);
  u8g2.drawStr(0, 20, "Teensy 4.1");
  u8g2.drawStr(0, 40, "ST7565P Test");
  u8g2.drawStr(0, 55, "Optimized");
  u8g2.sendBuffer();
  
  delay(2000);
  digitalWrite(LED_BUILTIN, LOW);  // Turn off LED after initialization
}

// Draw basic patterns test - optimized for Teensy 4.1
void drawPatternTest() {
  u8g2.clearBuffer();
  
  // Title
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 8, "Pattern Test");
  
  // Checkerboard pattern - optimized to minimize draw calls
  const uint8_t tileSize = 8;
  const uint8_t startY = 10;
  
  for (int y = 0; y < 6; y++) {
    for (int x = 0; x < 16; x++) {
      if ((x + y) % 2 == 0) {
        u8g2.drawBox(x * tileSize, startY + y * tileSize, tileSize, tileSize);
      }
    }
  }
  
  // Display FPS counter
  char buffer[16];
  u8g2.setFont(u8g2_font_5x7_tr);
  sprintf(buffer, "FPS: %.1f", fps);
  u8g2.drawStr(85, 8, buffer);
  
  u8g2.sendBuffer();
}

// Draw a moving graphics demo - optimized
void drawAnimationTest() {
  static int pos = 0;
  pos = (pos + 2) % 128; // Increment by 2 for smoother animation
  
  u8g2.clearBuffer();
  
  // Title
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 8, "Animation");
  
  // Moving ball
  u8g2.drawDisc(pos, 32, 10);
  
  // Sine wave - using pre-calculated values for speed
  static const uint8_t sinValues[32] = {
    32, 36, 40, 44, 48, 51, 54, 56, 58, 59, 59, 59, 58, 56, 54, 51,
    48, 44, 40, 36, 32, 28, 24, 20, 16, 13, 10, 8, 6, 5, 5, 5
  };
  
  for (int x = 0; x < 128; x += 4) { // Step by 4 pixels for speed
    int sinIndex = (x / 4 + pos / 4) % 32;
    u8g2.drawPixel(x, sinValues[sinIndex]);
  }
  
  // Display FPS counter
  char buffer[16];
  u8g2.setFont(u8g2_font_5x7_tr);
  sprintf(buffer, "FPS: %.1f", fps);
  u8g2.drawStr(85, 8, buffer);
  
  u8g2.sendBuffer();
}

// Draw text test with different fonts - optimized
void drawTextTest() {
  u8g2.clearBuffer();
  
  // Title
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 8, "Font Test");
  
  // Multiple fonts - minimized to most efficient ones
  u8g2.setFont(u8g2_font_5x7_tr); // Small, efficient font
  u8g2.drawStr(0, 20, "5x7 Font ABC 123");
  
  u8g2.setFont(u8g2_font_6x10_tr); // Medium, efficient font
  u8g2.drawStr(0, 35, "6x10 ABC 123");
  
  u8g2.setFont(u8g2_font_9x15_tr); // Larger font
  u8g2.drawStr(0, 55, "9x15 ABC");
  
  // Display FPS counter
  u8g2.setFont(u8g2_font_5x7_tr);
  char buffer[16];
  sprintf(buffer, "FPS: %.1f", fps);
  u8g2.drawStr(85, 8, buffer);
  
  u8g2.sendBuffer();
}

// Current test mode
uint8_t testMode = 0;
const uint8_t NUM_TEST_MODES = 3;
unsigned long lastModeSwitch = 0;

void loop() {
  // Track frame timing for FPS calculation
  unsigned long frameStart = micros(); // Use microseconds for higher precision
  
  // Switch test mode every 3 seconds
  if (millis() - lastModeSwitch > 3000) {
    testMode = (testMode + 1) % NUM_TEST_MODES;
    lastModeSwitch = millis();
    
    // Blink LED to indicate mode change
    digitalWrite(LED_BUILTIN, HIGH);
    delay(50);
    digitalWrite(LED_BUILTIN, LOW);
  }
  
  // Run current test mode
  switch (testMode) {
    case 0:
      drawPatternTest();
      break;
    case 1:
      drawAnimationTest();
      break;
    case 2:
      drawTextTest();
      break;
  }
  
  // Calculate and update FPS
  frameCount++;
  unsigned long currentTime = millis();
  processingTime = (micros() - frameStart) / 1000; // Convert to milliseconds
  
  if (currentTime - lastFrameTime >= 1000) { // Update FPS every second
    fps = frameCount * 1000.0 / (currentTime - lastFrameTime);
    frameCount = 0;
    lastFrameTime = currentTime;
    
    // Print performance info to serial for monitoring
    Serial.print("FPS: ");
    Serial.print(fps);
    Serial.print(", Processing time (ms): ");
    Serial.println(processingTime);
  }
}
