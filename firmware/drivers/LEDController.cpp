#include "LEDController.h"

LEDController::LEDController(uint16_t count, uint8_t pin)
  : strip(count, pin, NEO_GRB + NEO_KHZ800) {}

void LEDController::begin(uint8_t brightness) {
  strip.begin();
  strip.setBrightness(brightness);
  strip.clear();
  strip.show();
}

void LEDController::setPixel(uint16_t idx, uint8_t r, uint8_t g, uint8_t b) {
  if (idx < strip.numPixels()) {
    strip.setPixelColor(idx, strip.Color(r, g, b));
  }
}

void LEDController::clearPixel(uint16_t idx) {
  if (idx < strip.numPixels()) {
    strip.setPixelColor(idx, 0);
  }
}

void LEDController::clearAll() {
  strip.clear();
}

void LEDController::show() {
  strip.show();
}
