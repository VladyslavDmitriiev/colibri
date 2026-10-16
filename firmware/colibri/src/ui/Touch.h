// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "../../config.h"

// Capacitive touch panels on I2C, read by polling. A camera whose own SCCB driver takes the
// touch pins allows touchBegin() only while it is stopped; one that shares the open bus
// (CAMERA_GPIO_SIOD -1) leaves touch usable while scanning.
#if defined(TOUCH_FT5X06_I2C_ADDR)
  #define TOUCH_ENABLED
  #if defined(CAMERA_ENABLED) && CAMERA_GPIO_SIOD != -1 && CAMERA_GPIO_SIOD == TOUCH_GPIO_I2C_SDA
    #define TOUCH_BUS_TAKEN_BY_CAMERA
  #else
    // touch works on every screen: approvals, notices and scanning get on-screen buttons
    #define TOUCH_BUTTONS
  #endif

void touchBegin();
void touchEnd();
enum TouchState { TouchUp, TouchDown, TouchReleased };
// TouchDown while a finger is on the panel, TouchReleased once when it lifts.
// x, y: the latest point in display coordinates, for TouchReleased the last one before lifting.
TouchState touchRead(int16_t& x, int16_t& y);
// true once per press, when the finger lifts
bool touchTapped(int16_t& x, int16_t& y);

  #ifdef DEBUG_REMOTE_CONTROL
void touchSimulateTap(int16_t x, int16_t y);
// a finger held at x, y until the next simulated tap
void touchSimulateDown(int16_t x, int16_t y);
  #endif
#endif
