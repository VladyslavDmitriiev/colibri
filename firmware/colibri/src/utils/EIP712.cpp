// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "EIP712.h"

#include <algorithm>
#include <string>
#include <vector>

// Typed data comes from untrusted QR codes: every lookup is checked, every size is bounded and
// anything unexpected fails the whole hash instead of being encoded as zero.

namespace {

void keccak(const uint8_t* data, size_t len, uint8_t out[32]) {
  SHA3_CTX ctx;
  sha3_256_Init(&ctx);
  sha3_Update(&ctx, data, len);
  keccak_Final(&ctx, out);
}

// ==== Type names ====

bool isValidTypeName(const char* name) {
  if (name == nullptr || name[0] == 0) return false;
  size_t len = 0;
  for (const char* c = name; *c; c++, len++) {
    if (len >= EIP712_MAX_TYPE_NAME) return false;
    if (!isalnum((unsigned char)*c) && *c != '_' && *c != '$') return false;
  }
  return true;
}

// bit size of "uint<N>" / "int<N>", 0 if `type` is not one
int intBits(const char* type, bool& isSigned) {
  const char* digits;
  if (strncmp(type, "uint", 4) == 0) {
    isSigned = false;
    digits = type + 4;
  } else if (strncmp(type, "int", 3) == 0) {
    isSigned = true;
    digits = type + 3;
  } else {
    return 0;
  }
  if (*digits == 0) return 256;
  int bits = 0;
  for (const char* c = digits; *c; c++) {
    if (!isdigit((unsigned char)*c) || bits > 256) return 0;
    bits = bits * 10 + (*c - '0');
  }
  return (bits >= 8 && bits <= 256 && bits % 8 == 0 && digits[0] != '0') ? bits : 0;
}

// length of "bytes<N>", 0 for dynamic "bytes", -1 if `type` is not a bytes type
int bytesLength(const char* type) {
  if (strncmp(type, "bytes", 5) != 0) return -1;
  const char* digits = type + 5;
  if (*digits == 0) return 0;
  int len = 0;
  for (const char* c = digits; *c; c++) {
    if (!isdigit((unsigned char)*c) || len > 32) return -1;
    len = len * 10 + (*c - '0');
  }
  return (len >= 1 && len <= 32 && digits[0] != '0') ? len : -1;
}

bool isAtomic(const char* type) {
  bool isSigned;
  return strcmp(type, "address") == 0 || strcmp(type, "bool") == 0 || strcmp(type, "string") == 0 ||
      intBits(type, isSigned) > 0 || bytesLength(type) >= 0;
}

// "T[]" / "T[n]" -> element type T, `fixedLength` = n or -1; false if `type` is not an array
bool splitArrayType(const std::string& type, std::string& element, long& fixedLength) {
  if (type.empty() || type.back() != ']') return false;
  size_t open = type.rfind('[');
  if (open == std::string::npos || open == 0) return false;
  std::string length = type.substr(open + 1, type.size() - open - 2);
  element = type.substr(0, open);
  if (length.empty()) {
    fixedLength = -1;
    return true;
  }
  if (length.size() > 4 || length[0] == '0') return false;
  for (char c : length) {
    if (!isdigit((unsigned char)c)) return false;
  }
  fixedLength = atol(length.c_str());
  return true;
}

std::string baseType(const std::string& type) {
  size_t open = type.find('[');
  return open == std::string::npos ? type : type.substr(0, open);
}

// fields of struct `name`, false unless every field has a valid name and type
bool structFields(JsonObjectConst types, const char* name, JsonArrayConst& fields) {
  if (!isValidTypeName(name)) return false;
  fields = types[name].as<JsonArrayConst>();
  if (fields.isNull() || fields.size() > EIP712_MAX_FIELDS) return false;
  for (JsonVariantConst field : fields) {
    const char* fieldName = field["name"];
    const char* fieldType = field["type"];
    if (!isValidTypeName(fieldName) || fieldType == nullptr || strlen(fieldType) > EIP712_MAX_TYPE_NAME) return false;
  }
  return true;
}

bool isStruct(JsonObjectConst types, const std::string& type) {
  return !isAtomic(type.c_str()) && types[type.c_str()].is<JsonArrayConst>();
}

// ==== encodeType ====

bool findDependencies(JsonObjectConst types, const char* name, std::vector<std::string>& deps) {
  JsonArrayConst fields;
  if (!structFields(types, name, fields)) return false;
  for (JsonVariantConst field : fields) {
    std::string base = baseType(field["type"].as<const char*>());
    if (isAtomic(base.c_str())) continue;
    if (!isStruct(types, base)) return false;  // undefined type
    if (std::find(deps.begin(), deps.end(), base) != deps.end()) continue;
    if (deps.size() >= EIP712_MAX_DEPS) return false;
    deps.push_back(base);
    if (!findDependencies(types, base.c_str(), deps)) return false;
  }
  return true;
}

void appendStructType(JsonObjectConst types, const std::string& name, std::string& out) {
  out += name + "(";
  bool first = true;
  for (JsonVariantConst field : types[name.c_str()].as<JsonArrayConst>()) {
    if (!first) out += ",";
    first = false;
    out += field["type"].as<const char*>();
    out += " ";
    out += field["name"].as<const char*>();
  }
  out += ")";
}

bool typeHash(JsonObjectConst types, const char* name, uint8_t out[32]) {
  std::vector<std::string> deps;
  deps.push_back(name);
  if (!findDependencies(types, name, deps)) return false;
  std::sort(deps.begin() + 1, deps.end());

  std::string encoded;
  for (const std::string& dep : deps) {
    appendStructType(types, dep, encoded);
    if (encoded.size() > EIP712_MAX_TYPESTRING) return false;
  }
  keccak((const uint8_t*)encoded.data(), encoded.size(), out);
  return true;
}

// ==== Values ====

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// "0x" + an even number of hex digits
bool decodeHex(const char* hex, std::vector<uint8_t>& out, size_t maxLen) {
  if (hex == nullptr || hex[0] != '0' || (hex[1] != 'x' && hex[1] != 'X')) return false;
  hex += 2;
  size_t len = strlen(hex);
  if (len % 2 != 0 || len / 2 > maxLen) return false;
  out.resize(len / 2);
  for (size_t i = 0; i < len / 2; i++) {
    int hi = hexValue(hex[2 * i]), lo = hexValue(hex[2 * i + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = (uint8_t)(hi << 4 | lo);
  }
  return true;
}

// magnitude of a JSON integer, decimal string or 0x hex string, as 256 bit big-endian
bool parseInteger(JsonVariantConst value, uint8_t magnitude[32], bool& negative) {
  memset(magnitude, 0, 32);
  negative = false;

  if (value.is<uint64_t>() || value.is<int64_t>()) {
    uint64_t abs;
    if (value.is<uint64_t>()) {
      abs = value.as<uint64_t>();
    } else {
      int64_t v = value.as<int64_t>();
      negative = v < 0;
      abs = negative ? (uint64_t)(-(v + 1)) + 1 : (uint64_t)v;
    }
    for (int i = 0; i < 8; i++) magnitude[31 - i] = (abs >> (8 * i)) & 0xff;
    return true;
  }

  const char* str = value.as<const char*>();
  if (str == nullptr) return false;  // floats, null, objects...
  if (*str == '-') {
    negative = true;
    str++;
  }
  if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
    const char* digits = str + 2;
    size_t len = strlen(digits);
    if (len == 0 || len > 64) return false;
    for (size_t i = 0; i < len; i++) {
      int v = hexValue(digits[len - 1 - i]);
      if (v < 0) return false;
      magnitude[31 - i / 2] |= (i % 2) ? v << 4 : v;
    }
    return true;
  }

  size_t len = strlen(str);
  if (len == 0 || len > 78) return false;
  for (size_t i = 0; i < len; i++) {
    if (!isdigit((unsigned char)str[i])) return false;
    // magnitude = magnitude * 10 + digit
    uint32_t carry = str[i] - '0';
    for (int k = 31; k >= 0; k--) {
      uint32_t v = magnitude[k] * 10u + carry;
      magnitude[k] = v & 0xff;
      carry = v >> 8;
    }
    if (carry) return false;  // more than 256 bits
  }
  return true;
}

// true if `value` < 2^bits
bool fitsBits(const uint8_t value[32], int bits) {
  for (int bit = bits; bit < 256; bit++) {
    if (value[31 - bit / 8] & (1 << (bit % 8))) return false;
  }
  return true;
}

bool encodeInteger(JsonVariantConst value, int bits, bool isSigned, uint8_t out[32]) {
  bool negative;
  if (!parseInteger(value, out, negative)) return false;
  bool isZero = fitsBits(out, 0);
  if (!isSigned) return !negative || isZero ? fitsBits(out, bits) : false;

  if (!negative || isZero) return fitsBits(out, bits - 1);
  // -2^(bits-1) is the smallest value
  uint8_t limit[32] = {0};
  limit[31 - (bits - 1) / 8] = 1 << ((bits - 1) % 8);
  if (!fitsBits(out, bits - 1) && memcmp(out, limit, 32) != 0) return false;
  // two's complement over 256 bits
  uint16_t carry = 1;
  for (int k = 31; k >= 0; k--) {
    uint16_t v = (uint8_t)~out[k] + carry;
    out[k] = v & 0xff;
    carry = v >> 8;
  }
  return true;
}

bool hashStruct(JsonObjectConst types, const char* name, JsonVariantConst value, uint8_t out[32], int depth);

// encodeData() word of one value: atomic values in place, dynamic ones and structs hashed
bool encodeValue(JsonObjectConst types, const std::string& type, JsonVariantConst value, uint8_t out[32], int depth) {
  if (depth > EIP712_MAX_DEPTH) return false;

  std::string element;
  long fixedLength;
  if (splitArrayType(type, element, fixedLength)) {
    JsonArrayConst array = value.as<JsonArrayConst>();
    if (array.isNull() || array.size() > EIP712_MAX_ARRAY_ELEMENTS) return false;
    if (fixedLength >= 0 && array.size() != (size_t)fixedLength) return false;
    SHA3_CTX ctx;
    sha3_256_Init(&ctx);
    for (JsonVariantConst item : array) {
      uint8_t word[32];
      if (!encodeValue(types, element, item, word, depth + 1)) return false;
      sha3_Update(&ctx, word, 32);
    }
    keccak_Final(&ctx, out);
    return true;
  }

  if (isStruct(types, type)) return hashStruct(types, type.c_str(), value, out, depth + 1);

  const char* t = type.c_str();
  memset(out, 0, 32);
  bool isSigned;
  int bits = intBits(t, isSigned);
  int fixedBytes = bytesLength(t);

  if (bits > 0) return encodeInteger(value, bits, isSigned, out);

  if (strcmp(t, "bool") == 0) {
    if (!value.is<bool>()) return false;
    out[31] = value.as<bool>() ? 1 : 0;
    return true;
  }

  if (strcmp(t, "string") == 0) {
    if (!value.is<const char*>()) return false;
    JsonString str = value.as<JsonString>();
    keccak((const uint8_t*)str.c_str(), str.size(), out);
    return true;
  }

  std::vector<uint8_t> bytes;
  if (strcmp(t, "address") == 0) {
    if (!decodeHex(value.as<const char*>(), bytes, 20) || bytes.size() != 20) return false;
    memcpy(out + 12, bytes.data(), 20);
    return true;
  }
  if (fixedBytes > 0) {
    if (!decodeHex(value.as<const char*>(), bytes, fixedBytes) || bytes.size() != (size_t)fixedBytes) return false;
    memcpy(out, bytes.data(), fixedBytes);
    return true;
  }
  if (fixedBytes == 0) {
    if (!decodeHex(value.as<const char*>(), bytes, EIP712_MAX_BYTES)) return false;
    keccak(bytes.data(), bytes.size(), out);
    return true;
  }

  return false;  // unknown type
}

bool hashStruct(JsonObjectConst types, const char* name, JsonVariantConst value, uint8_t out[32], int depth) {
  if (depth > EIP712_MAX_DEPTH) return false;
  JsonObjectConst data = value.as<JsonObjectConst>();
  JsonArrayConst fields;
  if (data.isNull() || !structFields(types, name, fields)) return false;

  uint8_t word[32];
  if (!typeHash(types, name, word)) return false;

  SHA3_CTX ctx;
  sha3_256_Init(&ctx);
  sha3_Update(&ctx, word, 32);
  for (JsonVariantConst field : fields) {
    const char* fieldName = field["name"];
    JsonVariantConst fieldValue = data[fieldName];
    if (fieldValue.isNull()) return false;  // missing, not encoded as zero
    if (!encodeValue(types, field["type"].as<const char*>(), fieldValue, word, depth)) return false;
    sha3_Update(&ctx, word, 32);
  }
  keccak_Final(&ctx, out);
  return true;
}

}  // namespace

bool EIP712::computeTypedDataHash(JsonDocument& doc, uint8_t out[32]) {
  JsonObjectConst types = doc["types"].as<JsonObjectConst>();
  const char* primaryType = doc["primaryType"];
  if (types.isNull() || !isValidTypeName(primaryType)) return false;

  uint8_t buffer[2 + 32 + 32] = {0x19, 0x01};
  if (!hashStruct(types, "EIP712Domain", doc["domain"], buffer + 2, 0)) return false;

  // a request for the domain alone signs 0x1901 ‖ domainSeparator, like eth-sig-util
  if (strcmp(primaryType, "EIP712Domain") == 0) {
    keccak(buffer, 2 + 32, out);
    return true;
  }
  if (!hashStruct(types, primaryType, doc["message"], buffer + 2 + 32, 0)) return false;
  keccak(buffer, sizeof(buffer), out);
  return true;
}

bool EIP712::readUint(JsonVariantConst value, uint8_t out[32]) { return encodeInteger(value, 256, false, out); }

bool EIP712::readAddress(JsonVariantConst value, uint8_t out[20]) {
  std::vector<uint8_t> bytes;
  if (!decodeHex(value.as<const char*>(), bytes, 20) || bytes.size() != 20) return false;
  memcpy(out, bytes.data(), 20);
  return true;
}

bool EIP712::hasFields(JsonObjectConst types, const char* name, const char* fields) {
  JsonArrayConst list;
  if (!structFields(types, name, list)) return false;
  std::string encoded;
  for (JsonVariantConst field : list) {
    if (!encoded.empty()) encoded += ",";
    encoded += field["type"].as<const char*>();
    encoded += " ";
    encoded += field["name"].as<const char*>();
  }
  return encoded == fields;
}
