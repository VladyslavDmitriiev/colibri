// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "ui.h"

#include "../core/Wallet.h"
extern Wallet wallet;

static void checkForGesture() {
  if (isHot) return;

  // check for lock/OTA request
  if (buttonCancel.isLongPressed()) {
    if (!wallet.isLocked()) {
      log_i("Locking device");
      wallet.lock();
#ifdef OTA_ENABLED
    } else if (!isOtaActive) {
      log_i("Enabling OTA mode");
      initOta();
      isOtaActive = true;
      led_blink(100, 100, RgbColor::Busy);
#endif
    } else {
      log_i("Rebooting device");
      led_turnOn(RgbColor::Warning);
      delay(500);
      esp_restart();
    }
  }

  // check for wipe request
  if (buttonCancel.isMultiPressed(10)) {
    log_i("Device wipe initiated");
    if (!waitForApproval(DISPLAY_APPROVE_WIPE, RgbColor::Warning)) {
      return;
    }

    wallet.wipe();
  }
}

static void checkForTimeout() {
  uint64_t now = millis();
  uint64_t delta = now > wallet.timeLastActivity ? now - wallet.timeLastActivity : 0;

  if (wallet.timeLastActivity > 0 && delta > TIMEOUT_INACTIVITY_LOCK) {
    log_i("Auto-locking device after inactivity of %d ms (limit=%d)", delta, TIMEOUT_INACTIVITY_LOCK);
    log_d("- Now:   %d", now);
    log_d("- Last:  %d", wallet.timeLastActivity);

    wallet.lock();
  }
}

static void updateButtons() {
  // update buttons
  buttonOk.update();
#ifdef BUTTON_LAYOUT_MAIN_TWO
  buttonCancel.update();
#endif
}

static void updateLed() {
  if (!isHot && !led_isBlinking) {
    if (isBusy) {
      led_turnOn(Busy);
    } else if (connection > NotConnected) {
      led_turnOn(Idle);
    } else {
      led_turnOn(Connecting);
    }
  }

  led_update();
}

#ifdef DEBUG_REMOTE_CONTROL
// serial commands to drive the device from a computer, debug builds only:
// !ok, !ok2 (double click), !cancel, !long, !shot (screenshot)
void debugRemoteCommand(const std::string& cmd) {
  log_i("Debug remote command: %s", cmd.c_str());
  if (cmd == "!ok") {
    buttonOk.simulateClick();
  } else if (cmd == "!ok2") {
    buttonOk.simulateClick();
    buttonOk.simulateClick();
  } else if (cmd == "!cancel") {
  #ifdef BUTTON_LAYOUT_MAIN_TWO
    buttonCancel.simulateClick();
  #else
    buttonOk.simulateLongPress();
  #endif
  } else if (cmd == "!long") {
    buttonOk.simulateLongPress();
  } else if (cmd == "!shot") {
    dumpDisplay();
  }
}

// while a request blocks the main loop nobody else reads the serial port
static void debugRemotePoll() {
  static std::string line;
  if (!isBusy && !isHot) return;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (!line.empty() && line[0] == '!') debugRemoteCommand(line);
      line.clear();
    } else if (line.size() < 32) {
      line += c;
    }
  }
}
#endif

void updateUi() {
#ifdef DEBUG_REMOTE_CONTROL
  debugRemotePoll();
#endif
  if (isOtaActive) {
    updateOta();
  }

  updateButtons();
  updateLed();
  checkForGesture();
  checkForTimeout();
}

void displayMessage(const char* message) {
  log_d("Display: %s", message);
#ifdef DISPLAY_ENABLED
  drawText(message);
#endif
}

bool displayQrCode(const char* data) {
#if defined(DISPLAY_ENABLED) && !defined(DISPLAY_SMALL)
  esp_qrcode_config_t cfg = (esp_qrcode_config_t){
      .display_func = drawQrCode,
      .max_qrcode_version = QR_MAX_VERSION,
      .qrcode_ecc_level = ESP_QRCODE_ECC_LOW,
  };

  // display via callback to `Display` class
  return esp_qrcode_generate(&cfg, data) == ESP_OK;
#endif
}

bool waitForApproval(const char* text, RgbColor color) {
  log_i("Waiting for user approval...");
  // reset auto-lock timeout
  if (!wallet.isLocked() && wallet.isKeySet()) {
    wallet.timeLastActivity = millis();
  }

#ifndef DISPLAY_SMALL
  // tell the user how to answer
  std::string prompt = std::string(text) + "\n\n" DISPLAY_LINE_NOTE DISPLAY_HINT_APPROVE "\n" DISPLAY_LINE_NOTE DISPLAY_HINT_REJECT;
  displayMessage(prompt.c_str());
#else
  displayMessage(text);
#endif

  // set led hot
  isHot = true;
  led_blink(100, 100, color);
  updateUi();

  // reset buttons
  buttonOk.reset();
#ifdef BUTTON_LAYOUT_MAIN_TWO
  buttonCancel.reset();
#endif

  // loop for n sec, check for presses
  bool isApproved = false;
  uint64_t startTime = millis();
  while ((millis() - startTime) < TIMEOUT_WAIT_FOR_APPROVAL) {
    updateUi();

#ifdef BUTTON_LAYOUT_MAIN_TWO
    if (buttonCancel.isShortPressed()) {
      isApproved = false;
      break;
    }
    if (buttonOk.isShortPressed()) {
      isApproved = true;
      break;
    }
#else  // BUTTON_LAYOUT_MAIN_ONE
    if (buttonOk.isLongPressed()) {
      isApproved = false;
      break;
    }
    if (buttonOk.isMultiPressed(2)) {
      isApproved = true;
      break;
    }
#endif

    delay(5);
  }

  // blink led, reset hot state
  displayMessage(isApproved ? DISPLAY_APPROVED : DISPLAY_REJECTED);
  led_indicate(isApproved);
  isHot = false;

  buttonOk.reset();
  buttonCancel.reset();
  updateUi();

  return isApproved;
}

// show a notice until the user presses a button or the approval timeout runs out
void waitForDismiss(const char* text) {
#ifndef DISPLAY_SMALL
  std::string notice = std::string(text) + "\n\n" DISPLAY_LINE_NOTE DISPLAY_HINT_CLOSE;
  displayMessage(notice.c_str());
#else
  displayMessage(text);
#endif
  led_indicate(false);

  buttonOk.reset();
  buttonCancel.reset();
  uint64_t startTime = millis();
  while ((millis() - startTime) < TIMEOUT_WAIT_FOR_APPROVAL) {
    updateUi();
    if (buttonOk.isShortPressed() || buttonCancel.isShortPressed()) break;
    delay(5);
  }
  buttonOk.reset();
  buttonCancel.reset();
}

void setStateConnected(Connection conn) {
  log_d("Interface connected: %s (%d)", conn > 0 ? "true" : "false", conn);
  connection = conn;

  wallet.lock();
  if (connection == DebugSerialConnected) {
    displayMessage(DISPLAY_DEBUG);
    delay(2000);
  } else if (connection > DebugSerialConnected) {
    displayMessage(DISPLAY_CONNECTED);
    delay(500);
    displayMessage(DISPLAY_LOCKED);
  } else {
    displayMessage(DISPLAY_NOT_CONNECTED);
    delay(500);
  }

  updateUi();
}

void setStateBusy(bool busy) {
  isBusy = busy;
  updateUi();
}
