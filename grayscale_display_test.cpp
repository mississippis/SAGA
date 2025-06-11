#include "Arduino.h"
#include "display_config.h"
#include "CFADisplay.h"
#include <SPI.h>

// Global display object
CFADisplay display;

// Define grayscale levels for simulation
#define GRAY_LEVELS 4  // We'll simulate 4 levels (2-bit grayscale)

// Dithering patterns - 2x2 Bayer matrix
const uint8_t bayer2x2[2][2] = {
  {0, 2},
  {3, 1}
};

// Dithering patterns - 4x4 Bayer matrix for smoother gradients
const uint8_t bayer4x4[4][4] = {
  { 0, 8, 2, 10},
  {12, 4, 14, 6 },
  { 3, 11, 1, 9 },
  {15, 7, 13, 5 }
};

// Error diffusion dithering state
int16_t *error_buffer = NULL;

// Test image pattern - a simple grayscale gradient
uint8_t createGradientValue(int x, int y, int width, int height) {
  // Create a horizontal gradient
  return (x * 255) / width;
}

// Draw a grayscale image using 2x2 Bayer dithering
void drawGrayscale2x2Dithering() {
  display.clear();
  display.drawString(0, 10, "2x2 Bayer Dither");
  
  // Draw a gradient rectangle
  for (int y = 20; y < 60; y++) {
    for (int x = 0; x < 128; x++) {
      // Get gradient value (0-255)
      uint8_t value = createGradientValue(x, y, 128, 40);
      
      // Scale to our gray levels (0-3)
      uint8_t level = (value * (GRAY_LEVELS - 1)) / 255;
      
      // Apply dithering
      if (level > bayer2x2[y % 2][x % 2]) {
        display.drawPixel(x, y, 1);
      }
    }
  }
  
  display.update();
}

// Draw a grayscale image using 4x4 Bayer dithering for smoother gradients
void drawGrayscale4x4Dithering() {
  display.clear();
  display.drawString(0, 10, "4x4 Bayer Dither");
  
  // Draw a gradient rectangle
  for (int y = 20; y < 60; y++) {
    for (int x = 0; x < 128; x++) {
      // Get gradient value (0-255)
      uint8_t value = createGradientValue(x, y, 128, 40);
      
      // Scale to our gray levels (0-15 for 4x4 Bayer)
      uint8_t level = (value * 15) / 255;
      
      // Apply dithering
      if (level > bayer4x4[y % 4][x % 4]) {
        display.drawPixel(x, y, 1);
      }
    }
  }
  
  display.update();
}

// Floyd-Steinberg error diffusion dithering
void drawGrayscaleErrorDiffusion() {
  display.clear();
  display.drawString(0, 10, "Error Diffusion");
  
  const int width = 128;
  const int startY = 20;
  const int height = 40;
  
  // Allocate error buffer if not already allocated
  if (error_buffer == NULL) {
    error_buffer = (int16_t*)malloc(width * sizeof(int16_t));
    if (error_buffer == NULL) {
      display.drawString(0, 40, "Memory Error!");
      display.update();
      return;
    }
    // Initialize error buffer
    for (int x = 0; x < width; x++) {
      error_buffer[x] = 0;
    }
  }
  
  // Draw a gradient with error diffusion
  for (int y = startY; y < startY + height; y++) {
    int16_t error = 0;  // Error carried over from left pixel
    
    for (int x = 0; x < width; x++) {
      // Get gradient value (0-255)
      uint8_t value = createGradientValue(x, y, width, height);
      
      // Add error from previous pixels
      int16_t newValue = value + error;
      if (newValue < 0) newValue = 0;
      if (newValue > 255) newValue = 255;
      
      // Determine if pixel should be on or off (threshold at 128)
      if (newValue >= 128) {
        display.drawPixel(x, y, 1);
        error = newValue - 255;  // Distribute error
      } else {
        // Pixel off
        error = newValue;  // Distribute error
      }
      
      // Distribute 3/8 of error to next pixel, 3/8 to pixel below, 1/4 to diagonal
      if (x < width - 1) {
        error_buffer[x+1] += error * 3 / 8;
      }
      error = error * 3 / 8;  // Error for next pixel in this row
    }
  }
  
  display.update();
  
  // Free error buffer
  if (error_buffer != NULL) {
    free(error_buffer);
    error_buffer = NULL;
  }
}

// Pattern dithering - using predefined patterns for each gray level
void drawGrayscalePatternDithering() {
  display.clear();
  display.drawString(0, 10, "Pattern Dither");
  
  // Define 4 patterns for grayscale simulation
  const uint8_t patterns[4][8] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},  // 0% - all pixels off
    {0x88, 0x22, 0x88, 0x22, 0x88, 0x22, 0x88, 0x22},  // 25% - sparse pattern
    {0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55},  // 50% - checkerboard
    {0xEE, 0xBB, 0xEE, 0xBB, 0xEE, 0xBB, 0xEE, 0xBB}   // 75% - inverse sparse
  };
  
  // Draw bands of different gray levels
  for (int level = 0; level < 4; level++) {
    int startY = 20 + level * 10;
    
    for (int y = 0; y < 8; y++) {
      for (int x = 0; x < 128; x++) {
        // Get the appropriate bit from the pattern
        if (patterns[level][y] & (1 << (x % 8))) {
          display.drawPixel(x, startY + y, 1);
        }
      }
    }
  }
  
  display.update();
}

// Temporal dithering simulation (would be visible as flicker in real display)
void drawTemporalDithering() {
  static uint8_t phase = 0;
  
  display.clear();
  display.drawString(0, 10, "Temporal Dither");
  
  // Draw gradient with temporal dithering
  for (int y = 20; y < 60; y++) {
    for (int x = 0; x < 128; x++) {
      // Get gradient value (0-255)
      uint8_t value = createGradientValue(x, y, 128, 40);
      
      // Temporal threshold based on current phase
      uint8_t threshold;
      switch (phase) {
        case 0: threshold = 64; break;
        case 1: threshold = 128; break;
        case 2: threshold = 192; break;
        case 3: threshold = 255; break;
        default: threshold = 128;
      }
      
      if (value >= threshold) {
        display.drawPixel(x, y, 1);
      }
    }
  }
  
  display.update();
  
  // Update phase for next frame
  phase = (phase + 1) % 4;
}

void setup() {
  Serial.begin(115200);
  delay(1000); // Wait for serial to connect
  Serial.println("\nXiao Grayscale Display Test Starting...");
  
  // Initialize pin modes for display
  pinMode(DISPLAY_RST, OUTPUT);
  pinMode(DISPLAY_DC, OUTPUT);
  pinMode(DISPLAY_CS, OUTPUT);
  Serial.println("Display pins initialized");
  
  // Initialize display
  if (!display.begin()) {
    Serial.println("Failed to initialize display");
    while (1);
  }
  Serial.println("Display initialized successfully");
  
  // Welcome message
  display.clear();
  display.drawString(0, 20, "Grayscale Test");
  display.drawString(0, 40, "Press button to");
  display.drawString(0, 50, "change demo");
  display.update();
  delay(2000);
}

// Demo mode tracking
int demoMode = 0;
unsigned long lastModeChange = 0;

void loop() {
  // Check for serial commands
  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd >= '1' && cmd <= '5') {
      demoMode = cmd - '1';
      Serial.printf("Switching to demo mode %d\n", demoMode);
    }
  }
  
  // Auto cycle through demos every 3 seconds
  if (millis() - lastModeChange > 3000) {
    demoMode = (demoMode + 1) % 5;
    lastModeChange = millis();
    Serial.printf("Auto-switching to demo mode %d\n", demoMode);
  }
  
  // Run the selected demo
  switch (demoMode) {
    case 0:
      drawGrayscale2x2Dithering();
      break;
    case 1:
      drawGrayscale4x4Dithering();
      break;
    case 2:
      drawGrayscaleErrorDiffusion();
      break;
    case 3:
      drawGrayscalePatternDithering();
      break;
    case 4:
      drawTemporalDithering();
      break;
  }
  
  // Small delay for temporal dithering and to prevent CPU overload
  delay(100);
}
