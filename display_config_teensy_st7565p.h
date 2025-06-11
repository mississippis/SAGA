#ifndef DISPLAY_CONFIG_TEENSY_ST7565P_H
#define DISPLAY_CONFIG_TEENSY_ST7565P_H

// Teensy pin configuration for ST7565P display
// ST7565P is a 128x64 monochrome display

// Display dimensions
#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64
#define DISPLAY_GRAYSCALE_LEVELS 2  // Monochrome

// Teensy 4.1 pin assignments for ST7565P display based on provided mapping
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

#endif // DISPLAY_CONFIG_TEENSY_ST7565P_H
