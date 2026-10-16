// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "../../config.h"

#include <string>
#include <vector>

// KEF envelopes as written by Krux and Kern (https://github.com/odudex/kern):
// [len_id:1] [id] [version:1] [iterations:3 BE] [iv] [ciphertext] [auth]
// Only version 20 is supported: AES-256-GCM, 4 byte tag, key = PBKDF2-SHA256(password, id).

#define KEF_VERSION_GCM 20
#define KEF_MIN_ITERATIONS 10000
#define KEF_MAX_ITERATIONS 10000000  // a crafted envelope must not park the device in PBKDF2
#define KEF_MAX_ENVELOPE 1024

enum KefResult {
  KefOk,
  KefMalformed,
  KefUnsupportedVersion,
  KefWrongPassword,
};

// id (the PBKDF2 salt, Kern's wallet name) of an envelope, false if malformed
bool kefReadId(const std::vector<uint8_t>& envelope, std::string& id);

KefResult kefDecrypt(
    const std::vector<uint8_t>& envelope, const std::string& password, std::vector<uint8_t>& plaintext
);

// standard base64 as Kern stores envelopes on SD, whitespace ignored
bool base64Decode(const std::string& in, std::vector<uint8_t>& out);
