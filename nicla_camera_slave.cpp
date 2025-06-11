#include <Arduino.h>
#include <SPI.h>
#include <camera.h>
#include <Wire.h>

// Pin definitions for Nicla Vision to Teensy 4.1 SPI communication
// Based on user provided pinout
#define SPI_MISO_PIN PE13   // MISO to Teensy pin 1 (MISO1)
#define SPI_MOSI_PIN PE14   // MOSI (not explicitly mentioned in pinout)
#define SPI_SCLK_PIN PE12   // SCK to Teensy pin 27 (SCK1)
#define SPI_SS_PIN   PE11   // CS to Teensy pin 0 (CS1)

// Additional pins
#define READY_PIN    PG0    // Signals new frame is ready
#define BUSY_LED     LED_PWM // Built-in LED

// Communication protocol commands
#define CMD_CAPTURE_IMAGE     0x01
#define CMD_GET_FRAME         0x02
#define CMD_SET_RESOLUTION    0x03
#define CMD_SET_QUALITY       0x04
#define CMD_SET_CONTRAST      0x05
#define CMD_SET_BRIGHTNESS    0x06
#define CMD_SET_TARGET        0x07  // To specify which display we're targeting

// Target display types
#define DISPLAY_ST7565P       0x01  // 128x64 monochrome
#define DISPLAY_ST75256       0x02  // 240x128 grayscale (4 levels)

// Camera setup
#define IMAGE_WIDTH  320
#define IMAGE_HEIGHT 240

// Global variables
uint8_t displayTarget = DISPLAY_ST7565P;  // Default to ST7565P
uint8_t *imageBuffer = NULL;        // Raw camera frame buffer
uint8_t *processedImage = NULL;     // Pre-processed image buffer
size_t processedSize = 0;          // Size of processed image
bool newFrameReady = false;        // Flag to indicate a new frame is ready
unsigned long lastFrameTime = 0;
Camera cam;
CameraImage img;

// Function prototypes
bool setupCamera();
void processImageForDisplay();
void processSPICommand(uint8_t cmd, uint8_t *data, size_t len);
void sendImageData();
void captureFrame();

// SPI interrupt service routine
volatile bool spiTransactionActive = false;
volatile uint8_t spiCommand = 0;
volatile uint8_t spiDataBuffer[16];
volatile size_t spiDataLen = 0;

void setup() {
  // Initialize serial for debugging
  Serial.begin(115200);
  delay(1000); // Give serial port time to connect
  Serial.println("Nicla Vision Camera Slave");
  
  // Initialize pins
  pinMode(BUSY_LED, OUTPUT);
  digitalWrite(BUSY_LED, HIGH);  // Busy during setup
  
  pinMode(READY_PIN, OUTPUT);
  digitalWrite(READY_PIN, LOW);  // Not ready yet
  
  // Initialize SPI in slave mode
  SPI.begin();
  pinMode(SPI_SS_PIN, INPUT);
  pinMode(SPI_MISO_PIN, OUTPUT);
  pinMode(SPI_MOSI_PIN, INPUT);
  pinMode(SPI_SCLK_PIN, INPUT);
  
  // Setup camera
  if (!setupCamera()) {
    Serial.println("Failed to initialize camera");
    while (1) {
      digitalWrite(BUSY_LED, HIGH);
      delay(100);
      digitalWrite(BUSY_LED, LOW);
      delay(100);
    }
  }
  
  // Allocate memory for processed image (start with ST7565P size)
  processedImage = (uint8_t *)malloc(128*64/8);
  if (processedImage == NULL) {
    Serial.println("Failed to allocate memory for processed image");
    while (1);
  }
  processedSize = 128*64/8;
  
  Serial.println("Setup complete");
  digitalWrite(BUSY_LED, LOW);  // Setup finished
}

void loop() {
  // Check if we need to capture a new frame
  if (millis() - lastFrameTime > 100) {  // Capture at approximately 10 FPS
    captureFrame();
  }
  
  // Check if Teensy is selecting us (SPI SS goes LOW)
  if (digitalRead(SPI_SS_PIN) == LOW) {
    digitalWrite(BUSY_LED, HIGH);  // Indicate we're busy
    
    // Process SPI communication
    if (SPI.available()) {
      uint8_t cmd = SPI.transfer(0);
      uint8_t data[16];  // Buffer for command parameters
      size_t dataLen = 0;
      
      // Read any additional data bytes until SS goes high
      while (digitalRead(SPI_SS_PIN) == LOW && dataLen < sizeof(data)) {
        if (SPI.available()) {
          data[dataLen++] = SPI.transfer(0);
        }
        delayMicroseconds(10);
      }
      
      // Process the received command
      processSPICommand(cmd, data, dataLen);
    }
    
    digitalWrite(BUSY_LED, LOW);  // No longer busy
  }
  
  // Small delay to prevent CPU hogging
  delay(1);
}

bool setupCamera() {
  // Initialize camera
  if (!cam.begin(CAMERA_R320x240, 30, 1)) {
    Serial.println("Failed to initialize camera");
    return false;
  }
  
  // Set camera parameters
  cam.setFrameRate(20);  // Set frame rate to 20 FPS
  cam.setCameraBrightness(2);  // Range: 0-4
  cam.setCameraContrast(2);    // Range: 0-4
  cam.setCameraGain(2);        // Range: 0-4
  
  Serial.println("Camera initialized successfully");
  return true;
}

void captureFrame() {
  digitalWrite(BUSY_LED, HIGH);  // Busy capturing
  
  // Capture frame from camera
  if (cam.grabFrame(img, 1000) == 0) {
    // Frame captured successfully
    lastFrameTime = millis();
    
    // Process image for the target display
    processImageForDisplay();
    
    // Signal that a new frame is ready
    newFrameReady = true;
    digitalWrite(READY_PIN, HIGH);
    delayMicroseconds(100);
    digitalWrite(READY_PIN, LOW);
  } else {
    Serial.println("Failed to capture frame");
  }
  
  digitalWrite(BUSY_LED, LOW);  // Done capturing
}

void processImageForDisplay() {
  // Process the image differently depending on target display
  if (displayTarget == DISPLAY_ST7565P) {
    // Monochrome display - convert to 1-bit
    if (processedSize != 128*64/8) {
      // Reallocate if needed
      free(processedImage);
      processedImage = (uint8_t *)malloc(128*64/8);
      processedSize = 128*64/8;
    }
    
    // Clear the buffer
    memset(processedImage, 0, processedSize);
    
    // Simple thresholding and downsampling to 128x64
    for (int y = 0; y < 64; y++) {
      for (int x = 0; x < 128; x++) {
        // Calculate source coordinates (centering the image)
        int srcX = (x * IMAGE_WIDTH / 128);
        int srcY = (y * IMAGE_HEIGHT / 64);
        if (srcX >= IMAGE_WIDTH) srcX = IMAGE_WIDTH - 1;
        if (srcY >= IMAGE_HEIGHT) srcY = IMAGE_HEIGHT - 1;
        
        // Calculate position in the Nicla camera buffer
        uint8_t *pixelPtr = img.getPixelAddress(srcX, srcY);
        
        // Get grayscale value (convert RGB565 to grayscale)
        // Note: Nicla Vision camera format may vary, adjust accordingly
        uint16_t pixelValue = (pixelPtr[0] << 8) | pixelPtr[1];  // RGB565 format
        uint8_t r = ((pixelValue >> 11) & 0x1F) << 3;
        uint8_t g = ((pixelValue >> 5) & 0x3F) << 2;
        uint8_t b = (pixelValue & 0x1F) << 3;
        uint8_t gray = (r + g + g + b) >> 2;  // Simple grayscale conversion
        
        // Threshold to 1-bit (128 threshold)
        if (gray > 128) {
          // Set the bit in the right position
          processedImage[y * 16 + (x / 8)] |= (1 << (7 - (x % 8)));
        }
      }
    }
  } else if (displayTarget == DISPLAY_ST75256) {
    // Grayscale display - convert to 2-bit (4 levels)
    if (processedSize != 240*128/4) {
      // Reallocate if needed
      free(processedImage);
      processedImage = (uint8_t *)malloc(240*128/4);
      processedSize = 240*128/4;
    }
    
    // Clear the buffer
    memset(processedImage, 0, processedSize);
    
    // Convert to 2-bit grayscale and downsample to 240x128
    for (int y = 0; y < 128; y++) {
      for (int x = 0; x < 240; x++) {
        // Calculate source coordinates
        int srcX = (x * IMAGE_WIDTH / 240);
        int srcY = (y * IMAGE_HEIGHT / 128);
        if (srcX >= IMAGE_WIDTH) srcX = IMAGE_WIDTH - 1;
        if (srcY >= IMAGE_HEIGHT) srcY = IMAGE_HEIGHT - 1;
        
        // Calculate position in the Nicla camera buffer
        uint8_t *pixelPtr = img.getPixelAddress(srcX, srcY);
        
        // Get grayscale value (convert RGB565 to grayscale)
        uint16_t pixelValue = (pixelPtr[0] << 8) | pixelPtr[1];  // RGB565 format
        uint8_t r = ((pixelValue >> 11) & 0x1F) << 3;
        uint8_t g = ((pixelValue >> 5) & 0x3F) << 2;
        uint8_t b = (pixelValue & 0x1F) << 3;
        uint8_t gray = (r + g + g + b) >> 2;  // Simple grayscale conversion
        
        // Convert to 2-bit (0-3)
        uint8_t grayLevel = gray >> 6;  // 0-3
        
        // Calculate position in the buffer
        int bytePos = (y * 240 + x) / 4;
        int bitPos = ((y * 240 + x) % 4) * 2;
        
        // Set the 2 bits at the right position
        processedImage[bytePos] |= (grayLevel << (6 - bitPos));
      }
    }
  }
}

void processSPICommand(uint8_t cmd, uint8_t *data, size_t len) {
  switch (cmd) {
    case CMD_CAPTURE_IMAGE:
      // Force a new frame capture
      captureFrame();
      break;
      
    case CMD_GET_FRAME:
      // Send the processed image data back to Teensy
      sendImageData();
      break;
      
    case CMD_SET_RESOLUTION:
      if (len >= 1) {
        // Not implemented for Nicla - resolution is fixed
      }
      break;
      
    case CMD_SET_QUALITY:
      if (len >= 1) {
        // Not directly applicable to Nicla
      }
      break;
      
    case CMD_SET_CONTRAST:
      if (len >= 1) {
        uint8_t contrast = constrain(data[0], 0, 4);
        cam.setCameraContrast(contrast);
      }
      break;
      
    case CMD_SET_BRIGHTNESS:
      if (len >= 1) {
        uint8_t brightness = constrain(data[0], 0, 4);
        cam.setCameraBrightness(brightness);
      }
      break;
      
    case CMD_SET_TARGET:
      if (len >= 1) {
        displayTarget = data[0];
      }
      break;
      
    default:
      Serial.printf("Unknown command: 0x%02X\n", cmd);
      break;
  }
}

void sendImageData() {
  // Wait for Teensy to select us
  while (digitalRead(SPI_SS_PIN) == HIGH) {
    delayMicroseconds(10);
  }
  
  digitalWrite(BUSY_LED, HIGH); // Busy sending data
  
  // Send header: image size (4 bytes)
  SPI.transfer((uint8_t)(processedSize >> 24));
  SPI.transfer((uint8_t)(processedSize >> 16));
  SPI.transfer((uint8_t)(processedSize >> 8));
  SPI.transfer((uint8_t)(processedSize));
  
  // Send the actual image data
  for (size_t i = 0; i < processedSize; i++) {
    SPI.transfer(processedImage[i]);
    
    // Check if SS is still active
    if (digitalRead(SPI_SS_PIN) == HIGH) {
      break; // Transmission interrupted
    }
  }
  
  digitalWrite(BUSY_LED, LOW); // Done sending data
}
