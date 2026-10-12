// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "trng.h"

#if defined(RNG_ANTENNA_DISABLED)
// without a running radio, use the SAR ADC as entropy source for the hardware RNG
// (no Wi-Fi/BT may run and no ADC may be used in the meantime)
  #if defined(OTA_ENABLED)
extern bool isOtaActive;  // the OTA access point's radio is entropy source enough
  #else
const bool isOtaActive = false;
  #endif

void hwRngBegin() {
  if (!isOtaActive) bootloader_random_enable();
}

void hwRngEnd() {
  if (!isOtaActive) bootloader_random_disable();
}

#else
  #define hwRngBegin()
  #define hwRngEnd()
#endif

void generateEntropy(uint8_t *result, uint8_t entropyLength) {
  hwRngBegin();

  // gather entropy from hardware RNG
  esp_fill_random(result, entropyLength);

  // hash the entropy additionally if 256 bits are requested
  if (entropyLength == HASH_LENGTH) {
    uint8_t hash[HASH_LENGTH];
    sha3_256(result, entropyLength, hash);
    memcpy(result, hash, HASH_LENGTH);

    memzero(hash, HASH_LENGTH);
  }

  hwRngEnd();
}

uint32_t randomNumber(uint32_t min, uint32_t max) {
  hwRngBegin();

  uint32_t r;
  if (min == 0 && max == 0) {
    r = random();
  } else {
    r = random(min, max);
  }

  hwRngEnd();

  return r;
}

uint32_t randomNumber() { return randomNumber(0, 0); }

// for trezor-crypto lib
uint32_t random32(void) { return randomNumber(); }
