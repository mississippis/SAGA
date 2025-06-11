#include <Arduino.h>
#include <U8g2lib.h>
#include <SPI.h>
#include "soc/soc.h"          // For disabling brownout problems
#include "soc/rtc_cntl_reg.h" // For disabling brownout problems
#include "display_config_cfa10110.h"
#include "camera_settings.h"   // Camera initialization utility
#include "image_processing.h"  // Image processing utilities
#include "esp_camera.h"

// U8g2 constructor for CFA10110 display with hardware SPI
// CFA10110 is compatible with the ST75256 driver, supporting 4 gray levels
// Using JLX240160 as the closest match for the 240x128 display
U8G2_ST75256_JLX240160_F_4W_HW_SPI u8g2(
  U8G2_R0,
  DISPLAY_CS_PIN, 
  DISPLAY_DC_PIN, 
  DISPLAY_RST_PIN
);

// Frame buffer for processed image
uint8_t* processedImage = NULL;

// Performance metrics
unsigned long lastFrameTime = 0;
unsigned long frameCount = 0;
float fps = 0;
unsigned long processingTime = 0;

// Processing mode (0=direct, 1=gamma, 2=ordered dither, 3=floyd-steinberg)
int processingMode = 0;
const int NUM_PROCESSING_MODES = 4;
unsigned long lastModeChange = 0;
const unsigned long MODE_CHANGE_INTERVAL = 5000; // Change mode every 5 seconds

void setup_display() {
  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB10_tr);
  u8g2.setDrawColor(DISPLAY_GRAYSCALE_LEVELS - 1); // Darkest color
  u8g2.drawStr(20, 30, "CFA10110 Camera Test");
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(20, 50, "Starting camera...");
  u8g2.sendBuffer();
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // Disable brownout detector
  
  // Initialize LED pin for visual feedback
  pinMode(XIAO_LED_BUILTIN, OUTPUT);
  digitalWrite(XIAO_LED_BUILTIN, HIGH);  // LED on during initialization
  
  Serial.begin(115200);
  while(!Serial) delay(10);
  Serial.println("XIAO ESP32S3 Camera to CFA10110 Display Test");

  // Initialize display
  setup_display();
  
  // Initialize camera using camera_settings utility
  if (!initCamera(XIAO_ESP32S3_OV2640, FRAMESIZE_QVGA, PIXFORMAT_GRAYSCALE)) {
    Serial.println("Failed to initialize camera");
    u8g2.clearBuffer();
    u8g2.drawStr(20, 70, "Camera init failed!");
    u8g2.sendBuffer();
    while(1) {
      // Blink LED to indicate error
      digitalWrite(XIAO_LED_BUILTIN, !digitalRead(XIAO_LED_BUILTIN));
      delay(500);
    }
  }
  
  // Apply camera settings optimizations
  optimizeCameraSettings(1); // Use higher contrast profile
  
  // Allocate memory for processed image buffer
  processedImage = (uint8_t*)malloc(DISPLAY_WIDTH * DISPLAY_HEIGHT);
  if (!processedImage) {
    Serial.println("Failed to allocate memory for image processing");
  }
  
  Serial.println("System initialized successfully");
  
  // Display startup message
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB10_tr);
  u8g2.drawStr(10, 30, "Ready!");
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(10, 50, "Processing modes will cycle");
  u8g2.drawStr(10, 65, "Press RESET to restart");
  u8g2.sendBuffer();
  delay(1000);
  
  digitalWrite(XIAO_LED_BUILTIN, LOW);  // LED off after initialization
}

void displayProcessingMode() {
  const char* modeNames[] = {
    "Direct Map",
    "Gamma Correction",
    "Ordered Dither",
    "Floyd-Steinberg"
  };

  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.setDrawColor(DISPLAY_GRAYSCALE_LEVELS - 1); // Darkest color
  u8g2.drawStr(5, DISPLAY_HEIGHT - 6, modeNames[processingMode]);
}

void loop() {
  // Check if it's time to change processing mode
  if (millis() - lastModeChange > MODE_CHANGE_INTERVAL) {
    processingMode = (processingMode + 1) % NUM_PROCESSING_MODES;
    lastModeChange = millis();
    
    // Quick triple-blink to indicate mode change
    for (int i = 0; i < 3; i++) {
      digitalWrite(XIAO_LED_BUILTIN, HIGH);
      delay(50);
      digitalWrite(XIAO_LED_BUILTIN, LOW);
      delay(50);
    }
  }
  
  digitalWrite(XIAO_LED_BUILTIN, HIGH); // LED on during frame capture
  unsigned long startTime = millis();
  
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    digitalWrite(XIAO_LED_BUILTIN, LOW);
    delay(200);
    return;
  }

  if (fb->format != PIXFORMAT_GRAYSCALE) {
    Serial.println("Error: Unexpected camera pixel format!");
    esp_camera_fb_return(fb);
    digitalWrite(XIAO_LED_BUILTIN, LOW);
    return;
  }

  // Clear display buffer
  u8g2.clearBuffer();
  
  // Get camera image dimensions
  const int cam_width = fb->width;   // 320 for QVGA
  const int cam_height = fb->height; // 240 for QVGA

  // Scale the camera image to fit the display
  if (processedImage) {
    // Scale the image to display dimensions with center crop
    scaleGrayscaleImage(fb->buf, cam_width, cam_height, 
                       processedImage, DISPLAY_WIDTH, DISPLAY_HEIGHT, true);
    
    // Process the image based on current mode
    switch (processingMode) {
      case 0: // Direct mapping
        // Map grayscale levels to display levels (0-3 for CFA10110)
        mapGrayscaleLevels(processedImage, processedImage, 
                         DISPLAY_WIDTH, DISPLAY_HEIGHT, 
                         DISPLAY_GRAYSCALE_LEVELS, 1.0); // Linear mapping
        break;
        
      case 1: // Gamma correction
        // Map grayscale with gamma correction for better perception
        mapGrayscaleLevels(processedImage, processedImage, 
                         DISPLAY_WIDTH, DISPLAY_HEIGHT, 
                         DISPLAY_GRAYSCALE_LEVELS, 0.7); // Gamma = 0.7 for brightness
        break;
        
      case 2: // Ordered dithering
        // For ordered dithering, we need a temporary buffer for binary output
        // But since our display has 4 gray levels, we'll map the binary output to levels 0 and 3
        if (DISPLAY_GRAYSCALE_LEVELS > 2) {
          orderedDithering(processedImage, processedImage, DISPLAY_WIDTH, DISPLAY_HEIGHT);
          // Convert binary 0/255 to grayscale levels 0/3
          for (int i = 0; i < DISPLAY_WIDTH * DISPLAY_HEIGHT; i++) {
            if (processedImage[i] > 127) {
              processedImage[i] = DISPLAY_GRAYSCALE_LEVELS - 1; 
            } else {
              processedImage[i] = 0;
            }
          }
        } else {
          // For pure binary displays
          orderedDithering(processedImage, processedImage, DISPLAY_WIDTH, DISPLAY_HEIGHT);
        }
        break;
        
      case 3: // Floyd-Steinberg dithering
        // Similar to ordered dithering, but with error diffusion
        if (DISPLAY_GRAYSCALE_LEVELS > 2) {
          floydSteinbergDithering(processedImage, processedImage, DISPLAY_WIDTH, DISPLAY_HEIGHT);
          // Convert binary 0/255 to grayscale levels 0/3
          for (int i = 0; i < DISPLAY_WIDTH * DISPLAY_HEIGHT; i++) {
            if (processedImage[i] > 127) {
              processedImage[i] = DISPLAY_GRAYSCALE_LEVELS - 1;
            } else {
              processedImage[i] = 0;
            }
          }
        } else {
          // For pure binary displays
          floydSteinbergDithering(processedImage, processedImage, DISPLAY_WIDTH, DISPLAY_HEIGHT);
        }
        break;
    }
    
    // Render the processed image to display
    renderToDisplay(u8g2, processedImage, DISPLAY_WIDTH, DISPLAY_HEIGHT, 0, 0, DISPLAY_GRAYSCALE_LEVELS);
  }
  
  // Calculate and display performance metrics
  frameCount++;
  unsigned long currentTime = millis();
  processingTime = currentTime - startTime;
  
  if (currentTime - lastFrameTime >= 1000) { // Update FPS every second
    fps = frameCount * 1000.0 / (currentTime - lastFrameTime);
    frameCount = 0;
    lastFrameTime = currentTime;
  }

  // Draw FPS and processing time
  char buffer[16];
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.setDrawColor(DISPLAY_GRAYSCALE_LEVELS - 1); // Darkest color for text
  sprintf(buffer, "FPS:%.1f", fps);
  u8g2.drawStr(5, 10, buffer);
  sprintf(buffer, "ms:%lu", processingTime);
  u8g2.drawStr(DISPLAY_WIDTH - 50, 10, buffer);
  
  // Display current processing mode
  displayProcessingMode();

  // Send buffer to display
  digitalWrite(XIAO_LED_BUILTIN, LOW); // LED off briefly before sending buffer
  u8g2.sendBuffer();              
  
  // Quick blink LED when buffer is sent
  digitalWrite(XIAO_LED_BUILTIN, HIGH);
  delay(10);
  digitalWrite(XIAO_LED_BUILTIN, LOW);
  
  // Return the frame buffer to be reused
  esp_camera_fb_return(fb);
}
