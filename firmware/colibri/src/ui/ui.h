// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "../../config.h"
// config first

#include <esp_system.h>
#include <qrcode.h>

#include "../core/ota/ota.h"
#include "./gui/Display.h"
#include "AsyncButton.h"
#include "AsyncLed.h"
#include "Touch.h"

extern AsyncButton buttonOk;
#ifdef BUTTON_LAYOUT_MAIN_TWO
extern AsyncButton buttonCancel;
#else
  #define buttonCancel buttonOk
#endif
#ifdef LED_ENABLED
extern AsyncLed led;
#endif
extern bool isHot;
extern bool isBusy;
extern bool isOtaActive;
extern Connection connection;

void updateUi();
#ifdef DEBUG_REMOTE_CONTROL
void debugRemoteCommand(const std::string& cmd);
#endif

bool waitForApproval(const char* text, RgbColor color = Hot);

void waitForDismiss(const char* text);
#ifdef TOUCH_ENABLED
// touch keyboard, false if the user went back or didn't type for a while
// fills `text` from a scanned QR code, false if nothing was scanned
typedef bool (*TextScanner)(std::string& text);
// touch keyboard; with `scan` it gets a QR key, the scanned text lands in the field for review
bool readText(const char* title, std::string& text, bool secret = true, TextScanner scan = nullptr);
// buttons, index of the chosen option or -1
int chooseOption(const char* title, const std::vector<std::string>& options);
#endif

void setStateConnected(Connection conn);

void setStateBusy(bool busy);

void displayMessage(const char* message);

bool displayQrCode(const char* data);
