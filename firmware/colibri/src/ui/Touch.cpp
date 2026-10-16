// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "Touch.h"

#ifdef TOUCH_ENABLED

  #include <Wire.h>

  #ifndef TOUCH_I2C_FREQ
    #define TOUCH_I2C_FREQ 400000
  #endif

static bool isActive = false;
static bool wasPressed = false;
  #ifdef DEBUG_REMOTE_CONTROL
static bool hasSimulatedTap = false;
static int16_t simulatedX, simulatedY;
  #endif

void touchBegin() {
  if (isActive) return;
  // Wire.begin() is a no-op while Wire thinks it still owns the pins
  Wire.end();
  Wire.begin(TOUCH_GPIO_I2C_SDA, TOUCH_GPIO_I2C_SCL, TOUCH_I2C_FREQ);
  isActive = true;
  wasPressed = true;  // ignore a finger that is already down
}

void touchEnd() {
  if (!isActive) return;
  Wire.end();
  isActive = false;
}

// FT5x06 family (FT6336 etc.): 0x02 number of points, 0x03..0x06 first point
static bool readPoint(int16_t& x, int16_t& y) {
  Wire.beginTransmission(TOUCH_FT5X06_I2C_ADDR);
  Wire.write(0x02);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)TOUCH_FT5X06_I2C_ADDR, (uint8_t)5) != 5) return false;
  uint8_t data[5];
  for (uint8_t& b : data) b = Wire.read();
  uint8_t points = data[0] & 0x0f;
  if (points == 0 || points > 2) return false;
  x = ((data[1] & 0x0f) << 8) | data[2];
  y = ((data[3] & 0x0f) << 8) | data[4];
  return x < DISPLAY_WIDTH && y < DISPLAY_HEIGHT;
}

bool touchTapped(int16_t& x, int16_t& y) {
  #ifdef DEBUG_REMOTE_CONTROL
  if (hasSimulatedTap) {
    hasSimulatedTap = false;
    x = simulatedX;
    y = simulatedY;
    return true;
  }
  #endif
  if (!isActive) return false;
  int16_t px, py;
  bool pressed = readPoint(px, py);
  bool tapped = pressed && !wasPressed;
  wasPressed = pressed;
  if (tapped) {
    x = px;
    y = py;
    log_d("Touch at %d,%d", x, y);
  }
  return tapped;
}

  #ifdef DEBUG_REMOTE_CONTROL
void touchSimulateTap(int16_t x, int16_t y) {
  simulatedX = x;
  simulatedY = y;
  hasSimulatedTap = true;
}
  #endif

#endif
