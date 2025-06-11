#include "Arduino.h"
#include "display_config.h"
#include "CFADisplay.h"
#include "esp_camera.h"
#include "xiao_esp32s3_pins.h"

// Global display object
CFADisplay display;

// Camera pin definitions for XIAO ESP32S3 Sense
#define PWDN_GPIO_NUM     XIAO_CAM_PWDN     // Not used (-1)
#define RESET_GPIO_NUM    XIAO_CAM_RESET    // Not used (-1)
#define XCLK_GPIO_NUM     XIAO_CAM_XCLK     // GPIO 45
#define SIOD_GPIO_NUM     XIAO_CAM_SIOD     // GPIO 40
#define SIOC_GPIO_NUM     XIAO_CAM_SIOC     // GPIO 39
#define Y9_GPIO_NUM       XIAO_CAM_Y9       // GPIO 48
#define Y8_GPIO_NUM       XIAO_CAM_Y8       // GPIO 11
#define Y7_GPIO_NUM       XIAO_CAM_Y7       // GPIO 12
#define Y6_GPIO_NUM       XIAO_CAM_Y6       // GPIO 14
#define Y5_GPIO_NUM       XIAO_CAM_Y5       // GPIO 16
#define Y4_GPIO_NUM       XIAO_CAM_Y4       // GPIO 18
#define Y3_GPIO_NUM       XIAO_CAM_Y3       // GPIO 17
#define Y2_GPIO_NUM       XIAO_CAM_Y2       // GPIO 15
#define VSYNC_GPIO_NUM    XIAO_CAM_VSYNC    // GPIO 38
#define HREF_GPIO_NUM     XIAO_CAM_HREF     // GPIO 47
#define PCLK_GPIO_NUM     XIAO_CAM_PCLK     // GPIO 13

// Processing parameters
#define BRIGHTNESS_THRESHOLD 128  // Default threshold for binarization
bool adaptiveThreshold = true;    // Use adaptive thresholding by default
uint8_t contrastBoost = 30;       // Optional contrast boost

// Performance tracking
unsigned long frameCount = 0;
unsigned long lastFpsUpdate = 0;
float currentFps = 0;

/**
 * Initialize the camera
 */
bool setupCamera() {
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
    config.pixel_format = PIXFORMAT_GRAYSCALE;
    config.frame_size = FRAMESIZE_QQVGA;  // 160x120 resolution
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.jpeg_quality = 12;
    config.fb_count = 1;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed with error 0x%x\n", err);
        return false;
    }
    
    // Adjust camera settings for better image quality
    sensor_t * s = esp_camera_sensor_get();
    if (s) {
        s->set_brightness(s, 0);     // -2 to 2
        s->set_contrast(s, 1);       // -2 to 2 (slightly higher contrast)
        s->set_saturation(s, 0);     // -2 to 2
        s->set_special_effect(s, 0); // 0 = No Effect
        s->set_whitebal(s, 1);       // 1 = Enable auto white balance
        s->set_awb_gain(s, 1);       // 1 = Enable auto white balance gain
        s->set_wb_mode(s, 0);        // 0 = Auto
        s->set_exposure_ctrl(s, 1);  // 1 = Enable auto exposure
        s->set_aec2(s, 1);           // 1 = Enable auto exposure (AEC sensor)
        s->set_ae_level(s, 0);       // -2 to 2
        s->set_aec_value(s, 300);    // 0 to 1200
        s->set_gain_ctrl(s, 1);      // 1 = Enable auto gain control
        s->set_agc_gain(s, 0);       // 0 to 30
        s->set_gainceiling(s, (gainceiling_t)0);  // 0 to 6
        s->set_bpc(s, 1);            // 1 = Enable bad pixel correction
        s->set_wpc(s, 1);            // 1 = Enable white pixel correction
        s->set_raw_gma(s, 1);        // 1 = Enable gamma correction
        s->set_lenc(s, 1);           // 1 = Enable lens correction
        s->set_hmirror(s, 0);        // 0 = Disable horizontal mirror
        s->set_vflip(s, 0);          // 0 = Disable vertical flip
    }
    
    Serial.println("Camera initialized successfully");
    return true;
}

/**
 * Calculate adaptive threshold based on image content
 */
uint8_t calculateAdaptiveThreshold(const uint8_t* src, size_t width, size_t height) {
    uint32_t sum = 0;
    size_t count = 0;
    
    // Sample every 4th pixel for speed
    for (size_t y = 0; y < height; y += 2) {
        for (size_t x = 0; x < width; x += 2) {
            sum += src[y * width + x];
            count++;
        }
    }
    
    // Calculate mean and adjust threshold
    uint8_t mean = sum / count;
    // Offset from mean - can be tuned for better results
    int16_t threshold = mean + 10;  
    return constrain(threshold, 0, 255);
}

/**
 * Process camera frame to monochrome display
 */
void processFrameForDisplay(const uint8_t* source, size_t width, size_t height) {
    // Clear display buffer
    display.clear();
    
    // Determine threshold (fixed or adaptive)
    uint8_t threshold = adaptiveThreshold ? 
        calculateAdaptiveThreshold(source, width, height) : 
        BRIGHTNESS_THRESHOLD;
    
    // Draw statistics on display
    char buffer[32];
    sprintf(buffer, "FPS: %.1f", currentFps);
    display.drawString(0, 10, buffer);
    
    sprintf(buffer, "Threshold: %d", threshold);
    display.drawString(0, 20, buffer);
    
    // Process camera image for display
    // For ST7565 with 128x64 resolution
    const int DISPLAY_WIDTH = 128;
    const int DISPLAY_HEIGHT = 64;
    
    for (int y = 0; y < DISPLAY_HEIGHT; y++) {
        for (int x = 0; x < DISPLAY_WIDTH; x++) {
            // Map display coordinates to camera coordinates
            int srcX = (x * width) / DISPLAY_WIDTH;
            int srcY = (y * height) / DISPLAY_HEIGHT;
            
            // Get pixel value from camera frame
            uint8_t pixel = source[srcY * width + srcX];
            
            // Apply contrast boost if needed
            if (contrastBoost > 0) {
                pixel = min(255, pixel + contrastBoost);
            }
            
            // Convert to binary using threshold
            // For monochrome display: 1 = pixel on, 0 = pixel off
            if (pixel < threshold) {
                display.drawPixel(x, y, 1); // 1 = pixel on
            }
        }
    }
    
    // Update the display
    display.update();
}

void setup() {
    Serial.begin(115200);
    delay(1000); // Wait for serial to connect
    Serial.println("\nXiao Camera Display Integration Starting...");
    
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
    
    // Show startup message
    display.clear();
    display.drawString(0, 20, "Camera Init...");
    display.update();
    
    // Initialize camera
    if (!setupCamera()) {
        display.clear();
        display.drawString(0, 20, "Camera Failed!");
        display.update();
        while (1);
    }
    
    // Ready to start
    display.clear();
    display.drawString(0, 20, "System Ready");
    display.drawString(0, 35, "Camera OK");
    display.update();
    delay(1000);
    
    // Initialize performance tracking
    lastFpsUpdate = millis();
    frameCount = 0;
}

void loop() {
    static unsigned long lastFrameTime = 0;
    unsigned long frameStartTime = millis();
    
    // Process serial commands
    if (Serial.available()) {
        char cmd = Serial.read();
        switch (cmd) {
            case 'a':  // Toggle adaptive threshold
                adaptiveThreshold = !adaptiveThreshold;
                Serial.printf("Adaptive threshold: %s\n", adaptiveThreshold ? "ON" : "OFF");
                break;
            case '+':  // Increase contrast
                contrastBoost += 10;
                Serial.printf("Contrast boost: %d\n", contrastBoost);
                break;
            case '-':  // Decrease contrast
                contrastBoost = max(0, contrastBoost - 10);
                Serial.printf("Contrast boost: %d\n", contrastBoost);
                break;
            case 't':  // Toggle threshold value
                BRIGHTNESS_THRESHOLD = (BRIGHTNESS_THRESHOLD == 128) ? 64 : 128;
                Serial.printf("Threshold: %d\n", BRIGHTNESS_THRESHOLD);
                break;
        }
    }

    // Capture frame from camera
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Camera capture failed");
        delay(1000);
        return;
    }
    
    // Process frame for display
    processFrameForDisplay(fb->buf, fb->width, fb->height);
    
    // Release frame buffer
    esp_camera_fb_return(fb);
    
    // Update frame count and calculate FPS
    frameCount++;
    unsigned long now = millis();
    if (now - lastFpsUpdate >= 1000) {
        currentFps = frameCount * 1000.0f / (now - lastFpsUpdate);
        Serial.printf("FPS: %.1f\n", currentFps);
        frameCount = 0;
        lastFpsUpdate = now;
    }
    
    // Small delay to prevent overwhelming the system
    // Adjust based on performance needs
    unsigned long frameTime = millis() - frameStartTime;
    if (frameTime < 33) {  // Target ~30fps
        delay(33 - frameTime);
    }
}
