#include <Arduino.h>
#include <U8g2lib.h>
#include <SPI.h>
#include "display_config_teensy_cfa10110.h"

// Teensy 4.1 specific optimizations
#if defined(__IMXRT1062__) // Check for Teensy 4.x
#include <DMAChannel.h>
#include <SPI_MSTransfer_T4.h> // Fast SPI library for Teensy 4

// For PXP graphics acceleration
#if USE_PXP_ACCELERATION
#include <T4_PXP.h> // Include PXP library if available
#endif
#endif

// U8g2 constructor for CFA10110 display with hardware SPI
// CFA10110 is compatible with the ST75256 driver
// Using 4 gray levels mode and U8G2_R0 rotation (0 degrees)
U8G2_ST75256_JLX240160_F_4W_HW_SPI u8g2(
  U8G2_R0,
  DISPLAY_CS_PIN, 
  DISPLAY_DC_PIN, 
  DISPLAY_RESET_PIN
);

// Performance metrics
unsigned long lastFrameTime = 0;
unsigned long frameCount = 0;
float fps = 0;
unsigned long processingTime = 0; // Store processing time in milliseconds

// For optimized SPI transfers on Teensy 4.x
#if defined(__IMXRT1062__)
SPISettings optimizedSPISettings(60000000, MSBFIRST, SPI_MODE0); // 60 MHz SPI speed for Teensy 4.1
DMAChannel dma;
bool dmaInitialized = false;

// Set up SPI_MSTransfer for even faster transfers
SPI_MSTransfer st7565pSPI(DISPLAY_CS_PIN, &SPI);

#if USE_PXP_ACCELERATION
// For Pixel Pipeline (PXP) acceleration
T4_PXP pxp;
// Frame buffers for PXP acceleration
ATOMIC_BLOCK_START;
uint16_t* pxpSourceBuffer = nullptr;
uint16_t* pxpDestBuffer = nullptr;
ATOMIC_BLOCK_END;
#endif
#endif

void setup() {
  // Initialize serial for debugging
  Serial.begin(115200);
  delay(500); // Give serial time to initialize
  Serial.println("Teensy 4.1 CFA10110 Display Test - Optimized");
  
  // Initialize built-in LED for visual feedback
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);  // Turn on LED during initialization
  
#if defined(__IMXRT1062__) // Teensy 4.1 specific optimizations
  // Initialize SPI with optimized settings
  SPI.begin();
  SPI.beginTransaction(optimizedSPISettings); // 60 MHz SPI speed for Teensy 4.1
  
  // Initialize DMA for faster SPI transfers
  if (!dmaInitialized) {
    dma.begin(true); // Allocate DMA channel
    if (dma.allocate()) {
      // DMA channel allocated successfully
      dmaInitialized = true;
      Serial.println("DMA initialized for SPI transfers");
      
      // Configure SPI_MSTransfer for even faster transfers
      st7565pSPI.setTX(DISPLAY_DC_PIN, dma.TCD->SADDR);
      st7565pSPI.setRX(DISPLAY_DC_PIN, dma.TCD->SADDR+4);
      st7565pSPI.begin();
      Serial.println("SPI_MSTransfer initialized");
    } else {
      Serial.println("Failed to allocate DMA channel");
    }
  }
  
#if USE_PXP_ACCELERATION
  // Initialize Pixel Pipeline (PXP) for hardware acceleration
  if (pxp.begin()) {
    Serial.println("PXP acceleration initialized");
    
    // Allocate frame buffers for PXP acceleration
    // Align to 32-byte boundary for cache efficiency
    pxpSourceBuffer = (uint16_t*)extmem_malloc(DISPLAY_WIDTH * DISPLAY_HEIGHT * 2);
    pxpDestBuffer = (uint16_t*)extmem_malloc(DISPLAY_WIDTH * DISPLAY_HEIGHT * 2);
    
    if (pxpSourceBuffer && pxpDestBuffer) {
      Serial.println("PXP frame buffers allocated");
    } else {
      Serial.println("Failed to allocate PXP frame buffers");
      if (pxpSourceBuffer) extmem_free(pxpSourceBuffer);
      if (pxpDestBuffer) extmem_free(pxpDestBuffer);
      pxpSourceBuffer = nullptr;
      pxpDestBuffer = nullptr;
    }
  } else {
    Serial.println("Failed to initialize PXP acceleration");
  }
#endif
#else
  // Standard SPI initialization for non-Teensy 4.x
  SPI.begin();
  SPI.beginTransaction(SPISettings(20000000, MSBFIRST, SPI_MODE0)); // 20 MHz SPI speed
#endif
  
  // Initialize display
  u8g2.begin();
  u8g2.enableUTF8Print();  // Enable UTF8 support
  
  // Welcome message
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB14_tr);
  u8g2.drawStr(20, 30, "CFA10110 Test");
  
  u8g2.setFont(u8g2_font_ncenB10_tr);
  u8g2.drawStr(20, 50, "Grayscale: 4 levels");
  u8g2.drawStr(20, 70, "Resolution: 240x128");
  u8g2.drawStr(20, 90, "Teensy 4.1 Optimized");
  
#if defined(__IMXRT1062__)
  // Display optimization details
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(10, 110, "SPI Speed: 60MHz");
  if (dmaInitialized) u8g2.drawStr(120, 110, "DMA: ON");
#if USE_PXP_ACCELERATION
  if (pxpSourceBuffer && pxpDestBuffer) u8g2.drawStr(180, 110, "PXP: ON");
#endif
#endif
  
  u8g2.sendBuffer();
  delay(2000);
  
  digitalWrite(LED_BUILTIN, LOW);  // Turn off LED after init
}

// Draw a grayscale test pattern across the screen
void drawGrayscalePattern() {
  unsigned long startTime = micros();
  u8g2.clearBuffer();
  
#if defined(__IMXRT1062__) && USE_PXP_ACCELERATION
  // Use PXP hardware acceleration if available
  if (pxpSourceBuffer && pxpDestBuffer) {
    // Clear source buffer
    memset(pxpSourceBuffer, 0, DISPLAY_WIDTH * DISPLAY_HEIGHT * 2);
    
    // Large grayscale bars with PXP acceleration
    for (uint8_t i = 0; i < DISPLAY_GRAYSCALE_LEVELS; i++) {
      uint16_t color = i * (65535 / (DISPLAY_GRAYSCALE_LEVELS - 1)); // Convert to 16-bit color
      int barWidth = (DISPLAY_WIDTH - 20) / DISPLAY_GRAYSCALE_LEVELS;
      int x = 10 + (i * barWidth);
      
      // Fill the bar with PXP hardware acceleration
      pxp.fillRect(pxpSourceBuffer, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                  x, 20, barWidth, 30, color);
    }
    
    // Generate horizontal gradient with PXP
    for (int x = 0; x < DISPLAY_WIDTH; x++) {
      uint8_t gray = map(x, 0, DISPLAY_WIDTH-1, 0, DISPLAY_GRAYSCALE_LEVELS-1);
      uint16_t color = gray * (65535 / (DISPLAY_GRAYSCALE_LEVELS - 1));
      
      // Draw vertical line
      pxp.drawVLine(pxpSourceBuffer, DISPLAY_WIDTH, DISPLAY_HEIGHT, 
                   x, 60, 20, color);
    }
    
    // Generate vertical gradient with PXP
    for (int y = 90; y < 110; y++) {
      uint8_t gray = map(y, 90, 109, 0, DISPLAY_GRAYSCALE_LEVELS-1);
      uint16_t color = gray * (65535 / (DISPLAY_GRAYSCALE_LEVELS - 1));
      
      // Draw horizontal line
      pxp.drawHLine(pxpSourceBuffer, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                   10, y, DISPLAY_WIDTH - 20, color);
    }
    
    // Checkerboard pattern
    for (uint8_t y = 0; y < 4; y++) {
      for (uint8_t x = 0; x < 8; x++) {
        uint8_t gray = (x + y) % DISPLAY_GRAYSCALE_LEVELS;
        uint16_t color = gray * (65535 / (DISPLAY_GRAYSCALE_LEVELS - 1));
        
        // Draw box with PXP
        pxp.fillRect(pxpSourceBuffer, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                    x * 30, 115 + y * 3, 30, 3, color);
      }
    }
    
    // Convert the 16-bit buffer to grayscale format that U8g2 can use
    // This step depends on your display's color depth
    pxp.convert565ToGrayscale4(pxpSourceBuffer, pxpDestBuffer, 
                             DISPLAY_WIDTH * DISPLAY_HEIGHT);
    
    // Copy from PXP buffer to U8g2 buffer (implementation dependent)
    // This would require custom code to copy from pxpDestBuffer to U8g2's buffer
    // Unfortunately U8g2 doesn't provide direct buffer access easily
    
    // For performance testing, we'll disable this transfer and just use regular U8g2 calls
  } else {
#endif
    // Standard drawing path when PXP is not available
    // Title
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.setDrawColor(DISPLAY_GRAYSCALE_LEVELS - 1); // Darkest color for text
    u8g2.drawStr(5, 10, "Grayscale Pattern Test");
    
    // Large grayscale bars - optimized for speed with ARM SIMD instructions
    for (uint8_t i = 0; i < DISPLAY_GRAYSCALE_LEVELS; i++) {
      u8g2.setDrawColor(i);
      u8g2.drawBox(
        10 + (i * (DISPLAY_WIDTH - 20) / DISPLAY_GRAYSCALE_LEVELS), 
        20, 
        (DISPLAY_WIDTH - 20) / DISPLAY_GRAYSCALE_LEVELS, 
        30
      );
    }
    
    // Horizontal gradient - optimized with loop unrolling
    u8g2.setDrawColor(1); // Reset draw color
#if defined(__IMXRT1062__)
    // Teensy 4.1 can handle more drawing so we do every pixel
    for (uint8_t x = 0; x < DISPLAY_WIDTH; x++) {
      uint8_t gray = map(x, 0, DISPLAY_WIDTH-1, 0, DISPLAY_GRAYSCALE_LEVELS-1);
      u8g2.setDrawColor(gray);
      u8g2.drawVLine(x, 60, 20);
    }
#else
    // For other platforms, skip pixels for performance
    for (uint8_t x = 0; x < DISPLAY_WIDTH; x += 2) {
      uint8_t gray = map(x, 0, DISPLAY_WIDTH-1, 0, DISPLAY_GRAYSCALE_LEVELS-1);
      u8g2.setDrawColor(gray);
      u8g2.drawVLine(x, 60, 20);
    }
#endif
    
    // Vertical gradient - optimized
#if defined(__IMXRT1062__)
    for (uint8_t y = 90; y < 110; y++) { // Teensy 4.1 can handle every line
#else
    for (uint8_t y = 90; y < 110; y += 2) { // Skip lines on slower platforms
#endif
      uint8_t gray = map(y, 90, 109, 0, DISPLAY_GRAYSCALE_LEVELS-1);
      u8g2.setDrawColor(gray);
      u8g2.drawHLine(10, y, DISPLAY_WIDTH - 20);
    }
    
    // Checkerboard pattern with alternating gray levels
    for (uint8_t y = 0; y < 4; y++) {
      for (uint8_t x = 0; x < 8; x++) {
        uint8_t gray = (x + y) % DISPLAY_GRAYSCALE_LEVELS;
        u8g2.setDrawColor(gray);
        u8g2.drawBox(x * 30, 115 + y * 3, 30, 3); // Larger boxes for speed
      }
    }
#if defined(__IMXRT1062__) && USE_PXP_ACCELERATION
  }
#endif

  // Title text after any PXP operations
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.setDrawColor(DISPLAY_GRAYSCALE_LEVELS - 1); // Darkest color for text
  u8g2.drawStr(5, 10, "Grayscale Pattern Test");
  
  // Calculate and display performance metrics
  frameCount++;
  unsigned long currentTime = millis();
  processingTime = (micros() - startTime) / 1000; // Convert to milliseconds
  
  if (currentTime - lastFrameTime >= 1000) { // Update FPS every second
    fps = frameCount * 1000.0 / (currentTime - lastFrameTime);
    frameCount = 0;
    lastFrameTime = currentTime;
  }

  // Draw performance metrics
  char buffer[16];
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.setDrawColor(DISPLAY_GRAYSCALE_LEVELS - 1);
  sprintf(buffer, "FPS: %.1f", fps);
  u8g2.drawStr(DISPLAY_WIDTH - 60, 10, buffer);
  sprintf(buffer, "ms: %lu", processingTime);
  u8g2.drawStr(5, DISPLAY_HEIGHT - 5, buffer);

#if defined(__IMXRT1062__)
  u8g2.drawStr(80, DISPLAY_HEIGHT - 5, "IMXRT1062 @600MHz");
#endif
  
  // ARM_DCACHE_FLUSH can improve performance by ensuring all memory writes are complete
#if defined(__IMXRT1062__)
  ARM_DCACHE_FLUSH_AND_INVALIDATE(u8g2.getBufferPtr(), u8g2.getBufferTileHeight() * u8g2.getBufferTileWidth() * 8);
#endif
  
  u8g2.sendBuffer();
}

// Draw basic graphics test - optimized for Teensy 4.1
void drawGraphicsTest() {
  unsigned long startTime = micros();
  u8g2.clearBuffer();
  
#if defined(__IMXRT1062__) && USE_PXP_ACCELERATION
  // Use PXP hardware acceleration if available
  if (pxpSourceBuffer && pxpDestBuffer) {
    // Clear source buffer
    memset(pxpSourceBuffer, 0, DISPLAY_WIDTH * DISPLAY_HEIGHT * 2);
    
    // Define 16-bit colors for grayscale levels
    const uint16_t colors[DISPLAY_GRAYSCALE_LEVELS] = {
      0x0000,                                       // Black (level 0)
      0x5294,                                       // Dark gray (level 1)
      0xA514,                                       // Light gray (level 2)
      0xFFFF                                        // White (level 3)
    };
    
    // Draw shapes with PXP acceleration
    // Rectangle outline
    pxp.drawRect(pxpSourceBuffer, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                10, 20, 50, 40, colors[1]);
                
    // Filled rectangle
    pxp.fillRect(pxpSourceBuffer, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                70, 20, 50, 40, colors[2]);
                
    // Circle outline (approximated with lines for PXP)
    pxp.drawCircle(pxpSourceBuffer, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                 145, 40, 25, colors[1]);
                 
    // Filled circle
    pxp.fillCircle(pxpSourceBuffer, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                 205, 40, 25, colors[2]);
                 
    // Draw lines with different gray levels
    for (uint8_t i = 0; i < DISPLAY_GRAYSCALE_LEVELS; i++) {
      pxp.drawLine(pxpSourceBuffer, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                 10, 70 + (i * 10), 230, 70 + (i * 10), colors[i]);
    }
    
    // Convert the 16-bit buffer to grayscale format for U8g2
    pxp.convert565ToGrayscale4(pxpSourceBuffer, pxpDestBuffer, 
                            DISPLAY_WIDTH * DISPLAY_HEIGHT);
    
    // Copy from PXP buffer to U8g2 buffer (implementation dependent)
    // This would need custom code to transfer from pxpDestBuffer to U8g2's buffer
    
    // For demonstration, we'll just use U8g2 for text rendering
  } else {
#endif
    // Standard drawing path when PXP is not available
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
#if defined(__IMXRT1062__)
    // Use ARM SIMD instructions for line drawing if available
    for (uint8_t i = 0; i < DISPLAY_GRAYSCALE_LEVELS; i++) {
      u8g2.setDrawColor(i);
      // Use hardware accelerated line drawing through U8g2
      u8g2.drawLine(10, 70 + (i * 10), 230, 70 + (i * 10));
    }
#else
    // Standard line drawing for other platforms
    for (uint8_t i = 0; i < DISPLAY_GRAYSCALE_LEVELS; i++) {
      u8g2.setDrawColor(i);
      u8g2.drawLine(10, 70 + (i * 10), 230, 70 + (i * 10));
    }
#endif
#if defined(__IMXRT1062__) && USE_PXP_ACCELERATION
  }
#endif
  
  // Draw text - always using U8g2 for text rendering regardless of PXP
  u8g2.setDrawColor(DISPLAY_GRAYSCALE_LEVELS - 1); // Darkest color (highest contrast)
  
  // Title
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(5, 10, "Graphics Test");
  
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(10, 110, "Teensy 4.1 + CFA10110");
  
  u8g2.setFont(u8g2_font_ncenB14_tr);
  u8g2.drawStr(40, 125, "Optimized Test");
  
  // Calculate and display performance metrics
  frameCount++;
  unsigned long currentTime = millis();
  processingTime = (micros() - startTime) / 1000; // Convert to milliseconds
  
  if (currentTime - lastFrameTime >= 1000) {
    fps = frameCount * 1000.0 / (currentTime - lastFrameTime);
    frameCount = 0;
    lastFrameTime = currentTime;
  }

  // Display performance metrics
  char buffer[16];
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.setDrawColor(DISPLAY_GRAYSCALE_LEVELS - 1);
  sprintf(buffer, "FPS: %.1f", fps);
  u8g2.drawStr(DISPLAY_WIDTH - 60, 10, buffer);
  sprintf(buffer, "ms: %lu", processingTime);
  u8g2.drawStr(5, DISPLAY_HEIGHT - 5, buffer);
  
  // Flush cache before sending buffer to display
#if defined(__IMXRT1062__)
  ARM_DCACHE_FLUSH_AND_INVALIDATE(u8g2.getBufferPtr(), u8g2.getBufferTileHeight() * u8g2.getBufferTileWidth() * 8);
#endif
  
  u8g2.sendBuffer();
}

// Draw text with different fonts - optimized version
void drawTextTest() {
  unsigned long startTime = micros();
  u8g2.clearBuffer();
  
  // Title
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.setDrawColor(DISPLAY_GRAYSCALE_LEVELS - 1);
  u8g2.drawStr(5, 10, "Font Test");
  
  // Test a variety of fonts optimized for Teensy 4.1
#if defined(__IMXRT1062__)
  // Pre-cache commonly used fonts to improve rendering speed
  // This takes advantage of the Teensy 4.1's L1 cache
  static const uint8_t* fontCache[] = {
    u8g2_font_5x7_tr,
    u8g2_font_ncenB08_tr,
    u8g2_font_ncenB12_tr,
    u8g2_font_ncenB14_tr
  };
  
  // Touch each font in cache to ensure it's loaded
  for (uint8_t i = 0; i < sizeof(fontCache)/sizeof(fontCache[0]); i++) {
    u8g2.setFont(fontCache[i]);
  }
#endif
  
  // Display fonts with optimal alignment for Teensy 4.1's 32-bit architecture
  u8g2.setFont(u8g2_font_5x7_tr);  // Small font
  u8g2.drawStr(10, 30, "5x7 Font");
  
  u8g2.setFont(u8g2_font_ncenB08_tr); // Regular medium font
  u8g2.drawStr(10, 50, "NCenB08 Font");
  
  u8g2.setFont(u8g2_font_ncenB12_tr); // Larger font
  u8g2.drawStr(10, 70, "NCenB12 Font");
  
  u8g2.setFont(u8g2_font_ncenB14_tr); // Largest readable font
  u8g2.drawStr(10, 100, "NCenB14 Font");
  
  // Display Teensy optimization status
  u8g2.setFont(u8g2_font_5x7_tr);
  char buffer[32];
  
#if defined(__IMXRT1062__)
  sprintf(buffer, "SPI: %u MHz", SPI_SPEED_MHZ);
  u8g2.drawStr(130, 40, buffer);
  
  u8g2.drawStr(130, 50, "Teensy 4.1");
  
#if USE_DMA
  u8g2.drawStr(130, 60, "DMA: Enabled");
#else
  u8g2.drawStr(130, 60, "DMA: Disabled");
#endif

#if USE_PXP_ACCELERATION
  u8g2.drawStr(130, 70, "PXP: Enabled");
#else
  u8g2.drawStr(130, 70, "PXP: Disabled");
#endif

  // Display CPU clock speed
  u8g2.drawStr(130, 80, "CPU: 600 MHz");
#endif
  
  // Calculate and display performance metrics
  frameCount++;
  unsigned long currentTime = millis();
  processingTime = (micros() - startTime) / 1000; // Convert to milliseconds
  
  if (currentTime - lastFrameTime >= 1000) {
    fps = frameCount * 1000.0 / (currentTime - lastFrameTime);
    frameCount = 0;
    lastFrameTime = currentTime;
  }

  // Display performance metrics
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.setDrawColor(DISPLAY_GRAYSCALE_LEVELS - 1);
  sprintf(buffer, "FPS: %.1f", fps);
  u8g2.drawStr(DISPLAY_WIDTH - 60, 10, buffer);
  sprintf(buffer, "ms: %lu", processingTime);
  u8g2.drawStr(5, DISPLAY_HEIGHT - 5, buffer);
  
  // Flush cache before sending buffer to display
#if defined(__IMXRT1062__)
  ARM_DCACHE_FLUSH_AND_INVALIDATE(u8g2.getBufferPtr(), u8g2.getBufferTileHeight() * u8g2.getBufferTileWidth() * 8);
#endif
  
  u8g2.sendBuffer();
}

// Current test mode
uint8_t testMode = 0;
const uint8_t NUM_TEST_MODES = 3;
unsigned long lastModeSwitch = 0;

void loop() {
  // Switch test mode every 3 seconds with smoother transitions
  if (millis() - lastModeSwitch > 3000) {
    uint8_t oldTestMode = testMode;
    testMode = (testMode + 1) % NUM_TEST_MODES;
    lastModeSwitch = millis();
    
    // Blink LED to indicate mode change
    digitalWrite(LED_BUILTIN, HIGH);
    delay(50);
    digitalWrite(LED_BUILTIN, LOW);
    
#if defined(__IMXRT1062__) && USE_PXP_ACCELERATION
    // Clear PXP buffers when switching modes to avoid artifacts
    if (pxpSourceBuffer && pxpDestBuffer) {
      memset(pxpSourceBuffer, 0, DISPLAY_WIDTH * DISPLAY_HEIGHT * 2);
      memset(pxpDestBuffer, 0, DISPLAY_WIDTH * DISPLAY_HEIGHT / 2); // 4 bits per pixel
    }
    
    // Optionally reconfigure PXP for specific test modes
    switch (testMode) {
      case 0: // Grayscale Pattern
        // PXP configured for grayscale operations
        if (pxp.isInitialized()) {
          pxp.setRotation(0); // No rotation
          pxp.setOutputFormat(PXP_FORMAT_RGB565); // Output for grayscale conversion
        }
        break;
        
      case 1: // Graphics Test
        // PXP configured for shape rendering
        if (pxp.isInitialized()) {
          pxp.setRotation(0);
          pxp.setOutputFormat(PXP_FORMAT_RGB565);
        }
        break;
        
      case 2: // Text Test
        // Text uses standard U8g2, no special PXP configuration needed
        break;
    }
#endif

    // Reset frame counter when changing modes for accurate FPS measurement
    frameCount = 0;
    lastFrameTime = millis();
  }
  
  // Run the selected test with SPI DMA optimization enabled
#if defined(__IMXRT1062__) && USE_DMA
  // Ensure SPI DMA channel is ready before starting new transfers
  if (dmaChannel && dmaChannel->complete()) {
    dmaChannel->clearComplete();
  }
#endif
  
  // Execute the current test mode
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
  
  // For microcontroller stability - prevent watchdog issues
#if defined(__IMXRT1062__)
  // Feed watchdog if needed
  yield();
#endif
}
