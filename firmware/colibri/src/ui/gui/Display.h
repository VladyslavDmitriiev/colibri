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

// touch screens
  #include <string>
  #include <vector>
enum KeyAction { KeyNone, KeyChar, KeyBackspace, KeyShift, KeySymbols, KeyLetters, KeyDone, KeyCancel, KeyToggleShow };
struct KeyboardKey {
  int32_t x, y, w, h;
  const char* label;  // nullptr: the character
  char ch;
  KeyAction action;
};
// layout: 0 lowercase, 1 uppercase, 2 symbols
void drawKeyboard(const char* title, const std::string& shown, uint8_t layout);
KeyAction keyboardHit(uint8_t layout, int16_t x, int16_t y, char& ch);
void drawMenu(const char* title, const std::vector<std::string>& options);
int menuHit(size_t count, int16_t x, int16_t y);
#ifdef DEBUG_REMOTE_CONTROL
void dumpDisplay();
#endif

#else

  #define initDisplay()
  #define drawText(...)
  #define drawQrCode(...)
  #define drawCameraPreview(...)

#endif
