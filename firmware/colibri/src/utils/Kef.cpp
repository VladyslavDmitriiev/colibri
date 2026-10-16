// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "Kef.h"

extern "C" {
#include <aes/aesgcm.h>
#include <memzero.h>
#include <pbkdf2.h>
}

static const size_t KEF_IV_LENGTH = 12;
static const size_t KEF_TAG_LENGTH = 4;

struct KefHeader {
  size_t idOffset, idLength;
  uint8_t version;
  uint32_t iterations;
  size_t payloadOffset;
};

static bool parseHeader(const std::vector<uint8_t>& env, KefHeader& h) {
  if (env.empty() || env.size() > KEF_MAX_ENVELOPE) return false;
  h.idLength = env[0];
  h.idOffset = 1;
  if (h.idLength == 0 || env.size() < 1 + h.idLength + 1 + 3) return false;
  h.version = env[1 + h.idLength];
  const uint8_t* it = &env[1 + h.idLength + 1];
  uint32_t stored = (uint32_t)it[0] << 16 | (uint32_t)it[1] << 8 | it[2];
  // small values count in units of 10000
  h.iterations = stored <= 10000 ? stored * 10000 : stored;
  h.payloadOffset = 1 + h.idLength + 1 + 3;
  return h.iterations >= KEF_MIN_ITERATIONS && h.iterations <= KEF_MAX_ITERATIONS;
}

bool kefReadId(const std::vector<uint8_t>& envelope, std::string& id) {
  KefHeader h;
  if (!parseHeader(envelope, h)) return false;
  id.assign((const char*)&envelope[h.idOffset], h.idLength);
  return true;
}

KefResult kefDecrypt(const std::vector<uint8_t>& env, const std::string& password, std::vector<uint8_t>& plaintext) {
  plaintext.clear();
  KefHeader h;
  if (!parseHeader(env, h)) return KefMalformed;
  if (h.version != KEF_VERSION_GCM) return KefUnsupportedVersion;
  if (password.empty()) return KefWrongPassword;
  if (env.size() < h.payloadOffset + KEF_IV_LENGTH + 1 + KEF_TAG_LENGTH) return KefMalformed;

  const uint8_t* iv = &env[h.payloadOffset];
  size_t dataOffset = h.payloadOffset + KEF_IV_LENGTH;
  size_t dataLength = env.size() - dataOffset - KEF_TAG_LENGTH;
  const uint8_t* tag = &env[env.size() - KEF_TAG_LENGTH];

  uint8_t key[32];
  pbkdf2_hmac_sha256(
      (const uint8_t*)password.data(), password.size(), &env[h.idOffset], h.idLength, h.iterations, key, sizeof(key)
  );

  gcm_ctx ctx;
  plaintext.assign(env.begin() + dataOffset, env.begin() + dataOffset + dataLength);
  bool ok = gcm_init_and_key(key, sizeof(key), &ctx) == RETURN_GOOD &&
      gcm_decrypt_message(iv, KEF_IV_LENGTH, nullptr, 0, plaintext.data(), dataLength, tag, KEF_TAG_LENGTH, &ctx) ==
          RETURN_GOOD;
  gcm_end(&ctx);
  memzero(&ctx, sizeof(ctx));
  memzero(key, sizeof(key));

  if (!ok) {
    memzero(plaintext.data(), plaintext.size());
    plaintext.clear();
    return KefWrongPassword;
  }
  return KefOk;
}

bool base64Decode(const std::string& in, std::vector<uint8_t>& out) {
  out.clear();
  uint32_t buffer = 0;
  int bits = 0;
  bool padding = false;
  for (char c : in) {
    int v;
    if (c >= 'A' && c <= 'Z') v = c - 'A';
    else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
    else if (c >= '0' && c <= '9') v = c - '0' + 52;
    else if (c == '+') v = 62;
    else if (c == '/') v = 63;
    else if (c == '=') {
      padding = true;
      continue;
    } else if (c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
    else return false;
    if (padding) return false;  // data after '='
    buffer = buffer << 6 | v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back((buffer >> bits) & 0xff);
    }
  }
  return !out.empty();
}
