#include <Arduino.h>
#include "camera_handler.h"
#include "xiao_esp32s3_pins.h"
#include "display_config.h"
#include "CFADisplay.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// Global objects
CameraHandler camera;
CFADisplay display;

// Display dimensions for buffer size calculations
#define BUFFER_WIDTH 128
#define BUFFER_HEIGHT 64

// Status LED
#define STATUS_LED XIAO_LED_BUILTIN 

// Memory allocation for image buffers
#define USE_PSRAM true
#define FRAME_BUFFER_SIZE (BUFFER_WIDTH * BUFFER_HEIGHT)

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

// FreeRTOS handles
TaskHandle_t captureTaskHandle = NULL;
TaskHandle_t displayTaskHandle = NULL;
TaskHandle_t commandTaskHandle = NULL;
TaskHandle_t statsTaskHandle = NULL;
QueueHandle_t frameQueue = NULL;
SemaphoreHandle_t displayMutex = NULL;

// Shared state (protected by mutex when needed)
AppMode currentMode = MODE_GRAYSCALE;
uint8_t threshold = 128;
uint32_t frameCount = 0;
uint32_t lastFrameTime = 0;
uint32_t lastModeSwitch = 0;
bool ledState = false;

// Frame buffer structure for the queue
struct FrameBuffer {
    uint8_t *buffer;
    size_t size;
    bool isBinary;
    uint32_t timestamp;
};

// Function prototypes
void captureTask(void *parameter);
void displayTask(void *parameter);
void commandTask(void *parameter);
void statsTask(void *parameter);
void displayFrame(uint8_t* buffer, bool isBinary);
void displayStats();
void displayModeInfo(const char* modeName);
void setProcessingMode(AppMode mode);
bool setupCamera();
bool setupDisplay();

void setup() {
    // Initialize serial port
    Serial.begin(115200);
    delay(500);
    
    Serial.println("\n\nXIAO ESP32S3 FreeRTOS Camera Application");
    Serial.println("=========================================");
    
    // Set up status LED
    pinMode(STATUS_LED, OUTPUT);
    digitalWrite(STATUS_LED, HIGH); // LED on during initialization
    
    // Initialize display
    Serial.println("Initializing display...");
    bool displayOk = setupDisplay();
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
    display.drawString(0, 25, "FreeRTOS Camera");
    display.drawString(0, 40, "Initializing...");
    display.update();
    delay(1000);
    
    // Initialize camera
    Serial.println("Initializing camera...");
    bool cameraOk = setupCamera();
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
    
    // Create FreeRTOS synchronization primitives
    displayMutex = xSemaphoreCreateMutex();
    frameQueue = xQueueCreate(2, sizeof(FrameBuffer)); // Buffer for 2 frames
    
    if (!displayMutex || !frameQueue) {
        Serial.println("Failed to create FreeRTOS synchronization primitives!");
        while(1) {
            digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));
            delay(100);
        }
    }
    
    // Create FreeRTOS tasks
    // Higher number = higher priority
    xTaskCreatePinnedToCore(
        captureTask,           // Task function
        "CaptureTask",         // Task name
        4096,                  // Stack size (bytes)
        NULL,                  // Parameters
        3,                     // Priority (1-24, higher = higher priority)
        &captureTaskHandle,    // Task handle
        0                      // Core (0 or 1)
    );
    
    xTaskCreatePinnedToCore(
        displayTask,           // Task function
        "DisplayTask",         // Task name
        4096,                  // Stack size (bytes)
        NULL,                  // Parameters
        2,                     // Priority
        &displayTaskHandle,    // Task handle
        1                      // Core (using core 1 to balance workload)
    );
    
    xTaskCreatePinnedToCore(
        commandTask,           // Task function
        "CommandTask",         // Task name
        2048,                  // Stack size (bytes)
        NULL,                  // Parameters
        1,                     // Priority (lower than camera/display)
        &commandTaskHandle,    // Task handle
        1                      // Core
    );
    
    xTaskCreatePinnedToCore(
        statsTask,             // Task function
        "StatsTask",           // Task name
        4096,                  // Stack size (bytes)
        NULL,                  // Parameters
        1,                     // Priority (lower than camera/display)
        &statsTaskHandle,      // Task handle
        1                      // Core
    );
    
    // Turn off LED to indicate ready state
    digitalWrite(STATUS_LED, LOW);
    
    // Display startup message
    display.clear();
    display.drawString(0, 12, "Camera Ready");
    display.drawString(0, 25, "FreeRTOS Active");
    display.drawString(0, 40, "4 Tasks Running");
    display.update();
    
    // Wait a moment before starting
    delay(1000);
    
    // Setup complete - FreeRTOS scheduler now running tasks
}

void loop() {
    // Empty loop - all work is done in FreeRTOS tasks
    // Arduino's loop function is still required though
    vTaskDelay(1000 / portTICK_PERIOD_MS); // Yield to other tasks
}

// Camera capture task - highest priority
void captureTask(void *parameter) {
    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t captureFrequency = pdMS_TO_TICKS(50); // 20fps target
    
    Serial.println("Capture task started on core " + String(xPortGetCoreID()));
    
    while (true) {
        // Toggle LED to show activity
        ledState = !ledState;
        digitalWrite(STATUS_LED, ledState);
        
        // Get current mode (no need for mutex since reading is atomic)
        AppMode mode = currentMode;
        
        // Capture a frame
        camera_fb_t* fb = camera.captureFrame();
        if (fb) {
            // Update frame counter
            frameCount++;
            lastFrameTime = millis();
            
            // Set the processing mode based on the current app mode
            setProcessingMode(mode);
            
            // Process the frame
            camera.processFrame(fb, BUFFER_WIDTH, BUFFER_HEIGHT);
            
            // Get the processed buffer
            uint8_t* processedBuffer = camera.getProcessedBuffer();
            
            // Create a copy of the buffer to send to the display task
            uint8_t* bufferCopy = NULL;
            
            if (mode != MODE_STATS) {
                // For buffer safety, make a copy before sending to the display task
                if (USE_PSRAM && psramFound()) {
                    bufferCopy = (uint8_t*)ps_malloc(FRAME_BUFFER_SIZE);
                } else {
                    bufferCopy = (uint8_t*)malloc(FRAME_BUFFER_SIZE);
                }
                
                if (bufferCopy) {
                    // Copy the processed data
                    memcpy(bufferCopy, processedBuffer, FRAME_BUFFER_SIZE);
                    
                    // Create frame buffer structure
                    FrameBuffer frame;
                    frame.buffer = bufferCopy;
                    frame.size = FRAME_BUFFER_SIZE;
                    frame.isBinary = (mode != MODE_GRAYSCALE);
                    frame.timestamp = millis();
                    
                    // Send to display task (wait up to 10ms, then discard if queue full)
                    if (xQueueSend(frameQueue, &frame, pdMS_TO_TICKS(10)) != pdPASS) {
                        // Queue full, free the buffer
                        free(bufferCopy);
                    }
                }
            }
            
            // Return the frame buffer to the camera
            esp_camera_fb_return(fb);
        }
        
        // Wait for the next capture cycle using vTaskDelayUntil for consistent timing
        vTaskDelayUntil(&lastWakeTime, captureFrequency);
    }
}

// Display task
void displayTask(void *parameter) {
    FrameBuffer frame;
    TickType_t lastStatsTime = 0;
    
    Serial.println("Display task started on core " + String(xPortGetCoreID()));
    
    while (true) {
        // Check if we should display stats
        if (currentMode == MODE_STATS) {
            // Only update stats every 500ms to prevent flicker
            if (xTaskGetTickCount() - lastStatsTime > pdMS_TO_TICKS(500)) {
                displayStats();
                lastStatsTime = xTaskGetTickCount();
            }
            // Wait a bit before checking again
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        
        // Wait for a frame from the capture task
        if (xQueueReceive(frameQueue, &frame, pdMS_TO_TICKS(100)) == pdPASS) {
            // Take mutex to access display
            if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                // Display the frame
                displayFrame(frame.buffer, frame.isBinary);
                
                // Release mutex
                xSemaphoreGive(displayMutex);
            }
            
            // Free the buffer
            free(frame.buffer);
        }
    }
}

// Serial command handling task
void commandTask(void *parameter) {
    Serial.println("Command task started on core " + String(xPortGetCoreID()));
    
    while (true) {
        if (Serial.available()) {
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
            }
        }
        
        // Small delay to prevent hogging CPU
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// Statistics monitoring task
void statsTask(void *parameter) {
    TickType_t lastPrintTime = 0;
    const TickType_t printFrequency = pdMS_TO_TICKS(5000); // Print every 5 seconds
    
    Serial.println("Stats task started on core " + String(xPortGetCoreID()));
    
    while (true) {
        // Print system stats periodically
        if (xTaskGetTickCount() - lastPrintTime > printFrequency) {
            lastPrintTime = xTaskGetTickCount();
            
            // Get camera statistics
            camera_stats_t stats = camera.getStats();
            
            // Log system stats
            Serial.println("\n--- System Statistics ---");
            Serial.printf("Free heap: %lu bytes\n", ESP.getFreeHeap());
            Serial.printf("PSRAM: %s, Free: %lu bytes\n", 
                psramFound() ? "Available" : "Not available",
                ESP.getFreePsram());
                
            // Print task statistics
            Serial.println("\n--- Task Statistics ---");
            Serial.printf("Capture Task: High water mark: %u bytes\n", 
                uxTaskGetStackHighWaterMark(captureTaskHandle));
            Serial.printf("Display Task: High water mark: %u bytes\n", 
                uxTaskGetStackHighWaterMark(displayTaskHandle));
            Serial.printf("Command Task: High water mark: %u bytes\n", 
                uxTaskGetStackHighWaterMark(commandTaskHandle));
            Serial.printf("Stats Task: High water mark: %u bytes\n", 
                uxTaskGetStackHighWaterMark(statsTaskHandle));
            
            // Print camera statistics
            Serial.println("\n--- Camera Statistics ---");
            Serial.printf("Frames: %lu (%.1f FPS)\n", stats.frame_count, stats.avg_fps);
            Serial.printf("Avg processing: %.1f ms\n", stats.avg_processing_time);
            Serial.printf("Dropped frames: %lu\n", stats.frame_drops);
            Serial.printf("Memory usage: %lu bytes\n", stats.memory_usage);
            Serial.println("-----------------------");
        }
        
        // Yield to other tasks
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
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
    // Take mutex to access display
    if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
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
        
        // Release mutex
        xSemaphoreGive(displayMutex);
    }
}

void displayModeInfo(const char* modeName) {
    // Take mutex to access display
    if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // Show mode change on display
        display.clear();
        display.drawString(0, 20, "Mode Changed:");
        display.drawString(0, 35, modeName);
        display.update();
        
        // Release mutex
        xSemaphoreGive(displayMutex);
    }
    
    // Print to serial
    Serial.print("Mode changed to: ");
    Serial.println(modeName);
}

void setProcessingMode(AppMode mode) {
    switch (mode) {
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
}

bool setupCamera() {
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
    return camera.begin(config);
}

bool setupDisplay() {
    return display.begin();
}
