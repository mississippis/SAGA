#include <Arduino.h>
#include "camera_handler.h"
#include "xiao_esp32s3_pins.h"
#include "display_config.h"
#include "CFADisplay.h"

// Global objects
CameraHandler camera;
CFADisplay display;

// Display dimensions for buffer size calculations
#define BUFFER_WIDTH 128
#define BUFFER_HEIGHT 64

// Status LED
#define STATUS_LED XIAO_LED_BUILTIN // Using built-in LED

// Application modes
enum AppMode {
    MODE_GRAYSCALE,     // Grayscale display with dithering
    MODE_BINARY,        // Binary display with threshold
    MODE_ADAPTIVE,      // Adaptive threshold mode
    MODE_EDGE,          // Edge detection mode
    MODE_INVERTED,      // Inverted image mode
    MODE_STATS,         // Display performance statistics
    MODE_COUNT
};

// Global variables
AppMode currentMode = MODE_GRAYSCALE;
uint8_t threshold = 128;
uint32_t frameCount = 0;
uint32_t lastFrameTime = 0;
uint32_t lastModeSwitch = 0;
uint32_t lastStatsDisplay = 0;
bool ledState = false;

// Function prototypes
void displayFrame(uint8_t* buffer, bool isBinary);
void displayStats();
void displayModeInfo(const char* modeName);
void processSerialCommands();
void setupCamera();

void setup() {
    // Initialize serial port
    Serial.begin(115200);
    delay(500);
    
    Serial.println("\n\nXIAO ESP32S3 Camera Application");
    Serial.println("===============================");
    
    // Set up status LED
    pinMode(STATUS_LED, OUTPUT);
    digitalWrite(STATUS_LED, HIGH); // LED on during initialization
    
    // Initialize display
    Serial.println("Initializing display...");
    bool displayOk = display.begin();
    if (!displayOk) {
        Serial.println("Display initialization failed!");
        while (1) {
            // Blink LED to indicate error
            digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));
            delay(200);
        }
    }
    
    // Show startup message
    display.clear();
    display.drawString(0, 12, "XIAO ESP32S3");
    display.drawString(0, 25, "Camera App");
    display.drawString(0, 40, "Initializing...");
    display.update();
    delay(1000);
    
    // Configure camera settings
    camera_config_settings_t config;
    config.frame_size = CAMERA_SIZE_QVGA;  // Start with QVGA resolution
    config.quality = CAMERA_QUALITY_BALANCED;
    config.process_mode = CAMERA_MODE_GRAYSCALE;
    config.contrast = 1;
    config.brightness = 0;
    config.threshold = threshold;
    config.auto_exposure = true;
    config.awb = true;
    config.high_speed = true;  // Optimize for performance
    
    // Initialize camera
    Serial.println("Initializing camera...");
    bool cameraOk = camera.begin(config);
    if (!cameraOk) {
        Serial.println("Camera initialization failed!");
        display.clear();
        display.drawString(0, 12, "Camera Error!");
        display.drawString(0, 25, "Check connections");
        display.drawString(0, 40, "and reset device");
        display.update();
        
        while (1) {
            // Blink LED to indicate error
            digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));
            delay(500);
        }
    }
    
    // Camera initialized successfully
    Serial.println("Camera initialized successfully!");
    Serial.println("Available commands:");
    Serial.println("  g - Grayscale mode with dithering");
    Serial.println("  b - Binary mode with fixed threshold");
    Serial.println("  a - Adaptive threshold mode");
    Serial.println("  e - Edge detection mode");
    Serial.println("  i - Inverted image mode");
    Serial.println("  s - Display statistics");
    Serial.println("  + - Increase threshold");
    Serial.println("  - - Decrease threshold");
    Serial.println("  c - Toggle contrast");
    Serial.println("  z - Toggle auto exposure");
    
    // Turn off LED to indicate ready state
    digitalWrite(STATUS_LED, LOW);
    
    // Display startup message
    display.clear();
    display.drawString(0, 12, "Camera Ready");
    display.drawString(0, 25, "Press 'g' to start");
    display.drawString(0, 40, "grayscale capture");
    display.update();
    
    // Wait a moment before starting
    delay(1500);
}

void loop() {
    // Process any serial commands
    processSerialCommands();
    
    // Handle LED flashing as a heartbeat
    if (millis() - lastFrameTime > 500) {
        ledState = !ledState;
        digitalWrite(STATUS_LED, ledState);
        lastFrameTime = millis();
    }
    
    // Capture and process a frame based on current mode
    camera_fb_t* fb = camera.captureFrame();
    if (fb) {
        // Update frame counter
        frameCount++;
        lastFrameTime = millis();
        
        // Toggle LED for frame activity indication
        ledState = !ledState;
        digitalWrite(STATUS_LED, ledState);
        
        // Set the processing mode based on the current app mode
        switch (currentMode) {
            case MODE_BINARY:
                camera.setProcessMode(CAMERA_MODE_BINARY);
                camera.setThreshold(threshold);
                break;
                
            case MODE_ADAPTIVE:
                camera.setProcessMode(CAMERA_MODE_ADAPTIVE);
                break;
                
            case MODE_EDGE:
                camera.setProcessMode(CAMERA_MODE_EDGE);
                camera.setThreshold(threshold);
                break;
                
            case MODE_INVERTED:
                camera.setProcessMode(CAMERA_MODE_INVERTED);
                break;
                
            case MODE_GRAYSCALE:
            default:
                camera.setProcessMode(CAMERA_MODE_GRAYSCALE);
                break;
        }
        
        // Process the frame
        camera.processFrame(fb, BUFFER_WIDTH, BUFFER_HEIGHT);
        
        // Get the processed buffer
        uint8_t* buffer = camera.getProcessedBuffer();
        
        // Display the frame if not in stats mode
        if (currentMode != MODE_STATS) {
            // For binary modes (all except grayscale) the data is already binary
            bool isBinary = (currentMode != MODE_GRAYSCALE);
            displayFrame(buffer, isBinary);
        } else {
            // Display statistics
            displayStats();
        }
        
        // Return the frame buffer to the camera
        esp_camera_fb_return(fb);
    }
    
    // Display statistics periodically
    if (currentMode == MODE_STATS && millis() - lastStatsDisplay > 1000) {
        displayStats();
        lastStatsDisplay = millis();
    }
    
    // Short delay to prevent hogging the CPU
    delay(5);
}

void displayFrame(uint8_t* buffer, bool isBinary) {
    // Clear the display buffer
    display.clear();
    
    // Display the processed image
    for (int y = 0; y < BUFFER_HEIGHT; y++) {
        for (int x = 0; x < BUFFER_WIDTH; x++) {
            int index = y * BUFFER_WIDTH + x;
            
            if (isBinary) {
                // For binary modes, the pixel is either 0 or 255
                if (buffer[index] > 128) {
                    display.drawPixel(x, y, 1);
                }
            } else {
                // For grayscale mode, the buffer has already been dithered to 1-bit
                if (buffer[index] > 128) {
                    display.drawPixel(x, y, 1);
                }
            }
        }
    }
    
    // Draw frame counter and mode in the top-left corner
    char frameInfo[20];
    sprintf(frameInfo, "F:%lu", frameCount);
    display.drawString(0, 6, frameInfo);
    
    // Draw mode indicator in the top-right corner
    const char* modeStr = "";
    switch (currentMode) {
        case MODE_GRAYSCALE: modeStr = "GRAY"; break;
        case MODE_BINARY:    modeStr = "BIN"; break;
        case MODE_ADAPTIVE:  modeStr = "ADPT"; break;
        case MODE_EDGE:      modeStr = "EDGE"; break;
        case MODE_INVERTED:  modeStr = "INV"; break;
        default:             modeStr = ""; break;
    }
    
    display.drawString(BUFFER_WIDTH - 25, 6, modeStr);
    
    // Update the display
    display.update();
}

void displayStats() {
    // Get camera statistics
    camera_stats_t stats = camera.getStats();
    
    // Clear display
    display.clear();
    
    // Display header
    display.drawString(0, 8, "Performance Stats");
    
    // Display FPS
    char fpsStr[20];
    sprintf(fpsStr, "FPS: %.1f", stats.avg_fps);
    display.drawString(0, 20, fpsStr);
    
    // Display processing time
    char timeStr[20];
    sprintf(timeStr, "Proc: %.1f ms", stats.avg_processing_time);
    display.drawString(0, 30, timeStr);
    
    // Display frame count and drops
    char frameStr[20];
    sprintf(frameStr, "Frames: %lu", stats.frame_count);
    display.drawString(0, 40, frameStr);
    
    char dropStr[20];
    sprintf(dropStr, "Drops: %lu", stats.frame_drops);
    display.drawString(0, 50, dropStr);
    
    // Update display
    display.update();
    
    // Print to serial as well
    Serial.println("\n--- Camera Statistics ---");
    Serial.printf("Frames: %lu (%.1f FPS)\n", stats.frame_count, stats.avg_fps);
    Serial.printf("Avg processing: %.1f ms\n", stats.avg_processing_time);
    Serial.printf("Dropped frames: %lu\n", stats.frame_drops);
    Serial.printf("Memory usage: %lu bytes\n", stats.memory_usage);
    Serial.println("-----------------------");
}

void displayModeInfo(const char* modeName) {
    // Show mode change on display
    display.clear();
    display.drawString(0, 20, "Mode Changed:");
    display.drawString(0, 35, modeName);
    display.update();
    
    // Print to serial
    Serial.print("Mode changed to: ");
    Serial.println(modeName);
    
    // Briefly pause to show the mode change
    delay(500);
}

void processSerialCommands() {
    if (!Serial.available()) return;
    
    char cmd = Serial.read();
    bool modeChanged = false;
    
    switch (cmd) {
        case 'g':
            currentMode = MODE_GRAYSCALE;
            displayModeInfo("Grayscale");
            modeChanged = true;
            break;
            
        case 'b':
            currentMode = MODE_BINARY;
            displayModeInfo("Binary");
            modeChanged = true;
            break;
            
        case 'a':
            currentMode = MODE_ADAPTIVE;
            displayModeInfo("Adaptive");
            modeChanged = true;
            break;
            
        case 'e':
            currentMode = MODE_EDGE;
            displayModeInfo("Edge Detection");
            modeChanged = true;
            break;
            
        case 'i':
            currentMode = MODE_INVERTED;
            displayModeInfo("Inverted");
            modeChanged = true;
            break;
            
        case 's':
            currentMode = MODE_STATS;
            displayModeInfo("Statistics");
            modeChanged = true;
            break;
            
        case '+':
            threshold = min(threshold + 10, 240);
            camera.setThreshold(threshold);
            Serial.printf("Threshold increased to %d\n", threshold);
            break;
            
        case '-':
            threshold = max(threshold - 10, 20);
            camera.setThreshold(threshold);
            Serial.printf("Threshold decreased to %d\n", threshold);
            break;
            
        case 'c':
            {
                static uint8_t contrast = 1;
                contrast = (contrast + 1) % 3; // Cycle through 0, 1, 2
                camera.setContrast(contrast);
                Serial.printf("Contrast set to %d\n", contrast);
            }
            break;
            
        case 'z':
            {
                static bool autoExposure = true;
                autoExposure = !autoExposure;
                camera.setAutoExposure(autoExposure);
                Serial.printf("Auto exposure: %s\n", autoExposure ? "ON" : "OFF");
            }
            break;
            
        default:
            // Ignore other characters
            break;
    }
    
    // Clear any remaining characters in the serial buffer
    while (Serial.available()) Serial.read();
    
    // Reset stats display timer if mode changed
    if (modeChanged) {
        lastModeSwitch = millis();
        lastStatsDisplay = millis();
    }
}
