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
static bool ignoreUntilUp = false;
static int16_t lastX, lastY;
  #ifdef DEBUG_REMOTE_CONTROL
static bool hasSimulatedTap = false;
static bool hasSimulatedDown = false;
static int16_t simulatedX, simulatedY;
  #endif

void touchBegin() {
  if (isActive) return;
    #ifdef TOUCH_BUS_TAKEN_BY_CAMERA
  // Wire.begin() is a no-op while Wire thinks it still owns the pins
  Wire.end();
  #endif
  // with a shared bus it stays open for the camera's SCCB, begin() is a no-op then
  Wire.begin(TOUCH_GPIO_I2C_SDA, TOUCH_GPIO_I2C_SCL, TOUCH_I2C_FREQ);
  isActive = true;
  wasPressed = false;
  ignoreUntilUp = true;  // a finger that is already down does not count
}

void touchEnd() {
  #ifdef TOUCH_BUTTONS
  return;  // nothing else needs the bus to itself
  #endif
  if (!isActive) return;
  #ifdef TOUCH_BUS_TAKEN_BY_CAMERA
  Wire.end();  // the camera's own SCCB driver takes the pins
  #endif
  isActive = false;
}

enum PointRead { PointNone, PointDown, PointError };

// FT5x06 family (FT6336 etc.): 0x02 number of points, 0x03..0x06 first point
static PointRead readPoint(int16_t& x, int16_t& y) {
  Wire.beginTransmission(TOUCH_FT5X06_I2C_ADDR);
  Wire.write(0x02);
  if (Wire.endTransmission(false) != 0) return PointError;
  if (Wire.requestFrom((uint8_t)TOUCH_FT5X06_I2C_ADDR, (uint8_t)5) != 5) return PointError;
  uint8_t data[5];
  for (uint8_t& b : data) b = Wire.read();
  uint8_t points = data[0] & 0x0f;
  if (points == 0) return PointNone;
  if (points > 2) return PointError;
  x = ((data[1] & 0x0f) << 8) | data[2];
  y = ((data[3] & 0x0f) << 8) | data[4];
  return x < DISPLAY_WIDTH && y < DISPLAY_HEIGHT ? PointDown : PointError;
}

// The first report after touchdown is the least accurate one, so a tap counts where the
// finger lifts, and screens can highlight what is under the finger until then.
TouchState touchRead(int16_t& x, int16_t& y) {
  #ifdef DEBUG_REMOTE_CONTROL
  if (hasSimulatedTap) {
    hasSimulatedTap = false;
    x = simulatedX;
    y = simulatedY;
    return TouchReleased;
  }
  if (hasSimulatedDown) {
    x = simulatedX;
    y = simulatedY;
    return TouchDown;
  }
  #endif
  if (!isActive) return TouchUp;
  int16_t px, py;
  PointRead read = readPoint(px, py);
  if (read == PointError) {
    // a glitch on the shared bus neither presses nor lifts
    x = lastX;
    y = lastY;
    return wasPressed ? TouchDown : TouchUp;
  }
  if (ignoreUntilUp) {
    if (read == PointNone) ignoreUntilUp = false;
    return TouchUp;
  }
  if (read == PointDown) {
    lastX = x = px;
    lastY = y = py;
    wasPressed = true;
    return TouchDown;
  }
  x = lastX;
  y = lastY;
  if (!wasPressed) return TouchUp;
  wasPressed = false;
  log_d("Touch released at %d,%d", x, y);
  return TouchReleased;
}

bool touchTapped(int16_t& x, int16_t& y) {
  return touchRead(x, y) == TouchReleased;
}

  #ifdef DEBUG_REMOTE_CONTROL
void touchSimulateTap(int16_t x, int16_t y) {
  simulatedX = x;
  simulatedY = y;
  hasSimulatedTap = true;
  hasSimulatedDown = false;
}

void touchSimulateDown(int16_t x, int16_t y) {
  simulatedX = x;
  simulatedY = y;
  hasSimulatedDown = true;
}
  #endif

#endif
