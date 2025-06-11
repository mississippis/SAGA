#ifndef DISPLAY_CONFIG_TEENSY_CFA10110_H
#define DISPLAY_CONFIG_TEENSY_CFA10110_H

// Teensy pin configuration for CFA10110 display
// CFA10110 is a 240x128 display with 4 gray levels

// Display dimensions
#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 128
#define DISPLAY_GRAYSCALE_LEVELS 4

// Teensy 4.1 pin assignments for CFA10110 display based on provided mapping
// Breakout Pin  → Teensy 4.1 Pin
// ------------------------------
// 3  RES        → 24 (PWM capable)
// 5  DC         → 9  (PWM capable)
// 6  CS         → 10 (CS - Chip Select)
// 15 D6 (SCK)   → 13 (SCK - Serial Clock)
// 16 D7 (MOSI)  → 11 (MOSI - Master Out Slave In)
#define DISPLAY_CS_PIN    10  // Chip Select pin
#define DISPLAY_DC_PIN    9   // Data/Command control pin
#define DISPLAY_RST_PIN   24  // Reset pin

// Teensy default hardware SPI pins used
// SCK: pin 13
// MOSI: pin 11 
// MISO: pin 12 (not used for display)

// Teensy 4.1 optimization flags
#define USE_TEENSY_SPI_OPTIMIZED 1  // Use optimized SPI transfers for Teensy 4.1
#define USE_PXP_ACCELERATION 1      // Use Pixel Pipeline acceleration when possible

#endif // DISPLAY_CONFIG_TEENSY_CFA10110_H
