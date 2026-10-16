// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "../../config.h"

#include <ArduinoJson.h>

extern "C" {
#include <sha3.h>
}

// ==== Security Limits (Hardware Wallet Safe) ====

#define EIP712_MAX_TYPESTRING     1024  // encodeType() of a struct with its dependencies
#define EIP712_MAX_DEPS           16  // struct types referenced by one struct
#define EIP712_MAX_FIELDS         32
#define EIP712_MAX_ARRAY_ELEMENTS 64
#define EIP712_MAX_DEPTH          6  // nested structs and arrays
#define EIP712_MAX_TYPE_NAME      64
#define EIP712_MAX_BYTES          4096  // `bytes` values

class EIP712 {
 public:
  // keccak256(0x1901 ‖ hashStruct(domain) ‖ hashStruct(message)) of eth_signTypedData_v4 JSON.
  // False for malformed or unsupported typed data, never sign `out` then.
  static bool computeTypedDataHash(JsonDocument& doc, uint8_t out[32]);

  // values parsed exactly like the hash does, for showing them to the user
  static bool readUint(JsonVariantConst value, uint8_t out[32]);
  static bool readAddress(JsonVariantConst value, uint8_t out[20]);
  // true if struct `name` has exactly these fields, `fields` is "type name,type name,..."
  static bool hasFields(JsonObjectConst types, const char* name, const char* fields);
};
