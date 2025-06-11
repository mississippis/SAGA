#include <Arduino.h>
#include <U8g2lib.h>
#include <SPI.h>
#include "test_patterns.h"

// Pin definitions for XIAO ESP32S3
#define RST_PIN   2  // XIAO_D1 (GPIO2)
#define DC_PIN    3  // XIAO_D2 (GPIO3)
#define CS_PIN    4  // XIAO_D3 (GPIO4)

// SPI speed (default 8 MHz, can be changed via serial)
uint32_t spiSpeedHz = 8000000;

// U8g2 constructor for ST7565P 128x64 display
U8G2_ST7565_NHD_C12864_F_4W_HW_SPI u8g2(U8G2_R0, CS_PIN, DC_PIN, RST_PIN);

// Pattern selection
const char* patternNames[] = {"Horizontal Grad", "Vertical Grad", "Radial Grad", "Checkerboard", "Sine Wave", "X Shape"};
const int numPatterns = 6;
int currentPattern = 0;
int checkerboardSize = 8;
int contrast = 32; // Default contrast
unsigned long frameCount = 0;
unsigned long lastFpsMillis = 0;
float fps = 0;

void crystalfontzInit() {
  // Crystalfontz-style robust reset/init
  pinMode(RST_PIN, OUTPUT);
  digitalWrite(RST_PIN, LOW);
  delay(50);
  digitalWrite(RST_PIN, HIGH);
  delay(100);
  // U8g2 handles most init, but we can add contrast/LUT setup if needed
}

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  Serial.println("XIAO ESP32S3 + ST7565P Robust Test");

  // Explicitly set SPI speed
  SPI.begin();
  SPI.beginTransaction(SPISettings(spiSpeedHz, MSBFIRST, SPI_MODE0));

  crystalfontzInit();
  u8g2.begin();
  u8g2.setContrast(contrast);
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 10, "ST7565P Display Test");
  u8g2.sendBuffer();
  delay(500);
  Serial.println("Commands: [0-5]=pattern, c/C=contrast, s/S=SPI MHz, n=next, p=prev, h=help");
  lastFpsMillis = millis();
}

void drawPattern(int patternId) {
  int w = u8g2.getDisplayWidth();
  int h = u8g2.getDisplayHeight();
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      uint8_t val = 0;
      switch (patternId) {
        case 0: val = horizontalGradient(x, y, w, h); break;
        case 1: val = verticalGradient(x, y, w, h); break;
        case 2: val = radialGradient(x, y, w, h); break;
        case 3: val = checkerboard(x, y, w, h, checkerboardSize); break;
        case 4: val = sineWave(x, y, w, h); break;
        case 5: val = textX(x, y, w, h); break;
      }
      // Map 8-bit to 1bpp for ST7565P
      if (val > 127) u8g2.drawPixel(x, y);
    }
  }
}

void handleSerial() {
  while (Serial.available()) {
    char cmd = Serial.read();
    if (cmd >= '0' && cmd <= '5') {
      currentPattern = cmd - '0';
      Serial.printf("Pattern: %s\n", patternNames[currentPattern]);
    } else if (cmd == 'n') {
      currentPattern = (currentPattern + 1) % numPatterns;
      Serial.printf("Pattern: %s\n", patternNames[currentPattern]);
    } else if (cmd == 'p') {
      currentPattern = (currentPattern - 1 + numPatterns) % numPatterns;
      Serial.printf("Pattern: %s\n", patternNames[currentPattern]);
    } else if (cmd == 'c') {
      contrast += 4; if (contrast > 63) contrast = 63;
      u8g2.setContrast(contrast);
      Serial.printf("Contrast: %d\n", contrast);
    } else if (cmd == 'C') {
      contrast -= 4; if (contrast < 0) contrast = 0;
      u8g2.setContrast(contrast);
      Serial.printf("Contrast: %d\n", contrast);
    } else if (cmd == 's') {
      spiSpeedHz += 1000000; if (spiSpeedHz > 12000000) spiSpeedHz = 12000000;
      SPI.beginTransaction(SPISettings(spiSpeedHz, MSBFIRST, SPI_MODE0));
      Serial.printf("SPI speed: %lu Hz\n", spiSpeedHz);
    } else if (cmd == 'S') {
      spiSpeedHz -= 1000000; if (spiSpeedHz < 1000000) spiSpeedHz = 1000000;
      SPI.beginTransaction(SPISettings(spiSpeedHz, MSBFIRST, SPI_MODE0));
      Serial.printf("SPI speed: %lu Hz\n", spiSpeedHz);
    } else if (cmd == 'h') {
      Serial.println("Commands: [0-5]=pattern, n=next, p=prev, c/C=contrast, s/S=SPI MHz, h=help");
    }
  }
}

void loop() {
  handleSerial();
  u8g2.clearBuffer();
  drawPattern(currentPattern);
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.setDrawColor(1);
  char buf[32];
  sprintf(buf, "%s", patternNames[currentPattern]);
  u8g2.drawStr(0, 8, buf);
  sprintf(buf, "Contrast:%d", contrast);
  u8g2.drawStr(0, 16, buf);
  sprintf(buf, "SPI:%.1fMHz", spiSpeedHz/1e6);
  u8g2.drawStr(0, 24, buf);
  frameCount++;
  unsigned long now = millis();
  if (now - lastFpsMillis >= 1000) {
    fps = frameCount * 1000.0 / (now - lastFpsMillis);
    frameCount = 0;
    lastFpsMillis = now;
  }
  sprintf(buf, "FPS:%.1f", fps);
  u8g2.drawStr(0, 32, buf);
  u8g2.sendBuffer();
  delay(10);
}
