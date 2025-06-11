#include "Arduino.h"
#include "display_config.h"
#include "CFADisplay.h"

// Global display object
CFADisplay display;

void setup() {
    Serial.begin(115200);
    delay(1000); // Wait for serial to connect
    Serial.println("\nXiao Display Test Starting...");
    
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
    
    // Test display
    display.clear();
    display.drawString(0, 20, "Display Test");  // U8G2 Y position is baseline of text
    display.drawLine(0, 30, 127, 30);  // Draw a line across the screen
    display.drawRect(10, 35, 30, 20);  // Draw a rectangle
    display.update();
    
    Serial.println("Test pattern drawn to display");
}

void loop() {
    // Basic test loop
    static uint32_t lastTime = 0;
    static int counter = 0;
    
    if (millis() - lastTime > 1000) {  // Update every second
        lastTime = millis();
        counter++;
        
        display.clear();
        
        // Display counter and timestamp
        char buffer[32];
        sprintf(buffer, "Count: %d", counter);
        display.drawString(0, 15, buffer);  // Y position is baseline of text
        
        sprintf(buffer, "Time: %lu ms", millis());
        display.drawString(0, 30, buffer);
        
        // Draw a moving pattern
        int x = (counter * 5) % display.getWidth();
        display.drawLine(x, 35, x+20, 55);
        
        display.update();
        
        // Print to serial for debugging
        if (counter % 5 == 0) {
            Serial.print("Counter: ");
            Serial.println(counter);
        }
    }
}
