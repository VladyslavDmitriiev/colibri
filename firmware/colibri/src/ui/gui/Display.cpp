// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "Display.h"

#include <functional>
#include <vector>

extern "C" {
#include <memzero.h>
}

#ifdef DISPLAY_ENABLED

static TFT_eSPI display;
// static TFT_eSprite sprite(&display);

struct TextLine {
  std::string text;
  char style;  // DISPLAY_LINE_* char, 0 for plain text
};

static bool isLineStyle(char c) { return c >= DISPLAY_LINE_TITLE[0] && c <= DISPLAY_LINE_ALERT[0]; }

// monospace fonts from big to small, tried until the text fits
struct TextTier {
  const lgfx::IFont* font;
  float size;
  float bigSize;  // DISPLAY_LINE_BIG
};

#ifndef DISPLAY_SMALL
static const TextTier textTiers[] = {
    {&fonts::Font0, 2.0f, 3.0f},  // 12x16 px chars
    {&fonts::AsciiFont8x16, 1.0f, 2.0f},
    {&fonts::Font0, 1.0f, 2.0f},  // 6x8 px chars
};
#else
static const TextTier textTiers[] = {{&fonts::Font0, 1.0f, 1.5f}};
#endif

static float lineTextSize(char style, const TextTier& tier) {
  return style == DISPLAY_LINE_BIG[0] ? tier.bigSize : tier.size;
}

static uint16_t lineColor(char style) {
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
static std::vector<TextLine> layoutText(LovyanGFX& gfx, const char* message, const TextTier& tier, int32_t maxWidth) {
  std::vector<TextLine> lines;
  const char* c = message;
  while (true) {
    const char* end = strchr(c, '\n');
    std::string raw = end ? std::string(c, end - c) : std::string(c);
    char style = (!raw.empty() && isLineStyle(raw[0])) ? raw[0] : 0;
    if (style) raw.erase(0, 1);

    gfx.setTextSize(lineTextSize(style, tier));
    size_t maxChars = maxWidth / gfx.textWidth("0");
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
static int32_t lineHeight(LovyanGFX& gfx, const TextLine& line, const TextTier& tier) {
  gfx.setTextSize(lineTextSize(line.style, tier));
  return gfx.fontHeight() * 3 / 2;
}

// Centered text, multiple lines separated by `\n`, each optionally styled by a DISPLAY_LINE_*
// prefix. Long lines wrap, and the text falls back to smaller fonts if it doesn't fit.
// renders on the panel or, for screenshots, on a sprite
static void renderText(LovyanGFX& gfx, const char* message) {
  gfx.fillScreen(DISPLAY_COLOR_BG);

  const int32_t padding = 10;
  const size_t tierCount = sizeof(textTiers) / sizeof(textTiers[0]);
  const TextTier* tier = nullptr;
  std::vector<TextLine> lines;
  int32_t totalHeight;
  for (size_t i = 0; i < tierCount; i++) {
    tier = &textTiers[i];
    gfx.setFont(tier->font);
    lines = layoutText(gfx, message, *tier, gfx.width() - 2 * padding);
    totalHeight = 0;
    for (const TextLine& line : lines) totalHeight += lineHeight(gfx, line, *tier);
    if (totalHeight <= gfx.height() - 2 * padding) break;
  }

  int32_t y = (gfx.height() - totalHeight) / 2;
  gfx.setTextDatum(middle_center);
  for (const TextLine& line : lines) {
    int32_t h = lineHeight(gfx, line, *tier);
    gfx.setTextColor(lineColor(line.style));
    gfx.drawString(line.text.c_str(), gfx.width() / 2, y + h / 2);
    y += h;
  }

  gfx.drawRect(5, 5, gfx.width() - 10, gfx.height() - 10, DISPLAY_COLOR_TEXT);
}

#ifdef DEBUG_REMOTE_CONTROL
// draws the screen on the panel again, for screenshots; empty while it shows a camera frame or a QR code
static std::function<void(LovyanGFX&)> lastScreen;
#endif

void drawText(const char* message) {
#ifdef DEBUG_REMOTE_CONTROL
  std::string text = message;
  lastScreen = [text](LovyanGFX& gfx) { renderText(gfx, text.c_str()); };
#endif
  display.startWrite();
  display.clearDisplay();
  renderText(display, message);
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
#ifdef DEBUG_REMOTE_CONTROL
  lastScreen = nullptr;
#endif
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
#ifdef DEBUG_REMOTE_CONTROL
  lastScreen = nullptr;
#endif
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


// ===== Touch screens ===== //

// Layout after Kern's compact keyboard: few keys per row for wide touch targets,
// digits and symbols on their own page, rows stretched to the full width.
static const int32_t KEYBOARD_TOP = 168;
static const int32_t KEY_GAP = 3;  // drawn inside each key, hit areas still touch
static const int32_t FIELD_Y = 48, FIELD_H = 40;
static const int32_t BACK_W = 56, BACK_H = 40;

struct KeySpec {
  const char* text;  // one character for KeyChar, else the label
  uint8_t units;
  KeyAction action;
};

static void keyboardKeys(uint8_t layout, bool withScan, std::vector<KeyboardKey>& keys) {
  static const char* const letterRows[2][3] = {
      {"qwertyuiop", "asdfghjkl", "zxcvbnm"},
      {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"},
  };
  static const char* const symbolRows[4] = {"1234567890", "@#$%&*+-=/", "()[]{}<>\"'", "!?;:_\\|"};
  std::vector<std::vector<KeySpec>> rows;
  auto charRow = [](const char* chars, std::vector<KeySpec>& row) {
    for (const char* c = chars; *c; c++) row.push_back({c, 1, KeyChar});
  };
  if (layout < 2) {
    for (int i = 0; i < 2; i++) {
      rows.emplace_back();
      charRow(letterRows[layout][i], rows.back());
    }
    rows.push_back({{layout == 1 ? "abc" : "ABC", 2, KeyShift}});
    charRow(letterRows[layout][2], rows.back());
    rows.back().push_back({"Del", 2, KeyBackspace});
    rows.push_back({{"1#", 2, KeySymbols}, {",", 1, KeyChar}, {" ", 5, KeyChar}, {".", 1, KeyChar}});
  } else {
    for (int i = 0; i < 3; i++) {
      rows.emplace_back();
      charRow(symbolRows[i], rows.back());
    }
    rows.push_back({{"abc", 2, KeyLetters}});
    charRow(symbolRows[3], rows.back());
    rows.back().push_back({"Del", 2, KeyBackspace});
    rows.push_back({{"~", 1, KeyChar}, {"^", 1, KeyChar}, {"`", 1, KeyChar}, {" ", 5, KeyChar}});
  }
  if (withScan) {
    for (KeySpec& k : rows.back()) {
      if (k.text[0] == ' ') k.units = 3;  // space makes room
    }
    rows.back().push_back({"QR", 2, KeyScan});
  }
  rows.back().push_back({"OK", 2, KeyDone});

  keys.clear();
  keys.push_back({0, 0, BACK_W, BACK_H, "<", 0, KeyCancel});
  const int32_t rowHeight = (DISPLAY_HEIGHT - KEYBOARD_TOP) / rows.size();
  for (size_t r = 0; r < rows.size(); r++) {
    int32_t total = 0, done = 0;
    for (const KeySpec& k : rows[r]) total += k.units;
    for (const KeySpec& k : rows[r]) {
      int32_t x = DISPLAY_WIDTH * done / total;
      done += k.units;
      int32_t w = DISPLAY_WIDTH * done / total - x;
      bool isChar = k.action == KeyChar;
      const char* label = !isChar ? k.text : (k.text[0] == ' ' ? "space" : nullptr);
      keys.push_back({x, KEYBOARD_TOP + (int32_t)r * rowHeight, w, rowHeight, label, isChar ? k.text[0] : (char)0, k.action});
    }
  }
}

static void renderKeyboard(
    LovyanGFX& gfx, const std::string& title, const std::string& shown, uint8_t layout, bool withScan, int pressed
) {
  gfx.fillScreen(DISPLAY_COLOR_BG);
  gfx.setFont(&fonts::AsciiFont8x16);
  gfx.setTextSize(1);
  gfx.setTextDatum(top_center);
  gfx.setTextColor(TFT_WHITE);
  gfx.drawString(title.c_str(), gfx.width() / 2, 16);

  // input field, scrolled to the end
  gfx.drawRect(8, FIELD_Y, gfx.width() - 16, FIELD_H, DISPLAY_COLOR_TEXT);
  size_t maxChars = (gfx.width() - 32) / gfx.textWidth("0");
  std::string visible = shown.size() > maxChars ? shown.substr(shown.size() - maxChars) : shown;
  gfx.setTextDatum(middle_left);
  gfx.setTextColor(DISPLAY_COLOR_TEXT);
  gfx.drawString((visible + "_").c_str(), 16, FIELD_Y + FIELD_H / 2);
  gfx.setTextDatum(top_center);
  gfx.setTextColor(TFT_LIGHTGREY);
  gfx.drawString("Tap the field to show or hide", gfx.width() / 2, FIELD_Y + FIELD_H + 8);

  std::vector<KeyboardKey> keys;
  keyboardKeys(layout, withScan, keys);
  gfx.setTextDatum(middle_center);
  for (size_t i = 0; i < keys.size(); i++) {
    const KeyboardKey& k = keys[i];
    uint16_t frame = k.action == KeyDone ? TFT_GREEN : (k.action == KeyChar ? TFT_DARKGREY : TFT_LIGHTGREY);
    int32_t x = k.x + KEY_GAP, y = k.y + KEY_GAP, w = k.w - 2 * KEY_GAP, h = k.h - 2 * KEY_GAP;
    if ((int)i == pressed) {
      gfx.fillRoundRect(x, y, w, h, 6, frame);
    } else {
      gfx.drawRoundRect(x, y, w, h, 6, frame);
    }
    gfx.setTextColor((int)i == pressed ? DISPLAY_COLOR_BG : (k.action == KeyDone ? TFT_GREEN : TFT_WHITE));
    gfx.setTextSize(k.label ? 1 : 2);
    if (k.label) {
      gfx.drawString(k.label, k.x + k.w / 2, k.y + k.h / 2);
    } else {
      char label[2] = {k.ch, 0};
      gfx.drawString(label, k.x + k.w / 2, k.y + k.h / 2);
    }
  }

  // the pressed character, enlarged above the finger so it can be read without lifting it
  if (pressed >= 0 && pressed < (int)keys.size() && keys[pressed].action == KeyChar && !keys[pressed].label) {
    const KeyboardKey& k = keys[pressed];
    const int32_t w = 56, h = 72;
    int32_t x = constrain(k.x + k.w / 2 - w / 2, 0, (int32_t)gfx.width() - w);
    int32_t y = k.y - h - 4;
    gfx.fillRoundRect(x, y, w, h, 10, TFT_WHITE);
    gfx.setTextColor(DISPLAY_COLOR_BG);
    gfx.setTextSize(4);
    char label[2] = {k.ch, 0};
    gfx.drawString(label, x + w / 2, y + h / 2);
  }
}

// Every keystroke used to clear and redraw the panel, which flickered. Frames are drawn on a
// PSRAM sprite instead and only the rows that changed since the last frame are sent.
static LGFX_Sprite* keyboardFrame = nullptr;
static LGFX_Sprite* keyboardShown = nullptr;

static void freeKeyboardSprite(LGFX_Sprite*& sprite) {
  if (!sprite) return;
  if (sprite->getBuffer()) memzero(sprite->getBuffer(), sprite->bufferLength());  // may show a passphrase
  sprite->deleteSprite();
  delete sprite;
  sprite = nullptr;
}

static LGFX_Sprite* newKeyboardSprite() {
  LGFX_Sprite* sprite = new LGFX_Sprite(&display);
  sprite->setPsram(true);
  sprite->setColorDepth(16);
  if (!sprite->createSprite(display.width(), display.height())) {
    delete sprite;
    return nullptr;
  }
  return sprite;
}

void drawKeyboard(const char* title, const std::string& shown, uint8_t layout, bool withScan, int pressed) {
  #ifdef DEBUG_REMOTE_CONTROL
  std::string t = title;
  lastScreen = [t, shown, layout, withScan, pressed](LovyanGFX& gfx) {
    renderKeyboard(gfx, t, shown, layout, withScan, pressed);
  };
  #endif
  if (!keyboardFrame) keyboardFrame = newKeyboardSprite();
  if (keyboardFrame && !keyboardShown) {
    keyboardShown = newKeyboardSprite();
    if (keyboardShown) keyboardShown->fillScreen((uint16_t)~DISPLAY_COLOR_BG);  // differs everywhere: first frame goes out whole
  }
  if (!keyboardFrame || !keyboardShown) {
    // no PSRAM: draw straight to the panel
    freeKeyboardSprite(keyboardFrame);
    freeKeyboardSprite(keyboardShown);
    display.startWrite();
    renderKeyboard(display, title, shown, layout, withScan, pressed);
    display.endWrite();
    return;
  }

  renderKeyboard(*keyboardFrame, title, shown, layout, withScan, pressed);
  const int32_t width = keyboardFrame->width(), height = keyboardFrame->height();
  const uint16_t* next = (const uint16_t*)keyboardFrame->getBuffer();
  const uint16_t* prev = (const uint16_t*)keyboardShown->getBuffer();
  int32_t first = 0, last = height - 1;
  while (first < height && memcmp(next + first * width, prev + first * width, width * 2) == 0) first++;
  if (first == height) return;
  while (last > first && memcmp(next + last * width, prev + last * width, width * 2) == 0) last--;

  display.startWrite();
  display.pushImage(0, first, width, last - first + 1, (const lgfx::swap565_t*)(next + first * width));
  display.endWrite();
  std::swap(keyboardFrame, keyboardShown);
}

void closeKeyboard() {
  freeKeyboardSprite(keyboardFrame);
  freeKeyboardSprite(keyboardShown);
}

int keyboardKeyAt(uint8_t layout, bool withScan, int16_t x, int16_t y) {
  std::vector<KeyboardKey> keys;
  keyboardKeys(layout, withScan, keys);
  for (size_t i = 0; i < keys.size(); i++) {
    const KeyboardKey& k = keys[i];
    if (x >= k.x && x < k.x + k.w && y >= k.y && y < k.y + k.h) return i;
  }
  return -1;
}

KeyAction keyboardHit(uint8_t layout, bool withScan, int16_t x, int16_t y, char& ch) {
  if (y >= FIELD_Y && y < FIELD_Y + FIELD_H + 24) return KeyToggleShow;
  int i = keyboardKeyAt(layout, withScan, x, y);
  if (i < 0) return KeyNone;
  std::vector<KeyboardKey> keys;
  keyboardKeys(layout, withScan, keys);
  ch = keys[i].ch;
  return keys[i].action;
}

static const int32_t MENU_TOP = 90, MENU_ITEM_HEIGHT = 64;

static void renderMenu(LovyanGFX& gfx, const std::string& title, const std::vector<std::string>& options) {
  gfx.fillScreen(DISPLAY_COLOR_BG);
  gfx.setFont(&fonts::AsciiFont8x16);
  gfx.setTextSize(1);
  gfx.setTextDatum(top_center);
  gfx.setTextColor(TFT_WHITE);
  // title may span lines
  int32_t y = 20;
  size_t start = 0;
  while (start <= title.size()) {
    size_t end = title.find('\n', start);
    if (end == std::string::npos) end = title.size();
    gfx.drawString(title.substr(start, end - start).c_str(), gfx.width() / 2, y);
    y += 20;
    start = end + 1;
  }
  gfx.setTextDatum(middle_center);
  gfx.setTextSize(2);
  for (size_t i = 0; i < options.size(); i++) {
    int32_t top = MENU_TOP + i * MENU_ITEM_HEIGHT;
    gfx.drawRoundRect(16, top + 4, gfx.width() - 32, MENU_ITEM_HEIGHT - 8, 8, DISPLAY_COLOR_TEXT);
    gfx.setTextColor(DISPLAY_COLOR_TEXT);
    gfx.drawString(options[i].c_str(), gfx.width() / 2, top + MENU_ITEM_HEIGHT / 2);
  }
}

void drawMenu(const char* title, const std::vector<std::string>& options) {
  #ifdef DEBUG_REMOTE_CONTROL
  std::string t = title;
  lastScreen = [t, options](LovyanGFX& gfx) { renderMenu(gfx, t, options); };
  #endif
  display.startWrite();
  renderMenu(display, title, options);
  display.endWrite();
}

int menuHit(size_t count, int16_t x, int16_t y) {
  if (x < 16 || x >= DISPLAY_WIDTH - 16 || y < MENU_TOP) return -1;
  size_t i = (y - MENU_TOP) / MENU_ITEM_HEIGHT;
  return i < count ? (int)i : -1;
}

#ifdef DEBUG_REMOTE_CONTROL
// last text or touch screen as raw RGB565 (little endian) after a "SHOT_BEGIN w h bytes" line, read by ctl.py.
// The panel can't be read back (no MISO on some boards), so the screen is drawn again on a PSRAM sprite.
void dumpDisplay() {
  if (!lastScreen) {
    Serial.println("SHOT_NONE no text screen shown");
    return;
  }
  LGFX_Sprite sprite(&display);
  sprite.setPsram(true);
  sprite.setColorDepth(16);
  if (!sprite.createSprite(display.width(), display.height())) {
    Serial.println("SHOT_NONE sprite allocation failed");
    return;
  }
  lastScreen(sprite);
  int32_t w = sprite.width(), h = sprite.height();
  Serial.printf("SHOT_BEGIN %d %d %d\n", w, h, w * h * 2);
  for (int32_t y = 0; y < h; y++) {
    uint16_t row[DISPLAY_WIDTH > DISPLAY_HEIGHT ? DISPLAY_WIDTH : DISPLAY_HEIGHT];
    for (int32_t x = 0; x < w; x++) row[x] = sprite.readPixel(x, y);
    Serial.write((const uint8_t*)row, w * 2);
  }
  Serial.flush();
  sprite.deleteSprite();
}
#endif

#endif
