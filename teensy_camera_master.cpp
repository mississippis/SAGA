#include <Arduino.h>
#include <SPI.h>
#include <U8g2lib.h>
#include <DMAChannel.h>

// Select which display type to use
// Comment out one of these to select the other
#define USE_ST7565P
//#define USE_CFA10110

// Include appropriate display config based on selection
#if defined(USE_ST7565P)
  #include "display_config_teensy_st7565p.h"
  #define DISPLAY_WIDTH 128
  #define DISPLAY_HEIGHT 64
  #define DISPLAY_GRAYSCALE_LEVELS 2
  #define DISPLAY_TYPE_ID 1
#elif defined(USE_CFA10110)
  #include "display_config_teensy_cfa10110.h"
  #define DISPLAY_WIDTH 240
  #define DISPLAY_HEIGHT 128
  #define DISPLAY_GRAYSCALE_LEVELS 4
  #define DISPLAY_TYPE_ID 2
#else
  #error "No display selected! Uncomment either USE_ST7565P or USE_CFA10110"
#endif

// Enable Teensy 4.1 specific optimizations
#if defined(__IMXRT1062__)
  #define USE_DMA 1
  #define SPI_SPEED_MHZ 30
  // Enable PXP hardware acceleration for CFA10110 (grayscale display)
  #if defined(USE_CFA10110)
    #define USE_PXP_ACCELERATION 1
    #include <T4_PXP.h>
  #endif
#endif

// Pin definitions for Teensy 4.1 to XIAO ESP32S3 communication
#define SPI_MISO_PIN 12      // Teensy MISO to XIAO MISO
#define SPI_MOSI_PIN 11      // Teensy MOSI to XIAO MOSI
#define SPI_SCLK_PIN 13      // Teensy SCK to XIAO SCK
#define XIAO_SS_PIN  10      // Teensy SS to XIAO SS
#define XIAO_READY_PIN 15    // XIAO signals when new data is ready
#define BUSY_LED_PIN 14      // LED to indicate Teensy is busy

// Communication protocol commands
#define CMD_CAPTURE_IMAGE     0x01
#define CMD_GET_FRAME         0x02
#define CMD_SET_RESOLUTION    0x03
#define CMD_SET_QUALITY       0x04
#define CMD_SET_CONTRAST      0x05
#define CMD_SET_BRIGHTNESS    0x06
#define CMD_SET_TARGET        0x07

// Image capture settings
#define FRAMESIZE_QVGA 8     // 320x240

// Display-specific setup
#if defined(USE_ST7565P)
  U8G2_ST7565_EA_DOGM128_F_4W_HW_SPI u8g2(
    U8G2_R0,
    DISPLAY_CS_PIN,
    DISPLAY_DC_PIN,
    DISPLAY_RST_PIN
  );
#elif defined(USE_CFA10110)
  U8G2_ST75256_JLX240160_F_4W_HW_SPI u8g2(
    U8G2_R0,
    DISPLAY_CS_PIN,
    DISPLAY_DC_PIN,
    DISPLAY_RST_PIN
  );
#endif

// Global variables
uint8_t *imageBuffer = NULL;
uint8_t *displayBuffer = NULL;
size_t imageSize = 0;
unsigned long lastFrameTime = 0;
unsigned long frameCount = 0;
float fps = 0;
unsigned long processingTime = 0;

// For DMA transfers
#if defined(__IMXRT1062__) && USE_DMA
  DMAChannel dmaChannel;
  volatile bool dmaTransferComplete = false;
#endif

// For PXP acceleration
#if defined(__IMXRT1062__) && USE_PXP_ACCELERATION
  T4_PXP pxp;
  uint16_t *pxpSourceBuffer = NULL;
  uint8_t *pxpDestBuffer = NULL;
#endif

// Function prototypes
void setupSPI();
void setupDisplay();
void sendCommand(uint8_t cmd, uint8_t *data = NULL, size_t len = 0);
bool captureAndReceiveImage();
void processAndDisplayImage();
void displayFPS();

void setup() {
  // Initialize serial for debugging
  Serial.begin(115200);
  delay(500); // Give serial port time to connect
  Serial.println("Teensy 4.1 Camera Master");
  
  // Initialize pins
  pinMode(BUSY_LED_PIN, OUTPUT);
  digitalWrite(BUSY_LED_PIN, HIGH);  // Busy during setup
  
  pinMode(XIAO_SS_PIN, OUTPUT);
  digitalWrite(XIAO_SS_PIN, HIGH);   // Deselect XIAO initially
  
  pinMode(XIAO_READY_PIN, INPUT);    // XIAO signals when data is ready
  
  // Setup SPI for communication with XIAO
  setupSPI();
  
  // Setup display
  setupDisplay();
  
  // Allocate memory for image buffer
  #if defined(USE_ST7565P)
    imageSize = (DISPLAY_WIDTH * DISPLAY_HEIGHT) / 8;  // 1-bit monochrome
  #elif defined(USE_CFA10110)
    imageSize = (DISPLAY_WIDTH * DISPLAY_HEIGHT) / 4;  // 2-bit grayscale (4 levels)
  #endif
  
  imageBuffer = (uint8_t *)malloc(imageSize);
  if (!imageBuffer) {
    Serial.println("Failed to allocate image buffer");
    while (1);
  }
  
  displayBuffer = (uint8_t *)malloc(u8g2.getBufferSize());
  if (!displayBuffer) {
    Serial.println("Failed to allocate display buffer");
    while (1);
  }
  
  // Initialize PXP hardware acceleration if available
  #if defined(__IMXRT1062__) && USE_PXP_ACCELERATION
    // Initialize PXP
    pxp.begin();
    
    // Allocate PXP buffers for image processing
    pxpSourceBuffer = (uint16_t *)malloc(DISPLAY_WIDTH * DISPLAY_HEIGHT * 2);  // RGB565 source
    pxpDestBuffer = (uint8_t *)malloc(DISPLAY_WIDTH * DISPLAY_HEIGHT / 4);     // 2-bit grayscale dest
    
    if (!pxpSourceBuffer || !pxpDestBuffer) {
      Serial.println("Failed to allocate PXP buffers");
      if (pxpSourceBuffer) free(pxpSourceBuffer);
      if (pxpDestBuffer) free(pxpDestBuffer);
      pxpSourceBuffer = NULL;
      pxpDestBuffer = NULL;
    } else {
      Serial.println("PXP hardware acceleration enabled");
    }
  #endif
  
  // Clear display and show startup message
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB10_tr);
  u8g2.drawStr(0, 20, "Teensy 4.1");
  u8g2.drawStr(0, 40, "Camera Master");
  u8g2.sendBuffer();
  delay(1000);
  
  // Tell XIAO which display type we're using
  uint8_t displayTypeData = DISPLAY_TYPE_ID;
  sendCommand(CMD_SET_TARGET, &displayTypeData, 1);
  
  // Set camera to QVGA resolution
  uint8_t resolutionData = FRAMESIZE_QVGA;
  sendCommand(CMD_SET_RESOLUTION, &resolutionData, 1);
  
  Serial.println("Setup complete");
  digitalWrite(BUSY_LED_PIN, LOW);  // Setup complete
}

void loop() {
  unsigned long startTime = micros();
  
  // Check if XIAO has signaled that new data is ready
  if (digitalRead(XIAO_READY_PIN) == HIGH || (millis() - lastFrameTime > 100)) {
    digitalWrite(BUSY_LED_PIN, HIGH);  // Indicate we're busy
    
    // Capture and receive image data from XIAO
    if (captureAndReceiveImage()) {
      // Process and display the image
      processAndDisplayImage();
      
      // Display FPS
      displayFPS();
      
      // Update FPS calculation
      frameCount++;
      unsigned long currentTime = millis();
      if (currentTime - lastFrameTime >= 1000) {
        fps = frameCount * 1000.0 / (currentTime - lastFrameTime);
        frameCount = 0;
        lastFrameTime = currentTime;
        Serial.printf("FPS: %.1f\n", fps);
      }
    }
    
    digitalWrite(BUSY_LED_PIN, LOW);  // No longer busy
  }
  
  // Calculate processing time
  processingTime = (micros() - startTime) / 1000;  // Convert to milliseconds
}

void setupSPI() {
  // Initialize SPI for communication with XIAO
  SPI.begin();
  
  #if defined(__IMXRT1062__)
    // Optimize SPI for Teensy 4.1
    SPI.setSCK(SPI_SCLK_PIN);
    SPI.setMOSI(SPI_MOSI_PIN);
    SPI.setMISO(SPI_MISO_PIN);
    
    // Setup DMA channel for SPI transfers if enabled
    #if USE_DMA
      if (dmaChannel.begin()) {
        dmaChannel.destinationBuffer(NULL, 0);
        dmaChannel.triggerAtHardwareEvent(DMAMUX_SOURCE_SPI0_RX);
        dmaChannel.attachInterrupt([]() {
          dmaTransferComplete = true;
        });
        Serial.println("DMA initialized for SPI transfers");
      } else {
        Serial.println("Failed to initialize DMA channel");
      }
    #endif
  #endif
}

void setupDisplay() {
  // Initialize display based on type
  u8g2.begin();
  
  // Configure display-specific settings
  #if defined(USE_ST7565P)
    u8g2.setContrast(128);
  #elif defined(USE_CFA10110)
    // Additional config for ST75256 if needed
  #endif
  
  // Clear display
  u8g2.clearBuffer();
  u8g2.sendBuffer();
}

void sendCommand(uint8_t cmd, uint8_t *data, size_t len) {
  SPISettings spiSettings(SPI_SPEED_MHZ * 1000000, MSBFIRST, SPI_MODE0);
  
  SPI.beginTransaction(spiSettings);
  digitalWrite(XIAO_SS_PIN, LOW);  // Select XIAO
  delayMicroseconds(10);
  
  // Send command byte
  SPI.transfer(cmd);
  
  // Send data if any
  for (size_t i = 0; i < len; i++) {
    SPI.transfer(data[i]);
  }
  
  digitalWrite(XIAO_SS_PIN, HIGH);  // Deselect XIAO
  SPI.endTransaction();
}

bool captureAndReceiveImage() {
  // Request a new image capture
  sendCommand(CMD_CAPTURE_IMAGE);
  delay(10);  // Give XIAO time to process
  
  // Request the frame data
  sendCommand(CMD_GET_FRAME);
  
  // Prepare to receive data
  SPISettings spiSettings(SPI_SPEED_MHZ * 1000000, MSBFIRST, SPI_MODE0);
  SPI.beginTransaction(spiSettings);
  digitalWrite(XIAO_SS_PIN, LOW);  // Select XIAO
  
  // First receive header (4 bytes for image size)
  size_t receivedSize = 0;
  receivedSize |= (size_t)SPI.transfer(0) << 24;
  receivedSize |= (size_t)SPI.transfer(0) << 16;
  receivedSize |= (size_t)SPI.transfer(0) << 8;
  receivedSize |= (size_t)SPI.transfer(0);
  
  if (receivedSize == 0 || receivedSize > 100000) {
    // Invalid size, abort
    digitalWrite(XIAO_SS_PIN, HIGH);
    SPI.endTransaction();
    Serial.printf("Invalid image size: %u\n", receivedSize);
    return false;
  }
  
  // Check/reallocate buffer if needed
  if (receivedSize != imageSize) {
    free(imageBuffer);
    imageBuffer = (uint8_t *)malloc(receivedSize);
    if (!imageBuffer) {
      digitalWrite(XIAO_SS_PIN, HIGH);
      SPI.endTransaction();
      Serial.println("Failed to reallocate image buffer");
      return false;
    }
    imageSize = receivedSize;
  }
  
  #if defined(__IMXRT1062__) && USE_DMA
    // Use DMA for faster transfers on Teensy 4.1
    if (dmaChannel) {
      dmaTransferComplete = false;
      dmaChannel.destinationBuffer(imageBuffer, receivedSize);
      dmaChannel.enable();
      
      // Wait for DMA transfer to complete or timeout
      unsigned long startTime = millis();
      while (!dmaTransferComplete && (millis() - startTime < 1000)) {
        // Wait for DMA to complete or 1 second timeout
      }
      
      dmaChannel.disable();
    } else {
      // Fallback to normal transfers
      for (size_t i = 0; i < receivedSize; i++) {
        imageBuffer[i] = SPI.transfer(0);
      }
    }
  #else
    // Standard transfer on other platforms
    for (size_t i = 0; i < receivedSize; i++) {
      imageBuffer[i] = SPI.transfer(0);
    }
  #endif
  
  digitalWrite(XIAO_SS_PIN, HIGH);  // Deselect XIAO
  SPI.endTransaction();
  
  return true;
}

void processAndDisplayImage() {
  u8g2.clearBuffer();
  
  #if defined(USE_ST7565P)
    // For ST7565P (monochrome), directly copy the 1-bit image
    memcpy(u8g2.getBufferPtr(), imageBuffer, imageSize);
    
  #elif defined(USE_CFA10110)
    // For CFA10110 (4 grayscale levels), convert 2-bit to U8g2 format
    
    #if defined(__IMXRT1062__) && USE_PXP_ACCELERATION && defined(pxpDestBuffer)
      // Use PXP for hardware-accelerated conversion
      if (pxpSourceBuffer && pxpDestBuffer) {
        // Convert 2-bit grayscale to RGB565 for PXP processing
        for (int i = 0; i < imageSize; i++) {
          uint8_t byte = imageBuffer[i];
          for (int j = 0; j < 4; j++) {  // Each byte contains 4 pixels
            uint8_t gray = (byte >> ((3-j) * 2)) & 0x03;  // Extract 2-bit grayscale value
            
            // Convert to 16-bit RGB565 grayscale
            uint16_t color = gray * 0x5555;  // Scale 0-3 to 0-65535 approx
            
            pxpSourceBuffer[i*4 + j] = color;
          }
        }
        
        // Use PXP to process the image (additional effects could be added here)
        pxp.process(pxpSourceBuffer, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                   pxpDestBuffer);
                   
        // Convert PXP output to U8G2 format
        for (int y = 0; y < DISPLAY_HEIGHT; y++) {
          for (int x = 0; x < DISPLAY_WIDTH; x++) {
            int pixelIndex = y * DISPLAY_WIDTH + x;
            int byteIndex = pixelIndex / 4;
            int bitPos = (pixelIndex % 4) * 2;
            uint8_t gray = (pxpDestBuffer[byteIndex] >> (6-bitPos)) & 0x03;
            
            u8g2.setDrawColor(gray);
            u8g2.drawPixel(x, y);
          }
        }
      } else 
    #endif
    {
      // Standard software conversion if PXP not available
      for (int y = 0; y < DISPLAY_HEIGHT; y++) {
        for (int x = 0; x < DISPLAY_WIDTH; x++) {
          // Calculate position in the buffer
          int pixelIndex = y * DISPLAY_WIDTH + x;
          int byteIndex = pixelIndex / 4;
          int bitPos = (pixelIndex % 4) * 2;
          
          // Extract 2-bit grayscale value (0-3)
          uint8_t gray = (imageBuffer[byteIndex] >> (6-bitPos)) & 0x03;
          
          // Draw pixel with appropriate grayscale level
          u8g2.setDrawColor(gray);
          u8g2.drawPixel(x, y);
        }
      }
    }
  #endif
  
  // Flush cache before sending buffer to display
  #if defined(__IMXRT1062__)
    ARM_DCACHE_FLUSH_AND_INVALIDATE(u8g2.getBufferPtr(), u8g2.getBufferSize());
  #endif
  
  // Send to display
  u8g2.sendBuffer();
}

void displayFPS() {
  // No need to clear since we're drawing over the image
  char buffer[32];
  u8g2.setFont(u8g2_font_5x7_tr);
  
  // Set maximum contrast for text overlay
  #if defined(USE_ST7565P)
    u8g2.setDrawColor(1);  // White on monochrome display
  #elif defined(USE_CFA10110)
    u8g2.setDrawColor(DISPLAY_GRAYSCALE_LEVELS - 1);  // Darkest color for grayscale
  #endif
  
  // Display FPS
  sprintf(buffer, "FPS: %.1f", fps);
  u8g2.drawStr(DISPLAY_WIDTH - 60, 8, buffer);
  
  // Display processing time
  sprintf(buffer, "ms: %lu", processingTime);
  u8g2.drawStr(5, DISPLAY_HEIGHT - 5, buffer);
}
