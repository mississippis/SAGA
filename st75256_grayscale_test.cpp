#include "Arduino.h"
#include <SPI.h>
#include "xiao_esp32s3_pins.h"

// Display dimensions for CFAG240128U0
#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 128

// Pin definitions for the display - XIAO ESP32S3
// Using pin definitions from xiao_esp32s3_pins.h
#define LCD_DC     XIAO_D2   // GPIO 3 - Data/Command
#define LCD_RESET  XIAO_D1   // GPIO 2 - Reset
#define LCD_CS     XIAO_D3   // GPIO 4 - Chip Select
// Hardware SPI pins are handled by the SPI library
// XIAO_SCK (GPIO 7) and XIAO_MOSI (GPIO 9) are used automatically

// Contrast setting - adjust as needed
uint8_t Vop = 0x2A; // 15.5v - affects display contrast

// Demo modes
typedef enum {
  GRAY_BARS,
  GRAY_GRADIENT_H,
  GRAY_GRADIENT_V,
  GRAY_RADIAL,
  GRAY_PATTERN,
  DEMO_COUNT
} DisplayMode;

// Current demo mode
DisplayMode currentMode = GRAY_BARS;

// Forward declarations
void executeCurrentDemo();

// Simple test image - 16x16 pattern
const uint8_t testPattern[16] = {
  0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 
  0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF
};

// Send command to the display
void SPI_sendCommand(uint8_t command) {
  digitalWrite(LCD_DC, LOW);  // Command mode
  digitalWrite(LCD_CS, LOW);  // Select display
  SPI.transfer(command);
  digitalWrite(LCD_CS, HIGH); // Deselect display
}

// Send data to the display
void SPI_sendData(uint8_t data) {
  digitalWrite(LCD_DC, HIGH);  // Data mode
  digitalWrite(LCD_CS, LOW);   // Select display
  SPI.transfer(data);
  digitalWrite(LCD_CS, HIGH);  // Deselect display
}

// Initialize the ST75256 display controller
void Initialize_Display() {
  // Hardware reset
  digitalWrite(LCD_RESET, LOW);
  delay(10);
  digitalWrite(LCD_RESET, HIGH);
  delay(100);
  
  // Extended command set
  SPI_sendCommand(0x30);  // Extension command 1
  SPI_sendCommand(0x6E);  // Set common scan - enable master
  SPI_sendCommand(0x31);  // Extension command 2
  SPI_sendCommand(0xD7);  // Disable auto read
  SPI_sendData(0x9F);

  // OTP related commands
  SPI_sendCommand(0xE0);  // Enable OTP read
  SPI_sendData(0x00);
  delay(1);
  SPI_sendCommand(0xE3);  // OTP Up-Load
  delay(2);
  SPI_sendCommand(0xE1);  // OTP Control Out

  // Continue with display initialization
  SPI_sendCommand(0x30);  // Extension command 1
  SPI_sendCommand(0x94);  // Sleep out
  SPI_sendCommand(0xAE);  // Display off
  delay(5);

  // Power control
  SPI_sendCommand(0x20);  // Power control
  SPI_sendData(0x0B);     // VB, VR, VF all on

  // Set voltage
  SPI_sendCommand(0x81);  // Set Vop
  SPI_sendData(Vop);      // Contrast value
  SPI_sendData(0x04);     // Secondary value
  
  // Setup grayscale levels
  SPI_sendCommand(0x31);         // Extension command 2
  SPI_sendCommand(0x20);         // Set grayscale levels
  // Gray level lookup table (16 values)
  SPI_sendData(0x00);  // Level 0 - G00
  SPI_sendData(0x00);  // Level 1 - G01
  SPI_sendData(0x00);  // Level 2 - G02
  SPI_sendData(0x00);  // Level 3 - G03
  SPI_sendData(0x11);  // Level 4 - G04
  SPI_sendData(0x11);  // Level 5 - G05
  SPI_sendData(0x11);  // Level 6 - G06
  SPI_sendData(0x00);  // Level 7 - G07
  SPI_sendData(0x00);  // Level 8 - G08
  SPI_sendData(0x22);  // Level 9 - G09
  SPI_sendData(0x00);  // Level 10 - G10
  SPI_sendData(0x00);  // Level 11 - G11
  SPI_sendData(0x33);  // Level 12 - G12
  SPI_sendData(0x33);  // Level 13 - G13
  SPI_sendData(0x33);  // Level 14 - G14
  SPI_sendData(0x00);  // Level 15 - G15

  // Analog circuit setting
  SPI_sendCommand(0x32);  // Analog circuit set
  SPI_sendData(0x00);     // Default value
  SPI_sendData(0x01);     // Booster efficiency
  SPI_sendData(0x02);     // Bias = 1/12

  // Booster level
  SPI_sendCommand(0x51);  // Booster level x10
  SPI_sendData(0xFB);     // Default value

  // Frame rate
  SPI_sendCommand(0xF0);  // Frame rate
  SPI_sendData(0x02);     // 77Hz frame rate
  SPI_sendData(0x08);     // Division ratio
  SPI_sendData(0x0F);     // Oscillation frequency
  SPI_sendData(0x18);     // Driver capability

  // Set to grayscale mode
  SPI_sendCommand(0x30);  // Extension command 1
  SPI_sendCommand(0xF0);  // Display mode
  SPI_sendData(0x11);     // 0x11 = 4-bit grayscale mode, 0x10 = monochrome

  // Display control
  SPI_sendCommand(0xCA);  // Display control
  SPI_sendData(0x00);     // CL dividing ratio
  SPI_sendData(0x7F);     // Duty set (128 lines)
  SPI_sendData(0x00);     // Frame inversion

  // Display scan direction
  SPI_sendCommand(0xBC);  // Data scan direction
  SPI_sendData(0x00);     // Normal scan direction

  // Display mode and power settings
  SPI_sendCommand(0xA6);  // Normal display (not inverted)
  SPI_sendCommand(0x31);  // Extension command 2
  SPI_sendCommand(0x40);  // Internal power supply

  // Set addressing window
  SPI_sendCommand(0x30);  // Extension command 1
  SPI_sendCommand(0x15);  // Column address setting
  SPI_sendData(0x00);     // Start column (0)
  SPI_sendData(0xEF);     // End column (239)
  SPI_sendCommand(0x75);  // Page address setting
  SPI_sendData(0x00);     // Start page (0)
  SPI_sendData(0x10);     // End page (16)

  // Turn display on
  SPI_sendCommand(0x30);  // Extension command 1
  SPI_sendCommand(0xAF);  // Display on
  
  Serial.println("ST75256 Display Initialized");
}

// Fill the display with a single value
void fillDisplay(uint8_t value) {
  SPI_sendCommand(0x30);  // Extension command 1
  SPI_sendCommand(0xF0);  // Display mode
  SPI_sendData(0x11);     // 0x11 = 4-bit grayscale mode
  SPI_sendCommand(0x5C);  // Write data to DDRAM

  // Create a byte with the same value in both nibbles
  uint8_t pixelByte = (value & 0x0F) | ((value & 0x0F) << 4);
  
  for (int y = 0; y < DISPLAY_HEIGHT; y++) {
    for (int x = 0; x < DISPLAY_WIDTH; x += 2) { // Process 2 pixels at once
      SPI_sendData(pixelByte);
    }
  }
}

// Draw horizontal grayscale bars
void drawGrayscaleBars() {
  SPI_sendCommand(0x30);  // Extension command 1
  SPI_sendCommand(0xF0);  // Display mode
  SPI_sendData(0x11);     // 0x11 = 4-bit grayscale mode
  SPI_sendCommand(0x5C);  // Write data to DDRAM

  // Draw 16 horizontal bars with different gray levels
  for (int level = 0; level < 16; level++) {
    int startY = level * (DISPLAY_HEIGHT / 16);
    int endY = (level + 1) * (DISPLAY_HEIGHT / 16);
    
    // Create a byte with the same grayscale level in both nibbles
    uint8_t pixelValue = (level << 4) | level;
    
    for (int y = startY; y < endY; y++) {
      for (int x = 0; x < DISPLAY_WIDTH; x += 2) { // Process 2 pixels at once
        SPI_sendData(pixelValue);
      }
    }
  }
}

// Draw horizontal grayscale gradient
void drawHorizontalGradient() {
  SPI_sendCommand(0x30);  // Extension command 1
  SPI_sendCommand(0xF0);  // Display mode
  SPI_sendData(0x11);     // 0x11 = 4-bit grayscale mode
  SPI_sendCommand(0x5C);  // Write data to DDRAM

  for (int y = 0; y < DISPLAY_HEIGHT; y++) {
    for (int x = 0; x < DISPLAY_WIDTH; x += 2) {
      // Calculate gray level (0-15) based on x position
      uint8_t level1 = (x * 15) / (DISPLAY_WIDTH - 1);
      uint8_t level2 = ((x+1) * 15) / (DISPLAY_WIDTH - 1);
      
      // Pack two 4-bit values in one byte (each nibble represents one pixel)
      SPI_sendData((level1 << 4) | level2);
    }
  }
}

// Draw vertical grayscale gradient
void drawVerticalGradient() {
  SPI_sendCommand(0x30);  // Extension command 1
  SPI_sendCommand(0xF0);  // Display mode
  SPI_sendData(0x11);     // 0x11 = 4-bit grayscale mode
  SPI_sendCommand(0x5C);  // Write data to DDRAM

  for (int y = 0; y < DISPLAY_HEIGHT; y++) {
    // Calculate gray level (0-15) based on y position
    uint8_t level = (y * 15) / (DISPLAY_HEIGHT - 1);
    
    for (int x = 0; x < DISPLAY_WIDTH; x += 2) {
      // Pack two 4-bit values in one byte (each nibble represents one pixel)
      SPI_sendData((level << 4) | level);
    }
  }
}

// Draw radial grayscale gradient
void drawRadialGradient() {
  SPI_sendCommand(0x30);  // Extension command 1
  SPI_sendCommand(0xF0);  // Display mode
  SPI_sendData(0x11);     // 0x11 = 4-bit grayscale mode
  SPI_sendCommand(0x5C);  // Write data to DDRAM

  int centerX = DISPLAY_WIDTH / 2;
  int centerY = DISPLAY_HEIGHT / 2;
  float maxDist = sqrt(centerX * centerX + centerY * centerY);

  for (int y = 0; y < DISPLAY_HEIGHT; y++) {
    for (int x = 0; x < DISPLAY_WIDTH; x += 2) {
      // Calculate distance from center for two adjacent pixels
      float distance1 = sqrt(pow(x - centerX, 2) + pow(y - centerY, 2));
      float distance2 = sqrt(pow(x+1 - centerX, 2) + pow(y - centerY, 2));
      
      // Calculate gray level (0-15) based on distance
      uint8_t level1 = 15 - min(15, (int)((distance1 * 15) / maxDist));
      uint8_t level2 = 15 - min(15, (int)((distance2 * 15) / maxDist));
      
      // Pack two 4-bit values in one byte (each nibble represents one pixel)
      SPI_sendData((level1 << 4) | level2);
    }
  }
}

// Draw a test pattern with various shapes and gray levels
void drawTestPattern() {
  SPI_sendCommand(0x30);  // Extension command 1
  SPI_sendCommand(0xF0);  // Display mode
  SPI_sendData(0x11);     // 0x11 = 4-bit grayscale mode
  SPI_sendCommand(0x5C);  // Write data to DDRAM

  // Fill with a light gray background
  for (int y = 0; y < DISPLAY_HEIGHT; y++) {
    for (int x = 0; x < DISPLAY_WIDTH; x += 2) { // Process 2 pixels at once
      SPI_sendData(0x22); // Gray level 2 in both nibbles
    }
  }

  // Draw some rectangles with different gray levels
  for (int i = 0; i < 4; i++) {
    int x = 20 + i * 50;
    int y = 20;
    int w = 40;
    int h = 40;
    uint8_t level = i * 4; // Different gray levels (0-15)
    
    // Set row/column addressing mode
    SPI_sendCommand(0x30);  // Extension command 1
    SPI_sendCommand(0x15);  // Column address setting
    SPI_sendData(x);        // Start column
    SPI_sendData(x + w - 1); // End column
    SPI_sendCommand(0x75);  // Page address setting
    SPI_sendData(y / 8);    // Start page
    SPI_sendData((y + h - 1) / 8); // End page
    
    // Write data for the entire rectangle at once
    SPI_sendCommand(0x5C);  // Write data to DDRAM
    
    // Create pixel value with the grayscale level
    uint8_t pixelValue = (level << 4) | level;
    
    // Fill the rectangle
    for (int ry = 0; ry < h; ry++) {
      for (int rx = 0; rx < w; rx += 2) {
        SPI_sendData(pixelValue);
      }
    }
  }

  // Draw some circles with different grayscale levels
  for (int i = 0; i < 3; i++) {
    int x = 120;
    int y = 80;
    int r = 10 + i * 15;
    uint8_t level = 15 - i * 4; // Different gray levels (0-15)
    
    // Create a buffer for the entire display area
    uint8_t circleBuffer[DISPLAY_WIDTH * DISPLAY_HEIGHT / 2];
    memset(circleBuffer, 0, sizeof(circleBuffer));
    
    // Draw circle into buffer (improved algorithm with thicker outline)
    for (int angle = 0; angle < 360; angle += 1) {
      float radians = angle * PI / 180.0;
      int px = x + r * cos(radians);
      int py = y + r * sin(radians);
      
      // Draw 3 pixels thick for visibility
      for (int t = -1; t <= 1; t++) {
        for (int s = -1; s <= 1; s++) {
          int drawX = px + t;
          int drawY = py + s;
          
          if (drawX >= 0 && drawX < DISPLAY_WIDTH && drawY >= 0 && drawY < DISPLAY_HEIGHT) {
            // Calculate buffer position (2 pixels per byte)
            int bufferPos = (drawY * DISPLAY_WIDTH + drawX) / 2;
            
            // Update the correct nibble based on odd/even pixel
            if (drawX % 2 == 0) {
              // Even pixel - high nibble
              circleBuffer[bufferPos] = (circleBuffer[bufferPos] & 0x0F) | (level << 4);
            } else {
              // Odd pixel - low nibble
              circleBuffer[bufferPos] = (circleBuffer[bufferPos] & 0xF0) | level;
            }
          }
        }
      }
    }
    
    // Set addressing for whole display
    SPI_sendCommand(0x30);  // Extension command 1
    SPI_sendCommand(0x15);  // Column address setting
    SPI_sendData(0);        // Start column
    SPI_sendData(DISPLAY_WIDTH - 1); // End column
    SPI_sendCommand(0x75);  // Page address setting
    SPI_sendData(0);        // Start page
    SPI_sendData(DISPLAY_HEIGHT / 8 - 1); // End page
    
    // Write the circle buffer to display
    SPI_sendCommand(0x5C);  // Write data to DDRAM
    for (int j = 0; j < DISPLAY_WIDTH * DISPLAY_HEIGHT / 2; j++) {
      SPI_sendData(circleBuffer[j]);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(500); // Give serial monitor time to connect
  
  Serial.println("\n\nST75256 Grayscale Display Test for XIAO ESP32S3");
  Serial.println("---------------------------------------");
  Serial.println("D1 (GPIO 2): Reset");
  Serial.println("D2 (GPIO 3): Data/Command");
  Serial.println("D3 (GPIO 4): Chip Select");
  Serial.println("D8 (GPIO 7): SPI SCK");
  Serial.println("D10 (GPIO 9): SPI MOSI");
  Serial.println("---------------------------------------");
  
  // Initialize SPI - XIAO ESP32S3 uses default hardware SPI pins
  SPI.begin();
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  
  // Set up control pins
  pinMode(LCD_DC, OUTPUT);
  pinMode(LCD_RESET, OUTPUT);
  pinMode(LCD_CS, OUTPUT);
  
  digitalWrite(LCD_CS, HIGH);
  
  // Initialize the display
  Serial.println("Initializing ST75256 display...");
  Initialize_Display();
  
  // Show initial test patterns
  Serial.println("Running display tests...");
  fillDisplay(0x00);  // Clear screen
  delay(500);
  fillDisplay(0xFF);  // Fill white
  delay(500);
  drawGrayscaleBars();
  
  Serial.println("\nCommands:");
  Serial.println("1: Grayscale Bars");
  Serial.println("2: Horizontal Gradient");
  Serial.println("3: Vertical Gradient");
  Serial.println("4: Radial Gradient");
  Serial.println("5: Test Pattern");
  Serial.println("Auto-cycling every 3 seconds");
}

// Global timer for demo switching
static unsigned long lastChange = 0;

void loop() {
  // Check for serial commands
  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd >= '1' && cmd <= '5') {
      currentMode = (DisplayMode)(cmd - '1');
      String modeName;
      switch (currentMode) {
        case GRAY_BARS: modeName = "Grayscale Bars"; break;
        case GRAY_GRADIENT_H: modeName = "Horizontal Gradient"; break;
        case GRAY_GRADIENT_V: modeName = "Vertical Gradient"; break;
        case GRAY_RADIAL: modeName = "Radial Gradient"; break;
        case GRAY_PATTERN: modeName = "Test Pattern"; break;
      }
      Serial.printf("Switching to mode: %s\n", modeName.c_str());
      
      // Reset timer to avoid immediate auto-switch
      lastChange = millis();
      
      // Execute the requested demo
      executeCurrentDemo();
    }
  }
  
  // Cycle through demo modes every 3 seconds
  if (millis() - lastChange > 3000) {
    currentMode = (DisplayMode)((currentMode + 1) % DEMO_COUNT);
    lastChange = millis();
    
    String modeName;
    switch (currentMode) {
      case GRAY_BARS: modeName = "Grayscale Bars"; break;
      case GRAY_GRADIENT_H: modeName = "Horizontal Gradient"; break;
      case GRAY_GRADIENT_V: modeName = "Vertical Gradient"; break;
      case GRAY_RADIAL: modeName = "Radial Gradient"; break;
      case GRAY_PATTERN: modeName = "Test Pattern"; break;
    }
    Serial.printf("Auto-switching to mode: %s\n", modeName.c_str());
    
    // Execute the current demo
    executeCurrentDemo();
  }
  
  // Small delay to prevent CPU overload
  delay(10);
}

// Helper function to run the current demo
void executeCurrentDemo() {
  switch (currentMode) {
    case GRAY_BARS:
      drawGrayscaleBars();
      break;
    case GRAY_GRADIENT_H:
      drawHorizontalGradient();
      break;
    case GRAY_GRADIENT_V:
      drawVerticalGradient();
      break;
    case GRAY_RADIAL:
      drawRadialGradient();
      break;
    case GRAY_PATTERN:
      drawTestPattern();
      break;
  }
}
