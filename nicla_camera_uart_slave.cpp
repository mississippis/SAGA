#include <Arduino.h>
#include <Camera.h>

// UART config for Nicla Vision
#define UART_PORT    Serial2  // Use Serial2 (pins 7/8 on Nicla)
#define UART_BAUD    2000000

// Display targets
#define DISPLAY_ST7565P   1
#define DISPLAY_ST75256   2

// Camera image size
#define IMAGE_WIDTH   320
#define IMAGE_HEIGHT  240

uint8_t displayTarget = DISPLAY_ST7565P;
Camera cam;
CameraImage img;
uint8_t *processedImage = nullptr;
size_t processedSize = 0;

void setupCamera() {
  if (!cam.begin(CAMERA_R320x240, 30, 1)) {
    Serial.println("Camera init failed");
    while (1);
  }
  cam.setFrameRate(20);
  cam.setCameraBrightness(2);
  cam.setCameraContrast(2);
  cam.setCameraGain(2);
}

void processImageForDisplay() {
  if (displayTarget == DISPLAY_ST7565P) {
    if (processedSize != 128*64/8) {
      free(processedImage);
      processedImage = (uint8_t *)malloc(128*64/8);
      processedSize = 128*64/8;
    }
    memset(processedImage, 0, processedSize);
    for (int y = 0; y < 64; y++) {
      for (int x = 0; x < 128; x++) {
        int srcX = (x * IMAGE_WIDTH / 128);
        int srcY = (y * IMAGE_HEIGHT / 64);
        uint8_t *pixelPtr = img.getPixelAddress(srcX, srcY);
        uint16_t pixelValue = (pixelPtr[0] << 8) | pixelPtr[1];
        uint8_t r = ((pixelValue >> 11) & 0x1F) << 3;
        uint8_t g = ((pixelValue >> 5) & 0x3F) << 2;
        uint8_t b = (pixelValue & 0x1F) << 3;
        uint8_t gray = (r + g + g + b) >> 2;
        if (gray > 128) {
          processedImage[y * 16 + (x / 8)] |= (1 << (7 - (x % 8)));
        }
      }
    }
  } else if (displayTarget == DISPLAY_ST75256) {
    if (processedSize != 240*128/4) {
      free(processedImage);
      processedImage = (uint8_t *)malloc(240*128/4);
      processedSize = 240*128/4;
    }
    memset(processedImage, 0, processedSize);
    for (int y = 0; y < 128; y++) {
      for (int x = 0; x < 240; x++) {
        int srcX = (x * IMAGE_WIDTH / 240);
        int srcY = (y * IMAGE_HEIGHT / 128);
        uint8_t *pixelPtr = img.getPixelAddress(srcX, srcY);
        uint16_t pixelValue = (pixelPtr[0] << 8) | pixelPtr[1];
        uint8_t r = ((pixelValue >> 11) & 0x1F) << 3;
        uint8_t g = ((pixelValue >> 5) & 0x3F) << 2;
        uint8_t b = (pixelValue & 0x1F) << 3;
        uint8_t gray = (r + g + g + b) >> 2;
        uint8_t grayLevel = gray >> 6;
        int bytePos = (y * 240 + x) / 4;
        int bitPos = ((y * 240 + x) % 4) * 2;
        processedImage[bytePos] |= (grayLevel << (6 - bitPos));
      }
    }
  }
}

void sendImageUART() {
  UART_PORT.write((uint8_t)(processedSize >> 24));
  UART_PORT.write((uint8_t)(processedSize >> 16));
  UART_PORT.write((uint8_t)(processedSize >> 8));
  UART_PORT.write((uint8_t)(processedSize));
  UART_PORT.write(processedImage, processedSize);
}

void setup() {
  Serial.begin(115200);
  UART_PORT.begin(UART_BAUD);
  setupCamera();
  Serial.println("Nicla UART Camera Slave ready");
}

void loop() {
  // Wait for a command from Teensy
  if (UART_PORT.available()) {
    uint8_t cmd = UART_PORT.read();
    if (cmd == 0xA5) { // 0xA5 = capture and send
      if (cam.grabFrame(img, 1000) == 0) {
        processImageForDisplay();
        sendImageUART();
      }
    } else if (cmd == 0xB1) { // 0xB1 = set display target
      while (!UART_PORT.available());
      displayTarget = UART_PORT.read();
    }
  }
}
