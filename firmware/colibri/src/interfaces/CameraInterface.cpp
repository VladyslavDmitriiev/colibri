// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "CameraInterface.h"

#include <cstring>

#include "../core/Stateless.h"

#if defined(CAMERA_ENABLED)

esp_err_t camera_init() {
  // zero-initialise: the driver reads fields we don't set explicitly
  camera_config_t config = {};

  config.pin_pwdn = CAMERA_GPIO_PWDN;
  config.pin_reset = CAMERA_GPIO_RESET;
  config.pin_xclk = CAMERA_GPIO_XCLK;
  config.pin_sccb_sda = CAMERA_GPIO_SIOD;
  config.pin_sccb_scl = CAMERA_GPIO_SIOC;
  // SIOD/SIOC set to -1: share an I2C bus that's already initialised (e.g. with touch/PMIC)
  config.sccb_i2c_port = CAMERA_SCCB_I2C_PORT;
  config.pin_d7 = CAMERA_GPIO_D7;
  config.pin_d6 = CAMERA_GPIO_D6;
  config.pin_d5 = CAMERA_GPIO_D5;
  config.pin_d4 = CAMERA_GPIO_D4;
  config.pin_d3 = CAMERA_GPIO_D3;
  config.pin_d2 = CAMERA_GPIO_D2;
  config.pin_d1 = CAMERA_GPIO_D1;
  config.pin_d0 = CAMERA_GPIO_D0;
  config.pin_vsync = CAMERA_GPIO_VSYNC;
  config.pin_href = CAMERA_GPIO_HREF;
  config.pin_pclk = CAMERA_GPIO_PCLK;

  config.xclk_freq_hz = CAMERA_XCLK_FREQ;
  config.ledc_timer = LEDC_TIMER_0;
  config.ledc_channel = LEDC_CHANNEL_0;
  // grayscale frames can be fed to quirc directly, no conversion needed
  config.pixel_format = PIXFORMAT_GRAYSCALE;
  config.frame_size = CAMERA_FRAME_SIZE;
  config.fb_count = 2;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.grab_mode = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) return err;

  sensor_t* sensor = esp_camera_sensor_get();
  if (sensor) {
    sensor->set_vflip(sensor, CAMERA_VFLIP);
    sensor->set_hmirror(sensor, CAMERA_HMIRROR);
  }

  return ESP_OK;
}

void CameraInterface::init() {
  log_i("Initializing camera interface...");

  if (!psramFound()) {
    log_e("Camera interface requires PSRAM");
    return;
  }

#ifndef STATELESS_MODE
  esp_err_t err = camera_init();
  if (err != ESP_OK) {
    log_e("Camera initialization failed with error 0x%x", err);
    return;
  }
  isSensorOn = true;
#endif

  qr = quirc_new();
  qrCode = (struct quirc_code*)ps_malloc(sizeof(struct quirc_code));
  qrData = (struct quirc_data*)ps_malloc(sizeof(struct quirc_data));
  if (!qr || !qrCode || !qrData) {
    log_e("QR decoder could not be allocated");
    if (qr) quirc_destroy(qr);
    free(qrCode);
    free(qrData);
    if (isSensorOn) esp_camera_deinit();
    isSensorOn = false;
    return;
  }

  resetDecoder();
  initialised = true;
  log_d("Camera initialized successfully");
}

void CameraInterface::update() {
  if (!initialised || isHot) return;

  // scanning only makes sense once the wallet can sign
  if (wallet.isLocked()) {
    isReady = false;
#ifdef STATELESS_MODE
    // the camera and the touch panel share the I2C bus
    if (isSensorOn) stopSensor();
    if (!isLockShown) {
      touchBegin();
      statelessShowLocked();
      isLockShown = true;
    }
    int16_t x, y;
    if (touchTapped(x, y)) {
      statelessUnlock([](std::string& text) { return cameraInterface.scanText(text); });
      isLockShown = false;
    }
#endif
    return;
  }
#ifdef STATELESS_MODE
  if (!isSensorOn) {
    touchEnd();
    startSensor();
    isLockShown = false;
    if (!isSensorOn) return;
  }
#endif
  if (!isReady) {
    // key check reads storage, don't do it every loop
    uint64_t now = millis();
    if (now - timeLastKeyCheck < CAMERA_KEY_CHECK_INTERVAL) return;
    timeLastKeyCheck = now;
    if (!wallet.isKeySet()) return;

    isReady = true;
    showIdle();
  }

  // result QR stays on screen until dismissed, so we don't rescan the same request
  if (isShowingResult) {
    if (buttonOk.isShortPressed() || millis() - timeResultShown > CAMERA_RESULT_TIMEOUT) {
      isShowingResult = false;
      showIdle();
    }
    return;
  }

  #ifdef STATELESS_MODE
  if (buttonOk.isShortPressed()) {
    showMenu();
    return;
  }
  #endif

  #ifdef BUTTON_LAYOUT_MAIN_TWO
  // show pairing QR (`ur:crypto-hdkey`) to connect a software wallet
  if (buttonOk.isMultiPressed(CAMERA_PAIR_CLICKS)) {
    WalletResponse r = wallet.urRequestPair();
    if (r.status >= Ok) {
      isShowingResult = true;
      timeResultShown = millis();
    }
    return;
  }
  #endif

  uint64_t now = millis();
  if (now - timeLastScan < CAMERA_SCAN_INTERVAL) return;
  timeLastScan = now;

  // drop incomplete multi-part scans after a while
  if (timeLastPart > 0 && now - timeLastPart > CAMERA_SCAN_TIMEOUT) {
    log_d("Multi-part scan timed out");
    resetDecoder();
    showIdle();
  }

  std::string payload;
  bool found = scanFrame(payload);
  logStats();
  if (found) {
    receivePart(payload);
  }
}

bool CameraInterface::scanFrame(std::string& payload) {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    log_e("Frame buffer could not be acquired");
    return false;
  }

  int w, h;
  quirc_begin(qr, &w, &h);
  if (w != fb->width || h != fb->height) {
    if (quirc_resize(qr, fb->width, fb->height) < 0) {
      log_e("QR decoder could not be resized");
      esp_camera_fb_return(fb);
      return false;
    }
  }

  uint8_t* image = quirc_begin(qr, nullptr, nullptr);
  memcpy(image, fb->buf, fb->width * fb->height);
  #ifdef CAMERA_PREVIEW
  drawCameraPreview(fb->buf, fb->width, fb->height, statusChanged ? status : nullptr);
  statusChanged = false;
  #endif
  esp_camera_fb_return(fb);
  quirc_end(qr);

  bool found = false;
  int count = quirc_count(qr);
  stats.frames++;
  stats.codes += count;
  for (int i = 0; i < count && !found; i++) {
    quirc_extract(qr, i, qrCode);

    quirc_decode_error_t err = quirc_decode(qrCode, qrData);
    if (err == QUIRC_ERROR_DATA_ECC) {
      // retry mirrored, some cameras flip the image
      quirc_flip(qrCode);
      err = quirc_decode(qrCode, qrData);
    }
    if (err != QUIRC_SUCCESS) {
      stats.errors++;
      log_v("QR decoding failed: %s", quirc_strerror(err));
      continue;
    }

    payload.assign(reinterpret_cast<const char*>(qrData->payload), qrData->payload_len);
    found = true;
  }

  return found;
}

void CameraInterface::receivePart(const std::string& payload) {
  if (payload.size() < sizeof(UR_PREFIX) || strncasecmp(payload.c_str(), UR_PREFIX, sizeof(UR_PREFIX) - 1) != 0) {
    log_d("Ignoring non-UR QR code");
    return;
  }

  if (!decoder->receive_part(payload)) return;
  timeLastPart = millis();

  if (!decoder->is_complete()) {
    int progress = (int)(decoder->estimated_percent_complete() * 100);
    if (progress != lastProgress) {
      lastProgress = progress;
      char text[24];
      snprintf(text, sizeof(text), DISPLAY_SCAN_PROGRESS, progress);
      setStatus(text);
    }
    return;
  }

  if (!decoder->is_success()) {
    log_e("UR decoding failed");
    resetDecoder();
    displayMessage(DISPLAY_ERROR);
    return;
  }

  UR ur = decoder->result_ur();
  resetDecoder();
  handleUr(ur);
}

void CameraInterface::handleUr(UR& ur) {
  setStateBusy(true);
  WalletResponse r = wallet.urSign(ur);
  setStateBusy(false);

  if (r.status < Ok) {
    log_e("UR signing failed: %s", r.error);
    // the blind signing notice was already shown and dismissed
    if (!r.error || strcmp(r.error, RPC_ERROR_BLIND_SIGNING) != 0) {
      displayMessage(r.status == UserRejected ? DISPLAY_REJECTED : DISPLAY_ERROR);
      delay(1000);
    }
    showIdle();
    return;
  }

  log_d("Signature UR: %s", r.result.c_str());
  if (displayQrCode(r.result.c_str())) {
    isShowingResult = true;
    timeResultShown = millis();
    buttonOk.reset();
  } else {
    log_e("Signature QR code could not be displayed");
    displayMessage(DISPLAY_ERROR);
  }
}

void CameraInterface::resetDecoder() {
  delete decoder;
  decoder = new ur::URDecoder();
  timeLastPart = 0;
  lastProgress = -1;
}

void CameraInterface::showIdle() { setStatus(DISPLAY_SCAN_QR); }

void CameraInterface::setStatus(const char* text) {
  #ifdef CAMERA_PREVIEW
  // drawn below the next preview frame
  strncpy(status, text, sizeof(status) - 1);
  status[sizeof(status) - 1] = '\0';
  statusChanged = true;
  #else
  displayMessage(text);
  #endif
}

void CameraInterface::logStats() {
  uint64_t now = millis();
  if (now - stats.since < 1000) return;
  if (stats.frames > 0) {
    log_d("Camera: %u frames, %u QR codes found, %u not decodable", stats.frames, stats.codes, stats.errors);
  }
  stats = {0, 0, 0, now};
}

void CameraInterface::disconnect() {}

void CameraInterface::stop() {
  disconnect();
  if (!initialised) return;

  if (isSensorOn) esp_camera_deinit();
  isSensorOn = false;
  quirc_destroy(qr);
  qr = nullptr;
  free(qrCode);
  qrCode = nullptr;
  free(qrData);
  qrData = nullptr;
  delete decoder;
  decoder = nullptr;
  isShowingResult = false;
  initialised = false;
}

#ifdef STATELESS_MODE
void CameraInterface::startSensor() {
  esp_err_t err = camera_init();
  if (err != ESP_OK) {
    log_e("Camera initialization failed with error 0x%x", err);
    setStatus("Camera error");
    return;
  }
  isSensorOn = true;
  resetDecoder();
  isReady = false;  // shows the scan screen again
}

void CameraInterface::stopSensor() {
  esp_camera_deinit();
  isSensorOn = false;
  resetDecoder();
}

bool CameraInterface::scanText(std::string& text) {
  if (!initialised) return false;
  bool wasOn = isSensorOn;
  if (!isSensorOn) startSensor();
  if (!isSensorOn) return false;
  displayMessage(DISPLAY_SCAN_TEXT_QR);
  setStatus(DISPLAY_SCAN_TEXT_QR);
  buttonOk.reset();
  bool found = false;
  uint32_t frames = 0;
  uint64_t start = millis();
  while (!found && millis() - start < TIMEOUT_TOUCH_INPUT) {
    updateUi();
    if (buttonOk.isShortPressed()) break;
    found = scanFrame(text);
    frames++;
  }
  log_d("Text QR scan: %s after %u frames", found ? "found" : "none", frames);
  // the decoder keeps the last payload, it may be a password
  memzero(qrData->payload, sizeof(qrData->payload));
  if (!wasOn) stopSensor();
  buttonOk.reset();
  return found;
}

// touch menu, opened with the button while scanning
void CameraInterface::showMenu() {
  stopSensor();
  touchBegin();
  int choice = chooseOption("MENU", {"Pair wallet app", "Lock", "Back"});
  touchEnd();
  if (choice == 1) {
    wallet.lock();
    return;
  }
  startSensor();
  if (choice == 0 && isSensorOn) {
    // pairing QR (`ur:crypto-hdkey`) for MetaMask, Rabby etc.
    WalletResponse r = wallet.urRequestPair();
    if (r.status >= Ok) {
      isShowingResult = true;
      timeResultShown = millis();
      isReady = true;
    }
  }
}
#endif

void CameraInterface::wipe() { stop(); }

#endif
