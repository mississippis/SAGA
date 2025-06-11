#include <Arduino.h>
#include <SPI.h>
#include <esp_camera.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

// Pin definitions for XIAO ESP32S3 <-> Teensy 4.1 SPI communication
#define HSPI_MISO       10  // XIAO D9 to Teensy MISO
#define HSPI_MOSI       9   // XIAO D10 to Teensy MOSI
#define HSPI_SCLK       8   // XIAO D8 to Teensy SCK
#define HSPI_SS         7   // XIAO D7 to Teensy SS (pin 10 on Teensy)

// Additional control pins
#define READY_PIN       6   // XIAO D6 to Teensy to signal data ready
#define BUSY_LED        LED_BUILTIN

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

// Camera configuration for OV2640
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     45
#define SIOD_GPIO_NUM     40
#define SIOC_GPIO_NUM     39
#define Y9_GPIO_NUM       48
#define Y8_GPIO_NUM       10
#define Y7_GPIO_NUM       11
#define Y6_GPIO_NUM       12
#define Y5_GPIO_NUM       13
#define Y4_GPIO_NUM       14
#define Y3_GPIO_NUM       47
#define Y2_GPIO_NUM       46
#define VSYNC_GPIO_NUM    38
#define HREF_GPIO_NUM     41
#define PCLK_GPIO_NUM     42

// Global variables
uint8_t displayTarget = DISPLAY_ST7565P;  // Default to ST7565P
uint8_t jpegQuality = 12;                 // For potential JPEG compression (0-63, lower is better quality)
camera_fb_t *fb = NULL;                   // Camera frame buffer
uint8_t *processedImage = NULL;           // Pre-processed image buffer
size_t processedSize = 0;                 // Size of processed image
SemaphoreHandle_t imageMutex = NULL;      // For thread-safe access to image data
TaskHandle_t captureTaskHandle = NULL;    // Handle to the image capture task

// SPI slave interface
static const SPISettings spiSettings(10000000, MSBFIRST, SPI_MODE0);

// Function prototypes
void setupCamera();
void captureTask(void *parameter);
void processImageForDisplay(camera_fb_t *fb);
void processSPICommand(uint8_t cmd, uint8_t *data, size_t len);
void sendImageData();

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("XIAO ESP32S3 Camera Slave");
  
  // Initialize pins
  pinMode(BUSY_LED, OUTPUT);
  digitalWrite(BUSY_LED, HIGH);  // Indicate busy during setup
  
  pinMode(HSPI_MISO, OUTPUT);
  pinMode(HSPI_MOSI, INPUT);
  pinMode(HSPI_SCLK, INPUT);
  pinMode(HSPI_SS, INPUT);
  pinMode(READY_PIN, OUTPUT);
  digitalWrite(READY_PIN, LOW);  // Not ready yet
  
  // Initialize SPI in slave mode
  SPI.begin(HSPI_SCLK, HSPI_MISO, HSPI_MOSI, HSPI_SS);
  SPI.setDataMode(SPI_MODE0);
  SPI.setBitOrder(MSBFIRST);
  
  // Create mutex for thread-safe image access
  imageMutex = xSemaphoreCreateMutex();
  if (imageMutex == NULL) {
    Serial.println("Failed to create mutex");
    while(1);
  }
  
  // Initialize camera
  setupCamera();
  
  // Start capture task on core 1 (ESP32S3 is dual core)
  xTaskCreatePinnedToCore(
    captureTask,          // Function to implement the task
    "CaptureTask",        // Name of the task
    8192,                 // Stack size in words
    NULL,                 // Task input parameter
    2,                    // Priority of the task
    &captureTaskHandle,   // Task handle
    1                     // Core where the task should run
  );
  
  Serial.println("Setup complete");
  digitalWrite(BUSY_LED, LOW);  // Setup finished
}

void loop() {
  // Check if Teensy is selecting us (SPI SS goes LOW)
  if (digitalRead(HSPI_SS) == LOW) {
    digitalWrite(BUSY_LED, HIGH);  // Indicate we're busy
    
    // Wait for first byte (command)
    SPI.beginTransaction(spiSettings);
    while (digitalRead(HSPI_SS) == LOW && !SPI.available()) {
      delayMicroseconds(10);
    }
    
    if (SPI.available()) {
      uint8_t cmd = SPI.transfer(0);
      uint8_t data[16]; // Buffer for command parameters
      size_t dataLen = 0;
      
      // Read any additional data bytes until SS goes high
      while (digitalRead(HSPI_SS) == LOW && dataLen < sizeof(data)) {
        if (SPI.available()) {
          data[dataLen++] = SPI.transfer(0);
        }
        delayMicroseconds(10);
      }
      
      SPI.endTransaction();
      
      // Process the received command
      processSPICommand(cmd, data, dataLen);
    } else {
      SPI.endTransaction();
    }
    
    digitalWrite(BUSY_LED, LOW);  // No longer busy
  }
  
  // Yield to other tasks
  delay(1);
}

void setupCamera() {
  // Configure camera
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_QVGA;  // 320x240, we'll downsample as needed
  config.pixel_format = PIXFORMAT_GRAYSCALE;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.fb_count = 2;
  config.jpeg_quality = jpegQuality;
  
  // Initialize camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    while (1);
  }
  
  // Set initial camera parameters
  sensor_t * s = esp_camera_sensor_get();
  s->set_brightness(s, 1);  // -2 to 2
  s->set_contrast(s, 0);    // -2 to 2
  s->set_saturation(s, 0);  // -2 to 2
  s->set_whitebal(s, 1);    // 0 = disable , 1 = enable
  s->set_awb_gain(s, 1);    // 0 = disable , 1 = enable
  s->set_wb_mode(s, 0);     // 0 to 4 - auto, sunny, cloudy, office, home
  s->set_aec_value(s, 300); // 0 to 1200
  
  Serial.println("Camera initialized");
  
  // Allocate memory for processed image
  processedImage = (uint8_t *)malloc(128*64/8);  // For ST7565P initially
  if (processedImage == NULL) {
    Serial.println("Failed to allocate memory for processed image");
    while (1);
  }
  processedSize = 128*64/8;
}

void captureTask(void *parameter) {
  while (true) {
    // Capture frame if needed
    if (!fb) {
      fb = esp_camera_fb_get();
      if (fb) {
        // Process the image for the current display target
        if (xSemaphoreTake(imageMutex, portMAX_DELAY)) {
          processImageForDisplay(fb);
          xSemaphoreGive(imageMutex);
          
          // Signal that new data is ready
          digitalWrite(READY_PIN, HIGH);
          delayMicroseconds(100);
          digitalWrite(READY_PIN, LOW);
        }
      }
    }
    
    // Let other tasks run
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void processImageForDisplay(camera_fb_t *fb) {
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
    
    // Simple thresholding and downsampling
    for (int y = 0; y < 64; y++) {
      for (int x = 0; x < 128; x++) {
        // Calculate source coordinates (centering the image)
        int srcX = (x * fb->width / 128);
        int srcY = (y * fb->height / 64);
        if (srcX >= fb->width) srcX = fb->width - 1;
        if (srcY >= fb->height) srcY = fb->height - 1;
        
        // Get pixel value (0-255)
        uint8_t pixel = fb->buf[srcY * fb->width + srcX];
        
        // Threshold to 1-bit (128 threshold)
        if (pixel > 128) {
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
    
    // Convert to 2-bit grayscale and downsample
    for (int y = 0; y < 128; y++) {
      for (int x = 0; x < 240; x++) {
        // Calculate source coordinates
        int srcX = (x * fb->width / 240);
        int srcY = (y * fb->height / 128);
        if (srcX >= fb->width) srcX = fb->width - 1;
        if (srcY >= fb->height) srcY = fb->height - 1;
        
        // Get pixel value (0-255)
        uint8_t pixel = fb->buf[srcY * fb->width + srcX];
        
        // Convert to 2-bit (0-3)
        uint8_t gray = pixel >> 6;
        
        // Calculate position in the buffer
        int bytePos = (y * 240 + x) / 4;
        int bitPos = ((y * 240 + x) % 4) * 2;
        
        // Set the 2 bits at the right position
        processedImage[bytePos] |= (gray << (6 - bitPos));
      }
    }
  }
}

void processSPICommand(uint8_t cmd, uint8_t *data, size_t len) {
  switch (cmd) {
    case CMD_CAPTURE_IMAGE:
      // Release previous frame buffer if any
      if (fb) {
        esp_camera_fb_return(fb);
        fb = NULL;
      }
      
      // Next loop iteration will capture a new frame
      break;
      
    case CMD_GET_FRAME:
      // Send the processed image data back to Teensy
      sendImageData();
      break;
      
    case CMD_SET_RESOLUTION:
      if (len >= 1) {
        sensor_t * s = esp_camera_sensor_get();
        s->set_framesize(s, (framesize_t)data[0]);
      }
      break;
      
    case CMD_SET_QUALITY:
      if (len >= 1) {
        jpegQuality = data[0];
        sensor_t * s = esp_camera_sensor_get();
        s->set_quality(s, jpegQuality);
      }
      break;
      
    case CMD_SET_CONTRAST:
      if (len >= 1) {
        sensor_t * s = esp_camera_sensor_get();
        s->set_contrast(s, data[0] - 2); // -2 to +2
      }
      break;
      
    case CMD_SET_BRIGHTNESS:
      if (len >= 1) {
        sensor_t * s = esp_camera_sensor_get();
        s->set_brightness(s, data[0] - 2); // -2 to +2
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
  while (digitalRead(HSPI_SS) == HIGH) {
    delayMicroseconds(10);
  }
  
  digitalWrite(BUSY_LED, HIGH); // Busy sending data
  
  // Acquire mutex to ensure no changes during transmission
  if (xSemaphoreTake(imageMutex, portMAX_DELAY)) {
    // Start SPI transaction
    SPI.beginTransaction(spiSettings);
    
    // Send header: image size (4 bytes)
    SPI.transfer((uint8_t)(processedSize >> 24));
    SPI.transfer((uint8_t)(processedSize >> 16));
    SPI.transfer((uint8_t)(processedSize >> 8));
    SPI.transfer((uint8_t)(processedSize));
    
    // Send the actual image data
    for (size_t i = 0; i < processedSize; i++) {
      SPI.transfer(processedImage[i]);
      
      // Check if SS is still active
      if (digitalRead(HSPI_SS) == HIGH) {
        break; // Transmission interrupted
      }
    }
    
    SPI.endTransaction();
    xSemaphoreGive(imageMutex);
  }
  
  digitalWrite(BUSY_LED, LOW); // Done sending data
}
