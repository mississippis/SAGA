// Nicla Vision + CFA10110 Display Demo (Direct SPI)
// Shows: checkerboard, gradient, animated bar
// No external libraries required
// Uses pinout from BoardConfig.h
#include <Arduino.h>
#include <SPI.h>
#include "BoardConfig.h"

SPISettings spiSettings(SPI_FREQUENCY, MSBFIRST, SPI_MODE0);

void setupPins() {
    pinMode(LCD_CS_PIN, OUTPUT);
    pinMode(LCD_DC_PIN, OUTPUT);
    pinMode(LCD_RES_PIN, OUTPUT);
    digitalWrite(LCD_CS_PIN, HIGH);
    digitalWrite(LCD_DC_PIN, LOW);
    digitalWrite(LCD_RES_PIN, HIGH);
}

void sendCommand(uint8_t cmd) {
    SPI.beginTransaction(spiSettings);
    digitalWrite(LCD_DC_PIN, LOW);
    digitalWrite(LCD_CS_PIN, LOW);
    SPI.transfer(cmd);
    digitalWrite(LCD_CS_PIN, HIGH);
    SPI.endTransaction();
}

void sendData(uint8_t data) {
    SPI.beginTransaction(spiSettings);
    digitalWrite(LCD_DC_PIN, HIGH);
    digitalWrite(LCD_CS_PIN, LOW);
    SPI.transfer(data);
    digitalWrite(LCD_CS_PIN, HIGH);
    SPI.endTransaction();
}

void hardwareReset() {
    digitalWrite(LCD_RES_PIN, HIGH);
    delay(50);
    digitalWrite(LCD_RES_PIN, LOW);
    delay(200);
    digitalWrite(LCD_RES_PIN, HIGH);
    delay(300);
}

void initDisplay() {
    hardwareReset();
    sendCommand(0x30); sendCommand(0x6E);
    sendCommand(0x31); sendCommand(0xD7); sendData(0x9F);
    sendCommand(0xE0); sendData(0x00); delay(1);
    sendCommand(0xE3); delay(2);
    sendCommand(0xE1);
    sendCommand(0x30); sendCommand(0x94); sendCommand(0xAE); delay(5);
    sendCommand(0x20); sendData(0x0B);
    sendCommand(0x81); sendData(0x2A); sendData(0x04);
    sendCommand(0x31); sendCommand(0x20);
    for (int i = 0; i < 16; ++i) sendData(0x00);
    sendCommand(0x32); sendData(0x00); sendData(0x01); sendData(0x02);
    sendCommand(0x51); sendData(0xFB);
    sendCommand(0xF0); sendData(0x02); sendData(0x08); sendData(0x0F); sendData(0x18);
    sendCommand(0x30); sendCommand(0xF0); sendData(0x11);
    sendCommand(0xCA); sendData(0x00); sendData(0x7F); sendData(0x00);
    sendCommand(0xBC); sendData(0x00);
    sendCommand(0xA6);
    sendCommand(0x31); sendCommand(0x40);
    sendCommand(0x30); sendCommand(0x15); sendData(0x00); sendData(0xEF);
    sendCommand(0x75); sendData(0x00); sendData(0x10);
    sendCommand(0x30); sendCommand(0xAF);
}

void fillDisplay(uint8_t pattern) {
    sendCommand(0x30); sendCommand(0xF0); sendData(0x11); sendCommand(0x5C);
    uint32_t pixels = (uint32_t)DISPLAY_WIDTH * DISPLAY_HEIGHT / 4;
    for (uint32_t i = 0; i < pixels; i++) sendData(pattern);
}

void drawGradient() {
    sendCommand(0x30); sendCommand(0xF0); sendData(0x11); sendCommand(0x5C);
    for (int y = 0; y < DISPLAY_HEIGHT; y++) {
        for (int x = 0; x < DISPLAY_WIDTH; x += 4) {
            uint8_t level = (x * 4) / DISPLAY_WIDTH;
            uint8_t val = (level == 0 ? 0x00 : level == 1 ? 0x55 : level == 2 ? 0xAA : 0xFF);
            sendData(val);
        }
    }
}

void animateBar(int frame) {
    sendCommand(0x30); sendCommand(0xF0); sendData(0x11); sendCommand(0x5C);
    int barX = frame % DISPLAY_WIDTH;
    for (int y = 0; y < DISPLAY_HEIGHT; y++) {
        for (int x = 0; x < DISPLAY_WIDTH; x += 4) {
            uint8_t val = ((x <= barX && barX < x + 4) ? 0xFF : 0x00);
            sendData(val);
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    setupPins();
    SPI.begin();
    initDisplay();
}

void loop() {
    static uint8_t mode = 0;
    static unsigned long last = 0;
    unsigned long now = millis();
    if (now - last > 1500) {
        mode = (mode + 1) % 3;
        Serial.print("Display mode: "); Serial.println(mode);
        if (mode == 0) fillDisplay(0x55);
        else if (mode == 1) drawGradient();
        else animateBar(now / 50);
        last = now;
    }
}