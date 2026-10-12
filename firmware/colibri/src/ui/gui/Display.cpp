// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "Display.h"

#include <vector>

#ifdef DISPLAY_ENABLED

static TFT_eSPI display;
// static TFT_eSprite sprite(&display);

struct TextLine {
  std::string text;
  char style;  // DISPLAY_LINE_* char, 0 for plain text
};

static bool isLineStyle(char c) { return c >= DISPLAY_LINE_TITLE[0] && c <= DISPLAY_LINE_ALERT[0]; }

static float lineTextSize(char style, float baseSize) {
  return style == DISPLAY_LINE_BIG[0] ? baseSize * 1.5f : baseSize;
}

static uint32_t lineColor(char style) {
  switch (style) {
    case DISPLAY_LINE_TITLE[0]: return TFT_WHITE;
    case DISPLAY_LINE_WARNING[0]: return TFT_YELLOW;
    case DISPLAY_LINE_DANGER[0]:
    case DISPLAY_LINE_ALERT[0]: return TFT_RED;
    case DISPLAY_LINE_NOTE[0]: return TFT_LIGHTGREY;
    default: return DISPLAY_COLOR_TEXT;
  }
}

// Splits text at newlines, takes the style prefix of each line and wraps lines wider
// than the frame, at the last space if there is one
static std::vector<TextLine> layoutText(const char* message, float baseSize, int32_t maxWidth) {
  std::vector<TextLine> lines;
  const char* c = message;
  while (true) {
    const char* end = strchr(c, '\n');
    std::string raw = end ? std::string(c, end - c) : std::string(c);
    char style = (!raw.empty() && isLineStyle(raw[0])) ? raw[0] : 0;
    if (style) raw.erase(0, 1);

    display.setTextSize(lineTextSize(style, baseSize));
    size_t maxChars = maxWidth / display.textWidth("0");
    if (maxChars < 1) maxChars = 1;
    do {
      size_t n = raw.size();
      if (n > maxChars) {
        size_t space = raw.rfind(' ', maxChars);
        n = (space != std::string::npos && space > 0) ? space : maxChars;
      }
      lines.push_back({raw.substr(0, n), style});
      raw.erase(0, n);
      if (!raw.empty() && raw[0] == ' ') raw.erase(0, 1);
    } while (!raw.empty());

    if (!end) break;
    c = end + 1;
  }
  return lines;
}

// line pitch with spacing, so multi-line text stays readable
static int32_t lineHeight(const TextLine& line, float baseSize) {
  display.setTextSize(lineTextSize(line.style, baseSize));
  return display.fontHeight() * 3 / 2;
}

// Centered text, multiple lines separated by `\n`, each optionally styled by a DISPLAY_LINE_*
// prefix. Long lines wrap, and the text falls back to the small font size if it doesn't fit.
void drawText(const char* message) {
  display.startWrite();
  display.clearDisplay();
  display.fillScreen(DISPLAY_COLOR_BG);
  display.setFont(&fonts::Font0);

  float baseSize = 1.0f;
  #ifndef DISPLAY_SMALL
  baseSize = 2.0f;
  #endif

  const int32_t padding = 10;
  std::vector<TextLine> lines;
  int32_t totalHeight;
  while (true) {
    lines = layoutText(message, baseSize, display.width() - 2 * padding);
    totalHeight = 0;
    for (const TextLine& line : lines) totalHeight += lineHeight(line, baseSize);
    if (baseSize <= 1.0f || totalHeight <= display.height() - 2 * padding) break;
    baseSize = 1.0f;
  }

  int32_t y = (display.height() - totalHeight) / 2;
  display.setTextDatum(middle_center);
  for (const TextLine& line : lines) {
    int32_t h = lineHeight(line, baseSize);
    display.setTextColor(lineColor(line.style));
    display.drawString(line.text.c_str(), display.width() / 2, y + h / 2);
    y += h;
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
