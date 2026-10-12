// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "../../../config.h"
#include "../../../constants.h"

#ifdef DISPLAY_ENABLED
  #include <qrcode.h>

  #include "PolymorphicLGFX.h"

void initDisplay();
void drawText(const char* message);
void drawQrCode(esp_qrcode_handle_t qrcode);
void drawCameraPreview(const uint8_t* gray, size_t width, size_t height, const char* status);

#else

  #define initDisplay()
  #define drawText(...)
  #define drawQrCode(...)
  #define drawCameraPreview(...)

#endif
