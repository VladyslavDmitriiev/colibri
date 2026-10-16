// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "AbstractInterface.h"

#if defined(CAMERA_ENABLED)
  #include <bc-ur.hpp>

  #include "esp_camera.h"

extern "C" {
  #include <quirc.h>
}

/*
 * Camera interface for air-gapped BC-UR QR code signing (EIP-4527).
 *
 * Scans (animated) `ur:eth-sign-request` QR codes, e.g. from MetaMask's
 * "QR-based hardware wallet", passes them to `Wallet::urSign()` and shows
 * the resulting `ur:eth-signature` as a QR code on the display.
 */
class CameraInterface : public Interface {
 public:
  CameraInterface()
      : Interface(),
        qr(nullptr),
        decoder(nullptr),
        timeLastScan(0),
        timeLastPart(0),
        lastProgress(-1),
        isShowingResult(false),
        timeLastKeyCheck(0),
        isReady(false) {};

  void init();
  void update();
  void stop();
  void disconnect();
  void wipe();

 private:
  struct quirc* qr;
  // ~13 KB together, too big for the loop task's stack
  struct quirc_code* qrCode;
  struct quirc_data* qrData;
  ur::URDecoder* decoder;
  uint64_t timeLastScan;
  uint64_t timeLastPart;
  int lastProgress;
  bool isShowingResult;
  uint64_t timeResultShown;
  uint64_t timeLastKeyCheck;
  bool isReady;
  bool isSensorOn = false;
#ifdef STATELESS_MODE
  bool isLockShown = false;
  void startSensor();
  void stopSensor();
  void showMenu();
#endif
  char status[24] = {0};
  bool statusChanged = false;
  struct {
    uint32_t frames, codes, errors;
    uint64_t since;
  } stats = {0, 0, 0, 0};

  bool scanFrame(std::string& payload);
  void receivePart(const std::string& payload);
  void handleUr(UR& ur);
  void resetDecoder();
  void showIdle();
  void setStatus(const char* text);
  void logStats();
};

#endif
