#include <Arduino.h>
#include <U8g2lib.h>
#include <SPI.h>
#include "display_config_cfa10110.h"

// U8g2 constructor for CFA10110 display with hardware SPI
// CFA10110 is compatible with the ST75256 driver
// Using 4 gray levels mode
U8G2_ST75256_JLX240128_F_4W_HW_SPI u8g2(
  U8G2_R0,
  DISPLAY_CS_PIN, 
  DISPLAY_DC_PIN, 
  DISPLAY_RST_PIN
);

void setup() {
  Serial.begin(115200);
  while(!Serial) delay(10);
  Serial.println("XIAO ESP32S3 CFA10110 Display Test");
  
  // Initialize built-in LED for visual feedback
  pinMode(XIAO_LED_BUILTIN, OUTPUT);
  digitalWrite(XIAO_LED_BUILTIN, HIGH);  // Turn on LED during initialization
  
  // Initialize display
  u8g2.begin();
  u8g2.enableUTF8Print();  // Enable UTF8 support
  u8g2.setDrawColor(1);    // Set default draw color
  u8g2.clearBuffer();
  
  // Welcome message
  u8g2.setFont(u8g2_font_ncenB14_tr);
  u8g2.drawStr(20, 30, "CFA10110 Test");
  
  u8g2.setFont(u8g2_font_ncenB10_tr);
  u8g2.drawStr(20, 50, "Grayscale: 4 levels");
  u8g2.drawStr(20, 70, "Resolution: 240x128");
  
  u8g2.sendBuffer();
  delay(2000);
  
  digitalWrite(XIAO_LED_BUILTIN, LOW);  // Turn off LED after init
}

// Draw a grayscale test pattern across the screen
void drawGrayscalePattern() {
  u8g2.clearBuffer();
  
  // Title
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(5, 10, "Grayscale Pattern Test");
  
  // Large grayscale bars
  for (uint8_t i = 0; i < DISPLAY_GRAYSCALE_LEVELS; i++) {
    u8g2.setDrawColor(i);
    u8g2.drawBox(
      10 + (i * (DISPLAY_WIDTH - 20) / DISPLAY_GRAYSCALE_LEVELS), 
      20, 
      (DISPLAY_WIDTH - 20) / DISPLAY_GRAYSCALE_LEVELS, 
      30
    );
  }
  
  // Horizontal gradient
  for (uint8_t x = 0; x < DISPLAY_WIDTH; x++) {
    uint8_t gray = map(x, 0, DISPLAY_WIDTH-1, 0, DISPLAY_GRAYSCALE_LEVELS-1);
    u8g2.setDrawColor(gray);
    u8g2.drawVLine(x, 60, 20);
  }
  
  // Vertical gradient
  for (uint8_t y = 90; y < 110; y++) {
    uint8_t gray = map(y, 90, 109, 0, DISPLAY_GRAYSCALE_LEVELS-1);
    u8g2.setDrawColor(gray);
    u8g2.drawHLine(10, y, DISPLAY_WIDTH - 20);
  }
  
  // Checkerboard pattern with alternating gray levels
  u8g2.setDrawColor(1);
  for (uint8_t y = 0; y < 8; y++) {
    for (uint8_t x = 0; x < 16; x++) {
      uint8_t gray = (x + y) % DISPLAY_GRAYSCALE_LEVELS;
      u8g2.setDrawColor(gray);
      u8g2.drawBox(x * 15, 115 + y * 1.5, 15, 1.5);
    }
  }
  
  u8g2.sendBuffer();
}

// Draw basic graphics test
void drawGraphicsTest() {
  u8g2.clearBuffer();
  
  // Title
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.setDrawColor(DISPLAY_GRAYSCALE_LEVELS - 1);  // Darkest color
  u8g2.drawStr(5, 10, "Graphics Test");
  
  // Draw shapes in different gray levels
  u8g2.setDrawColor(1);
  u8g2.drawFrame(10, 20, 50, 40);   // Outline rectangle
  
  u8g2.setDrawColor(2);
  u8g2.drawBox(70, 20, 50, 40);     // Filled rectangle
  
  u8g2.setDrawColor(1);
  u8g2.drawCircle(145, 40, 25, U8G2_DRAW_ALL); // Circle outline
  
  u8g2.setDrawColor(2);
  u8g2.drawDisc(205, 40, 25, U8G2_DRAW_ALL);   // Filled circle
  
  // Draw lines with different gray levels
  for (uint8_t i = 0; i < DISPLAY_GRAYSCALE_LEVELS; i++) {
    u8g2.setDrawColor(i);
    u8g2.drawLine(10, 70 + (i * 10), 230, 70 + (i * 10));
  }
  
  // Draw text in different sizes
  u8g2.setDrawColor(DISPLAY_GRAYSCALE_LEVELS - 1);
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(10, 110, "XIAO ESP32S3 + CFA10110");
  
  u8g2.setFont(u8g2_font_ncenB14_tr);
  u8g2.drawStr(40, 125, "Display Test");
  
  u8g2.sendBuffer();
}

// Draw text with different fonts
void drawTextTest() {
  u8g2.clearBuffer();
  
  u8g2.setDrawColor(DISPLAY_GRAYSCALE_LEVELS - 1);
  
  // Title
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(5, 10, "Text Font Test");
  
  // Display different fonts
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(10, 30, "5x7 Font: XIAO ESP32S3");
  
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(10, 50, "NCenB08: XIAO ESP32S3");
  
  u8g2.setFont(u8g2_font_ncenB10_tr);
  u8g2.drawStr(10, 70, "NCenB10: XIAO ESP32S3");
  
  u8g2.setFont(u8g2_font_ncenB12_tr);
  u8g2.drawStr(10, 90, "NCenB12: XIAO ESP32S3");
  
  u8g2.setFont(u8g2_font_ncenB14_tr);
  u8g2.drawStr(10, 110, "NCenB14: XIAO");
  
  u8g2.sendBuffer();
}

// Current test mode
uint8_t testMode = 0;
const uint8_t NUM_TEST_MODES = 3;
unsigned long lastModeSwitch = 0;

void loop() {
  // Switch test mode every 3 seconds
  if (millis() - lastModeSwitch > 3000) {
    testMode = (testMode + 1) % NUM_TEST_MODES;
    lastModeSwitch = millis();
    
    // Blink LED to indicate mode change
    digitalWrite(XIAO_LED_BUILTIN, HIGH);
    delay(50);
    digitalWrite(XIAO_LED_BUILTIN, LOW);
  }
  
  // Run current test
  switch (testMode) {
    case 0:
      drawGrayscalePattern();
      break;
    case 1:
      drawGraphicsTest();
      break;
    case 2:
      drawTextTest();
      break;
  }
  
  delay(100); // Small delay for stability
}
