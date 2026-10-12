// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "Display.h"

#include <vector>

#ifdef DISPLAY_ENABLED

static TFT_eSPI display;
// static TFT_eSprite sprite(&display);

// Splits text at newlines and wraps lines longer than `maxChars`
static std::vector<std::string> wrapText(const char* message, size_t maxChars) {
  std::vector<std::string> lines;
  std::string line;
  for (const char* c = message;; c++) {
    if (*c == '\n' || *c == '\0') {
      lines.push_back(line);
      line.clear();
      if (*c == '\0') break;
      continue;
    }
    if (line.size() >= maxChars) {
      lines.push_back(line);
      line.clear();
    }
    line += *c;
  }
  return lines;
}

// Centered text, multiple lines separated by `\n`. Long lines wrap, and the text
// falls back to the small font size if it doesn't fit the frame.
void drawText(const char* message) {
  display.startWrite();
  display.clearDisplay();
  display.fillScreen(DISPLAY_COLOR_BG);
  display.setTextColor(DISPLAY_COLOR_TEXT);

  display.setFont(&fonts::Font0);
  float textSize = 1.0f;
  #ifndef DISPLAY_SMALL
  textSize = 2.0f;
  #endif

  const int32_t padding = 10;
  std::vector<std::string> lines;
  while (true) {
    display.setTextSize(textSize);
    size_t maxChars = (display.width() - 2 * padding) / display.textWidth("0");
    lines = wrapText(message, maxChars > 0 ? maxChars : 1);
    if (textSize <= 1.0f || (int32_t)lines.size() * display.fontHeight() <= display.height() - 2 * padding) break;
    textSize = 1.0f;
  }

  int32_t lineHeight = display.fontHeight();
  int32_t y = (display.height() - (int32_t)lines.size() * lineHeight) / 2 + lineHeight / 2;
  display.setTextDatum(middle_center);
  for (const std::string& line : lines) {
    display.drawString(line.c_str(), display.width() / 2, y);
    y += lineHeight;
  }

  display.drawRect(5, 5, display.width() - 10, display.height() - 10, DISPLAY_COLOR_TEXT);
  display.endWrite();
}

void initDisplay() {
  display.setRotation(DISPLAY_ROTATION);
  display.setBrightness(DISPLAY_BRIGHTNESS);
  display.setColorDepth(DISPLAY_COLOR_DEPTH);
  display.begin();

  drawText(DISPLAY_SPLASH);
  log_d("Initialized display: %s", DISPLAY_TYPE);
  delay(500);
}

// Downscaled grayscale camera frame at the top, a status line below, for aiming at QR codes
void drawCameraPreview(const uint8_t* gray, size_t width, size_t height, const char* status) {
  size_t step = (width + display.width() - 1) / display.width();
  if (step < 1) step = 1;
  size_t w = width / step;
  size_t h = height / step;
  int32_t x0 = (display.width() - w) / 2;

  static lgfx::rgb565_t* line = nullptr;
  static size_t lineWidth = 0;
  if (lineWidth < w) {
    free(line);
    line = (lgfx::rgb565_t*)malloc(w * sizeof(lgfx::rgb565_t));
    lineWidth = line ? w : 0;
    if (!line) return;
  }

  display.startWrite();
  for (size_t y = 0; y < h; y++) {
    const uint8_t* row = gray + (y * step) * width;
    for (size_t x = 0; x < w; x++) {
      uint8_t g = row[x * step];
      line[x] = lgfx::rgb565_t(g, g, g);
    }
    display.pushImage(x0, y, w, 1, line);
  }

  if (status) {
    display.fillRect(0, h, display.width(), display.height() - h, DISPLAY_COLOR_BG);
    display.setTextColor(DISPLAY_COLOR_TEXT);
    display.setFont(&fonts::Font0);
    display.setTextSize(2.0f);
    display.setTextDatum(middle_center);
    display.drawString(status, display.width() / 2, h + (display.height() - h) / 2);
  }
  display.endWrite();
}

void drawQrCode(esp_qrcode_handle_t qrCode) {
  #ifndef DISPLAY_SMALL

  display.startWrite();
  display.clearDisplay();
  display.fillScreen(TFT_DARKGREY);

  int size = esp_qrcode_get_size(qrCode);
  log_d("Drawing QR code of size %d", size);
  int32_t px = QR_MODULE_PIXEL_SIZE;
  int32_t offset = (min(display.width(), display.height()) - (size * px)) / 2;

  for (size_t y = 0; y < size; y++) {
    for (size_t x = 0; x < size; x++) {
      int color = esp_qrcode_get_module(qrCode, x, y) ? TFT_BLACK : TFT_WHITE;
      display.fillRect(x * px + offset, y * px + offset, px, px, color);
    }
  }

  display.endWrite();

  #endif
}

#endif
