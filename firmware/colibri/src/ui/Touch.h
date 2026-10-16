// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "../../config.h"

// Capacitive touch panels on I2C, read by polling. Boards whose camera shares the bus
// call touchBegin() only while the camera is stopped.
#if defined(TOUCH_FT5X06_I2C_ADDR)
  #define TOUCH_ENABLED

void touchBegin();
void touchEnd();
// true once per press, with the point in display coordinates
bool touchTapped(int16_t& x, int16_t& y);

  #ifdef DEBUG_REMOTE_CONTROL
void touchSimulateTap(int16_t x, int16_t y);
  #endif
#endif
