#ifndef TEST_PATTERNS_H
#define TEST_PATTERNS_H

// Simple horizontal gradient pattern
uint8_t horizontalGradient(int x, int y, int width, int height) {
  return (x * 255) / width;
}

// Vertical gradient pattern
uint8_t verticalGradient(int x, int y, int width, int height) {
  return (y * 255) / height;
}

// Radial gradient from center
uint8_t radialGradient(int x, int y, int width, int height) {
  int centerX = width / 2;
  int centerY = height / 2;
  int maxDist = sqrt(centerX * centerX + centerY * centerY);
  int dist = sqrt((x - centerX) * (x - centerX) + (y - centerY) * (y - centerY));
  return ((maxDist - dist) * 255) / maxDist;
}

// Checkerboard pattern with variable size
uint8_t checkerboard(int x, int y, int width, int height, int size = 8) {
  return ((x / size) % 2 == (y / size) % 2) ? 255 : 0;
}

// Sine wave pattern
uint8_t sineWave(int x, int y, int width, int height) {
  float angle = (x * 4.0f * PI) / width;
  float amplitude = height / 3.0f;
  float centerY = height / 2.0f;
  float distFromSine = abs(y - (centerY + sin(angle) * amplitude));
  return (distFromSine < 3) ? 255 : 0;
}

// Text or shape rendering pattern (simple "X")
uint8_t textX(int x, int y, int width, int height) {
  // Create a simple X shape
  if (abs(x - y) < 3 || abs(x - (height - y)) < 3) {
    return 255;
  }
  return 0;
}

#endif // TEST_PATTERNS_H
