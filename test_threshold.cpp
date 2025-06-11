#include <Arduino.h>
#include <unity.h>

extern uint8_t IRAM_ATTR calculateAdaptiveThreshold(const uint8_t* src, size_t w, size_t h);

void setUp(void) {
  // Set a known brightnessThreshold
  extern uint8_t brightnessThreshold;
  brightnessThreshold = 150;
}

void tearDown(void) {
  // nothing to do
}

void test_threshold_all_same(void) {
  uint8_t img[16];
  memset(img, 100, sizeof(img));
  uint8_t t = calculateAdaptiveThreshold(img, 4, 4);
  // mean=100, offset=150-128=22, expected=122
  TEST_ASSERT_EQUAL_UINT8(122, t);
}

void test_threshold_varied(void) {
  uint8_t img[8] = {0, 50, 100, 150, 200, 250, 255, 128};
  // samples at idx 0 and 4: (0 + 200)/2 = 100, offset=22 => 122
  uint8_t t = calculateAdaptiveThreshold(img, 8, 1);
  TEST_ASSERT_EQUAL_UINT8(122, t);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_threshold_all_same);
  RUN_TEST(test_threshold_varied);
  UNITY_END();
  return 0;
}