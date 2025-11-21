#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include <Adafruit_NeoPixel.h>

class LEDController {
private:
  Adafruit_NeoPixel strip;

public:
  LEDController(uint16_t count, uint8_t pin);
  void begin(uint8_t brightness);
  void setPixel(uint16_t idx, uint8_t r, uint8_t g, uint8_t b);
  void clearPixel(uint16_t idx);
  void clearAll();
  void show();
};

#endif
