// SPDX-License-Identifier: AGPL-3.0-or-later
// Contains ported code from Trezor firmware licensed under GPL 3.0, see notes below.
#pragma once

#include "ethereum.h"

std::string ethGetAddress(HDNode* node) {
  uint8_t ethereumAddress[ADDRESS_LENGTH];
  hdnode_get_ethereum_pubkeyhash(node, ethereumAddress);

  return toHex(ethereumAddress, ADDRESS_LENGTH, true);
};

int ethSignRecoverableDigest(
    HDNode* node, uint8_t digest[HASH_LENGTH], uint8_t signature[RECOVERABLE_SIGNATURE_LENGTH]
) {
  // sign digest
  uint8_t v = 0;
  int result = hdnode_sign_digest(node, digest, signature, &v, NULL);

  // fix recovery bit for Ethereum's ecRecover
  signature[RECOVERABLE_SIGNATURE_LENGTH - 1] = (v + 27);

  // return toHex(signature, RECOVERABLE_SIGNATURE_LENGTH, true);
  return result;
};

/*
 * START ported and/or adapted code from Trezor firmware (licensed under GPL 3.0, see
 * `licenses/Trezor.GPL-3.0.txt`):
 * https://github.com/trezor/trezor-firmware/blob/29e03bd873977a498dbce79616bfb3fe4b7a0698/legacy/firmware/ethereum.c
 */

static void hashEthereumMessage(const uint8_t* message, size_t message_len, uint8_t hash[32]) {
  struct SHA3_CTX ctx = {0};
  sha3_256_Init(&ctx);
  sha3_Update(
      &ctx,
      (
          const uint8_t*
      )"\x19"
       "Ethereum Signed Message:\n",
      26
  );
  uint8_t c = 0;
  if (message_len >= 1000000000) {
    c = '0' + message_len / 1000000000 % 10;
    sha3_Update(&ctx, &c, 1);
  }
  if (message_len >= 100000000) {
    c = '0' + message_len / 100000000 % 10;
    sha3_Update(&ctx, &c, 1);
  }
  if (message_len >= 10000000) {
    c = '0' + message_len / 10000000 % 10;
    sha3_Update(&ctx, &c, 1);
  }
  if (message_len >= 1000000) {
    c = '0' + message_len / 1000000 % 10;
    sha3_Update(&ctx, &c, 1);
  }
  if (message_len >= 100000) {
    c = '0' + message_len / 100000 % 10;
    sha3_Update(&ctx, &c, 1);
  }
  if (message_len >= 10000) {
    c = '0' + message_len / 10000 % 10;
    sha3_Update(&ctx, &c, 1);
  }
  if (message_len >= 1000) {
    c = '0' + message_len / 1000 % 10;
    sha3_Update(&ctx, &c, 1);
  }
  if (message_len >= 100) {
    c = '0' + message_len / 100 % 10;
    sha3_Update(&ctx, &c, 1);
  }
  if (message_len >= 10) {
    c = '0' + message_len / 10 % 10;
    sha3_Update(&ctx, &c, 1);
  }
  c = '0' + message_len % 10;
  sha3_Update(&ctx, &c, 1);
  sha3_Update(&ctx, message, message_len);
  keccak_Final(&ctx, hash);
}

static void hashEthereumTypedData(
    const uint8_t domain_separator_hash[HASH_LENGTH], const uint8_t message_hash[HASH_LENGTH], bool has_message_hash,
    uint8_t hash[HASH_LENGTH]
) {
  struct SHA3_CTX ctx = {0};
  sha3_256_Init(&ctx);
  sha3_Update(&ctx, (const uint8_t*)"\x19\x01", 2);
  sha3_Update(&ctx, domain_separator_hash, HASH_LENGTH);
  if (has_message_hash) {
    sha3_Update(&ctx, message_hash, HASH_LENGTH);
  }
  keccak_Final(&ctx, hash);
}

std::string ethSignTypedDataHash(HDNode* node, std::string& domainSeparatorHash, std::string& messageHash) {
  uint8_t digest[HASH_LENGTH];

  uint8_t mhBytes[HASH_LENGTH];
  uint8_t dshBytes[HASH_LENGTH];
  fromHex(domainSeparatorHash.c_str(), dshBytes, HASH_LENGTH);

  bool hasMessageHash = messageHash.length() > 0 && messageHash != "0x";
  if (hasMessageHash) {
    fromHex(messageHash.c_str(), mhBytes, HASH_LENGTH);
  }

  hashEthereumTypedData(dshBytes, mhBytes, hasMessageHash, digest);

  uint8_t sigBytes[RECOVERABLE_SIGNATURE_LENGTH];
  ethSignRecoverableDigest(node, digest, sigBytes);
  return toHex(sigBytes, RECOVERABLE_SIGNATURE_LENGTH, true);
}

// ===== Ethereum transaction signing =====
static void hash_data(SHA3_CTX* ctx, const uint8_t* buf, size_t size) { sha3_Update(ctx, buf, size); }

static void hash_rlp_length(SHA3_CTX* ctx, uint32_t length, uint8_t firstbyte) {
  uint8_t buf[4] = {0};
  if (length == 1 && firstbyte <= 0x7f) {
    // empty length header
  } else if (length <= 55) {
    buf[0] = 0x80 + length;
    hash_data(ctx, buf, 1);
  } else if (length <= 0xff) {
    buf[0] = 0xb7 + 1;
    buf[1] = length;
    hash_data(ctx, buf, 2);
  } else if (length <= 0xffff) {
    buf[0] = 0xb7 + 2;
    buf[1] = length >> 8;
    buf[2] = length & 0xff;
    hash_data(ctx, buf, 3);
  } else {
    buf[0] = 0xb7 + 3;
    buf[1] = length >> 16;
    buf[2] = length >> 8;
    buf[3] = length & 0xff;
    hash_data(ctx, buf, 4);
  }
}

static void hash_rlp_field(SHA3_CTX* ctx, const uint8_t* buf, size_t size) {
  hash_rlp_length(ctx, size, size ? buf[0] : 0);
  hash_data(ctx, buf, size);
}

// encoded size of an RLP string, header included
static uint32_t rlp_calculate_length(uint32_t length, uint8_t firstbyte) {
  if (length == 1 && firstbyte <= 0x7f) return 1;
  if (length <= 55) return 1 + length;
  if (length <= 0xff) return 2 + length;
  if (length <= 0xffff) return 3 + length;
  return 4 + length;
}

static uint32_t rlp_calculate_number_length(uint64_t number) {
  uint32_t bytes = 0;
  for (uint64_t rest = number; rest > 0; rest >>= 8) bytes++;
  // 0 is the empty string
  return bytes == 1 && number <= 0x7f ? 1 : 1 + bytes;
}

static void hash_rlp_number(SHA3_CTX* ctx, uint64_t number) {
  if (!number) {
    hash_rlp_length(ctx, 0, 0);
    return;
  }
  uint8_t data[8] = {0};
  data[0] = (number >> 56) & 0xff;
  data[1] = (number >> 48) & 0xff;
  data[2] = (number >> 40) & 0xff;
  data[3] = (number >> 32) & 0xff;
  data[4] = (number >> 24) & 0xff;
  data[5] = (number >> 16) & 0xff;
  data[6] = (number >> 8) & 0xff;
  data[7] = (number) & 0xff;
  int offset = 0;
  while (!data[offset]) {
    offset++;
  }
  hash_rlp_field(ctx, data + offset, 8 - offset);
}

static void hash_rlp_list_length(SHA3_CTX* ctx, uint32_t length) {
  uint8_t buf[4] = {0};
  if (length <= 55) {
    buf[0] = 0xc0 + length;
    hash_data(ctx, buf, 1);
  } else if (length <= 0xff) {
    buf[0] = 0xf7 + 1;
    buf[1] = length;
    hash_data(ctx, buf, 2);
  } else if (length <= 0xffff) {
    buf[0] = 0xf7 + 2;
    buf[1] = length >> 8;
    buf[2] = length & 0xff;
    hash_data(ctx, buf, 3);
  } else {
    buf[0] = 0xf7 + 3;
    buf[1] = length >> 16;
    buf[2] = length >> 8;
    buf[3] = length & 0xff;
    hash_data(ctx, buf, 4);
  }
}

static void sign_transaction(
    const uint8_t* privkey, uint64_t chain_id, const uint8_t* nonce, size_t nonce_size, const uint8_t* gas_price,
    size_t gas_price_size, const uint8_t* gas_limit, size_t gas_limit_size, const uint8_t* to, size_t to_size,
    const uint8_t* value, size_t value_size, const uint8_t* data, size_t data_size, uint8_t* signature
) {
  SHA3_CTX ctx = {0};
  sha3_256_Init(&ctx);

  // Hash the transaction fields
  hash_rlp_list_length(
      &ctx, rlp_calculate_length(nonce_size, nonce[0]) + rlp_calculate_length(gas_price_size, gas_price[0]) +
                rlp_calculate_length(gas_limit_size, gas_limit[0]) + rlp_calculate_length(to_size, to[0]) +
                rlp_calculate_length(value_size, value[0]) + rlp_calculate_length(data_size, data[0]) +
                rlp_calculate_number_length(chain_id) + 2  // empty r and s
  );
  hash_rlp_field(&ctx, nonce, nonce_size);
  hash_rlp_field(&ctx, gas_price, gas_price_size);
  hash_rlp_field(&ctx, gas_limit, gas_limit_size);
  hash_rlp_field(&ctx, to, to_size);
  hash_rlp_field(&ctx, value, value_size);
  hash_rlp_field(&ctx, data, data_size);

  // Add chain_id, r, and s for EIP-155
  hash_rlp_number(&ctx, chain_id);
  hash_rlp_length(&ctx, 0, 0);
  hash_rlp_length(&ctx, 0, 0);

  uint8_t hash[32] = {0};
  keccak_Final(&ctx, hash);

  uint8_t v = 0;
  ecdsa_sign_digest(&secp256k1, privkey, hash, signature, &v, NULL);
  signature[RECOVERABLE_SIGNATURE_LENGTH - 1] = v + 27;
}

static void sign_eip1559_transaction(
    const uint8_t* privkey, uint64_t chain_id, const uint8_t* nonce, size_t nonce_size, const uint8_t* max_priority_fee,
    size_t max_priority_fee_size, const uint8_t* max_gas_fee, size_t max_gas_fee_size, const uint8_t* gas_limit,
    size_t gas_limit_size, const uint8_t* to, size_t to_size, const uint8_t* value, size_t value_size,
    const uint8_t* data, size_t data_size, uint8_t* signature
) {
  SHA3_CTX ctx = {0};
  sha3_256_Init(&ctx);

  // Hash the transaction fields
  const uint8_t txType = 2;  // EIP-1559 transaction type (EIP-2718)
  hash_data(&ctx, &txType, 1);
  hash_rlp_list_length(
      &ctx, rlp_calculate_number_length(chain_id) + rlp_calculate_length(nonce_size, nonce[0]) +
                rlp_calculate_length(max_priority_fee_size, max_priority_fee[0]) +
                rlp_calculate_length(max_gas_fee_size, max_gas_fee[0]) +
                rlp_calculate_length(gas_limit_size, gas_limit[0]) + rlp_calculate_length(to_size, to[0]) +
                rlp_calculate_length(value_size, value[0]) + rlp_calculate_length(data_size, data[0]) +
                1  // empty access list
  );
  hash_rlp_number(&ctx, chain_id);
  hash_rlp_field(&ctx, nonce, nonce_size);
  hash_rlp_field(&ctx, max_priority_fee, max_priority_fee_size);
  hash_rlp_field(&ctx, max_gas_fee, max_gas_fee_size);
  hash_rlp_field(&ctx, gas_limit, gas_limit_size);
  hash_rlp_field(&ctx, to, to_size);
  hash_rlp_field(&ctx, value, value_size);
  hash_rlp_field(&ctx, data, data_size);
  hash_rlp_list_length(&ctx, 0);  // access list

  uint8_t hash[32] = {0};
  keccak_Final(&ctx, hash);

  uint8_t v = 0;
  ecdsa_sign_digest(&secp256k1, privkey, hash, signature, &v, NULL);
  signature[RECOVERABLE_SIGNATURE_LENGTH - 1] = v + 27;
}

/*
 * END ported code from Trezor firmware
 */

std::string ethSignMessage(HDNode* node, std::string& message) {
  uint8_t digest[HASH_LENGTH];
  const char* msg = message.c_str();

  hashEthereumMessage((uint8_t*)msg, message.length(), digest);

  uint8_t sigBytes[RECOVERABLE_SIGNATURE_LENGTH];
  ethSignRecoverableDigest(node, digest, sigBytes);
  return toHex(sigBytes, RECOVERABLE_SIGNATURE_LENGTH, true);
}

int ethSignMessage(
    HDNode* node, const uint8_t* messageBytes, size_t messageLen, uint8_t signature[RECOVERABLE_SIGNATURE_LENGTH]
) {
  uint8_t digest[HASH_LENGTH];

  hashEthereumMessage(messageBytes, messageLen, digest);

  return ethSignRecoverableDigest(node, digest, signature);
}

static WalletResponse createWalletResponse(uint8_t signature[RECOVERABLE_SIGNATURE_LENGTH]) {
  std::string sig = toHex(signature, RECOVERABLE_SIGNATURE_LENGTH, true);
  if (sig.empty()) {
    return WalletResponse(InternalError, RPC_ERROR_SIGNATURE_FAILED);
  }

  return WalletResponse(sig);
}

WalletResponse ethSignTransaction(HDNode* node, EthTxData& tx, uint8_t signature[RECOVERABLE_SIGNATURE_LENGTH]) {
  if (tx.isEip1559) {
    sign_eip1559_transaction(
        node->private_key, tx.chainId, tx.nonce, tx.nonceSize, tx.maxPriorityFeePerGas, tx.maxPriorityFeePerGasSize,
        tx.maxGasFee, tx.maxGasFeeSize, tx.gasLimit, tx.gasLimitSize, tx.to, ADDRESS_LENGTH, tx.value, tx.valueSize,
        tx.data, tx.dataSize, signature
    );
  } else {
    sign_transaction(
        node->private_key, tx.chainId, tx.nonce, tx.nonceSize, tx.maxGasFee, tx.maxGasFeeSize, tx.gasLimit,
        tx.gasLimitSize, tx.to, ADDRESS_LENGTH, tx.value, tx.valueSize, tx.data, tx.dataSize, signature
    );
  }

  return createWalletResponse(signature);
}

// ===== Transaction review ===== //

extern "C" {
#include <address.h>
}

struct EthChainInfo {
  uint64_t chainId;
  const char* name;
  const char* symbol;
  bool isTestnet;
};

static const EthChainInfo ethChains[] = {
    {1, "Ethereum", "ETH", false},
    {10, "Optimism", "ETH", false},
    {56, "BNB Chain", "BNB", false},
    {97, "BNB Testnet", "tBNB", true},
    {137, "Polygon", "POL", false},
    {8453, "Base", "ETH", false},
    {17000, "Holesky", "ETH", true},
    {42161, "Arbitrum", "ETH", false},
    {11155111, "Sepolia", "ETH", true},
};

struct EthTokenInfo {
  uint64_t chainId;
  const char* address;
  const char* symbol;
  uint8_t decimals;
};

// well known tokens, to show amounts in their units
static const EthTokenInfo ethTokens[] = {
    {1, "0xdAC17F958D2ee523a2206206994597C13D831ec7", "USDT", 6},
    {1, "0xA0b86991c6218b36c1d19D4a2e9Eb0cE3606eB48", "USDC", 6},
    {1, "0x6B175474E89094C44Da98b954EedeAC495271d0F", "DAI", 18},
    {1, "0xC02aaA39b223FE8D0A0e5C4F27eAD9083C756Cc2", "WETH", 18},
    {1, "0x2260FAC5E5542a773Aa44fBCfeDf7C193bc2C599", "WBTC", 8},
    {10, "0x0b2C639c533813f4Aa9D7837CAf62653d097Ff85", "USDC", 6},
    {56, "0x55d398326f99059fF775485246999027B3197955", "USDT", 18},
    {56, "0x8AC76a51cc950d9822D68b83fE1Ad97B32Cd580d", "USDC", 18},
    {56, "0xbb4CdB9CBd36B01bD1cBaEBF2De08d9173bc095c", "WBNB", 18},
    {137, "0xc2132D05D31c914a87C6611C10748AEb04B58e8F", "USDT", 6},
    {137, "0x3c499c542cEF5E3811e1192ce70d8cC03d5c3359", "USDC", 6},
    {8453, "0x833589fCD6eDb6E08f4c7C32D4f71b54bdA02913", "USDC", 6},
    {42161, "0xFd086bC7CD5C481DCC9C85ebE478A1C0b69FCbb9", "USDT", 6},
    {42161, "0xaf88d065e77c8cC2239327C5EDb3A432268e5831", "USDC", 6},
    {42161, "0x82aF49447D8a07e3bd95BD0d56f35241523fBab1", "WETH", 18},
};

static const EthChainInfo* ethFindChain(uint64_t chainId) {
  for (const EthChainInfo& c : ethChains) {
    if (c.chainId == chainId) return &c;
  }
  return nullptr;
}

static const EthTokenInfo* ethFindToken(uint64_t chainId, const uint8_t address[ADDRESS_LENGTH]) {
  uint8_t tokenAddress[ADDRESS_LENGTH];
  for (const EthTokenInfo& t : ethTokens) {
    if (t.chainId != chainId) continue;
    fromHex(t.address, tokenAddress, ADDRESS_LENGTH);
    if (memcmp(tokenAddress, address, ADDRESS_LENGTH) == 0) return &t;
  }
  return nullptr;
}

// big-endian integer of up to 32 bytes, right-aligned into 32 bytes
static void ethReadUint256(const uint8_t* be, size_t len, uint8_t out[32]) {
  if (len > 32) len = 32;
  memset(out, 0, 32);
  memcpy(out + 32 - len, be, len);
}

// out = a * b, false if the product does not fit 256 bits
static bool ethMulUint256(const uint8_t a[32], const uint8_t b[32], uint8_t out[32]) {
  uint32_t acc[64] = {0};
  for (int i = 31; i >= 0; i--) {
    for (int j = 31; j >= 0; j--) {
      acc[i + j + 1] += (uint32_t)a[i] * b[j];
    }
  }
  for (int k = 63; k > 0; k--) {
    acc[k - 1] += acc[k] >> 8;
    acc[k] &= 0xff;
  }
  for (int k = 0; k < 32; k++) {
    if (acc[k]) return false;
  }
  for (int k = 0; k < 32; k++) out[k] = acc[32 + k];
  return true;
}

static std::string ethFormatUint256(const uint8_t value[32], unsigned int decimals, const char* suffix) {
  bignum256 bn;
  bn_read_be(value, &bn);
  char out[100];
  if (!bn_format(&bn, NULL, suffix, decimals, 0, false, 0, out, sizeof(out))) return "?";
  return out;
}

// EIP-55 address in groups of 4 chars, easier to compare with the address shown by the app
static std::string ethFormatAddress(const uint8_t* address) {
  char hex[2 + 2 * ADDRESS_LENGTH + 1];
  ethereum_address_checksum(address, hex, false, 0);
  std::string out(hex, 6);
  for (size_t i = 6; i < 2 + 2 * ADDRESS_LENGTH; i += 4) {
    out += " ";
    out.append(hex + i, 4);
  }
  return out;
}

// ABI-encoded address argument, false if the upper 12 bytes are not zero
static bool ethReadAbiAddress(const uint8_t* word, uint8_t address[ADDRESS_LENGTH]) {
  if (!isAllZero(word, 32 - ADDRESS_LENGTH)) return false;
  memcpy(address, word + 32 - ADDRESS_LENGTH, ADDRESS_LENGTH);
  return true;
}

static std::string ethFormatTokenAmount(const uint8_t amount[32], const EthTokenInfo* token) {
  if (token) return ethFormatUint256(amount, token->decimals, (std::string(" ") + token->symbol).c_str());
  return ethFormatUint256(amount, 0, " units");
}

static const uint8_t SELECTOR_TRANSFER[4] = {0xa9, 0x05, 0x9c, 0xbb};  // transfer(address,uint256)
static const uint8_t SELECTOR_APPROVE[4] = {0x09, 0x5e, 0xa7, 0xb3};  // approve(address,uint256)
static const uint8_t SELECTOR_INCREASE_ALLOWANCE[4] = {0x39, 0x50, 0x93, 0x51};  // increaseAllowance(address,uint256)
static const uint8_t SELECTOR_SET_APPROVAL_FOR_ALL[4] = {0xa2, 0x2c, 0xb4, 0x65};  // setApprovalForAll(address,bool)

// Plain language summary of a transaction for the user to review before signing, with
// warnings for common scams and mistakes. Lines are styled with DISPLAY_LINE_* prefixes.
// `isBlind` is set when the device can't tell what the transaction does.
std::string ethFormatTransaction(const EthTxData& tx, bool* isBlind) {
  const EthChainInfo* chain = ethFindChain(tx.chainId);
  std::string nativeSymbol = chain ? std::string(" ") + chain->symbol : " coins";
  uint8_t zeroAddress[ADDRESS_LENGTH] = {0};
  uint8_t value[32], num[32];
  ethReadUint256(tx.value, tx.valueSize, value);
  bool hasValue = !isAllZero(value, 32);

  // two arg calls: (address, uint256 / bool)
  uint8_t arg[ADDRESS_LENGTH];
  bool hasTwoArgs = tx.dataSize == 4 + 2 * 32 && ethReadAbiAddress(tx.data + 4, arg);
  const uint8_t* amount = tx.data + 4 + 32;
  bool isCall = tx.dataSize > 0;
  auto isSelector = [&](const uint8_t selector[4]) { return hasTwoArgs && memcmp(tx.data, selector, 4) == 0; };

  std::string text;
  std::string alerts;
  if (isBlind) *isBlind = false;

  if (!isCall) {
    text += DISPLAY_LINE_TITLE "SEND\n";
    text += DISPLAY_LINE_BIG + ethFormatUint256(value, 18, nativeSymbol.c_str()) + "\n";
    text += "To:\n" + ethFormatAddress(tx.to) + "\n";
    if (memcmp(tx.to, zeroAddress, ADDRESS_LENGTH) == 0) {
      alerts += DISPLAY_LINE_ALERT "The zero address: coins sent there are lost forever!\n";
    }
  } else if (isSelector(SELECTOR_TRANSFER) && !hasValue) {
    const EthTokenInfo* token = ethFindToken(tx.chainId, tx.to);
    text += DISPLAY_LINE_TITLE "SEND TOKEN\n";
    text += DISPLAY_LINE_BIG + ethFormatTokenAmount(amount, token) + "\n";
    text += "To:\n" + ethFormatAddress(arg) + "\n";
    if (!token) {
      text += "Token contract:\n" + ethFormatAddress(tx.to) + "\n";
      text += DISPLAY_LINE_NOTE "Unknown token, amount in its smallest units\n";
    }
    if (memcmp(arg, tx.to, ADDRESS_LENGTH) == 0) {
      alerts += DISPLAY_LINE_ALERT "Sending a token to its own contract: it is lost forever!\n";
    } else if (memcmp(arg, zeroAddress, ADDRESS_LENGTH) == 0) {
      alerts += DISPLAY_LINE_ALERT "The zero address: tokens sent there are lost forever!\n";
    }
  } else if ((isSelector(SELECTOR_APPROVE) || isSelector(SELECTOR_INCREASE_ALLOWANCE)) && !hasValue) {
    const EthTokenInfo* token = ethFindToken(tx.chainId, tx.to);
    uint8_t max[32];
    memset(max, 0xff, sizeof(max));
    bool isUnlimited = memcmp(amount, max, 32) == 0;
    std::string symbol = token ? token->symbol : "tokens";
    bool isRevoke = isAllZero(amount, 32) && isSelector(SELECTOR_APPROVE);

    if (isRevoke) {
      text += DISPLAY_LINE_TITLE "REMOVE SPENDING PERMISSION\n";
      text += "Who can no longer spend your " + symbol + ":\n" + ethFormatAddress(arg) + "\n";
    } else {
      text += std::string(isUnlimited ? DISPLAY_LINE_DANGER : DISPLAY_LINE_WARNING) + "ALLOW SPENDING\n";
      text += DISPLAY_LINE_BIG + (isUnlimited ? "ALL your " + symbol : ethFormatTokenAmount(amount, token)) + "\n";
      text += "Who can spend it:\n" + ethFormatAddress(arg) + "\n";
      text += DISPLAY_LINE_NOTE "They can take it any time without asking you again. Allow only apps you trust.\n";
    }
    if (!token) text += "Token contract:\n" + ethFormatAddress(tx.to) + "\n";
  } else if (isSelector(SELECTOR_SET_APPROVAL_FOR_ALL) && !hasValue && isAllZero(amount, 31) && amount[31] <= 1) {
    if (amount[31]) {
      text += DISPLAY_LINE_DANGER "ALLOW ACCESS TO ALL YOUR NFTs\n";
      text += "Who gets access:\n" + ethFormatAddress(arg) + "\n";
      text += DISPLAY_LINE_NOTE "They can take every NFT of this collection you own. Common scam!\n";
    } else {
      text += DISPLAY_LINE_TITLE "REMOVE NFT ACCESS\n";
      text += "Who loses access:\n" + ethFormatAddress(arg) + "\n";
    }
    text += "Collection:\n" + ethFormatAddress(tx.to) + "\n";
  } else {
    if (isBlind) *isBlind = true;
    text += DISPLAY_LINE_WARNING "CONTRACT INTERACTION\n";
    text += DISPLAY_LINE_NOTE "This device can't read what it does. Sign only if you trust the app.\n";
    if (hasValue) text += DISPLAY_LINE_BIG "Pays " + ethFormatUint256(value, 18, nativeSymbol.c_str()) + "\n";
    text += "Contract:\n" + ethFormatAddress(tx.to) + "\n";
    if (tx.dataSize >= 4) text += DISPLAY_LINE_NOTE "Action ID " + toHex(tx.data, 4, true) + "\n";
  }

  // network and worst case fee: gas limit * max fee per gas (gas price for legacy txs)
  text += "\nNetwork: ";
  text += chain ? chain->name : "unknown (ID " + std::to_string(tx.chainId) + ")";
  text += "\n";
  if (chain && chain->isTestnet) text += DISPLAY_LINE_NOTE "Test network, no real money\n";
  if (!chain) alerts += DISPLAY_LINE_ALERT "Unknown network: check it in your app\n";

  uint8_t gasLimit[32], gasPrice[32];
  ethReadUint256(tx.gasLimit, tx.gasLimitSize, gasLimit);
  ethReadUint256(tx.maxGasFee, tx.maxGasFeeSize, gasPrice);
  if (ethMulUint256(gasLimit, gasPrice, num)) {
    text += "Network fee: up to " + ethFormatUint256(num, 18, nativeSymbol.c_str()) + "\n";
    if (hasValue && memcmp(num, value, 32) > 0) {
      alerts += DISPLAY_LINE_ALERT "The fee is higher than the amount!\n";
    }
  } else {
    alerts += DISPLAY_LINE_ALERT "Absurdly high network fee!\n";
  }

  if (!alerts.empty()) text += "\n" + alerts;
  while (!text.empty() && text.back() == '\n') text.pop_back();
  return text;
}

// ===== Typed data review (EIP-712) ===== //

#include "../utils/EIP712.h"

static const char PERMIT2_ADDRESS[] = "0x000000000022D473030F116dDEE9F6B43aC78BA3";
static const size_t TYPED_DATA_MAX_TOKENS_SHOWN = 4;

static bool ethToUint64(const uint8_t value[32], uint64_t& out) {
  if (!isAllZero(value, 24)) return false;
  out = 0;
  for (int i = 24; i < 32; i++) out = out << 8 | value[i];
  return true;
}

// UTC date of a unix timestamp, "never" when absurdly far away (Permit2 uses max uint48)
static std::string ethFormatDate(const uint8_t value[32]) {
  uint64_t t;
  if (!ethToUint64(value, t) || t >= 253402300800ULL) return "never";  // year 10000
  // days to civil date, Howard Hinnant's algorithm
  int64_t days = t / 86400 + 719468;
  uint32_t secs = t % 86400;
  int64_t era = days / 146097;
  uint32_t doe = days - era * 146097;
  uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  uint32_t mp = (5 * doy + 2) / 153;
  uint32_t day = doy - (153 * mp + 2) / 5 + 1;
  uint32_t month = mp < 10 ? mp + 3 : mp - 9;
  int64_t year = yoe + era * 400 + (month <= 2);
  char out[32];
  snprintf(out, sizeof(out), "%04d-%02u-%02u %02u:%02u UTC", (int)year, month, day, secs / 3600, secs / 60 % 60);
  return out;
}

// strings from the request may carry line style bytes or newlines to fake parts of the review
static std::string ethSanitize(const char* str, size_t maxLen = 32) {
  std::string out;
  for (const char* c = str ? str : ""; *c && out.size() < maxLen; c++) out += (*c >= 0x20 && *c < 0x7f) ? *c : '?';
  if (str && strlen(str) > maxLen) out += "...";
  return out;
}

// at least 2^160 - 1: the max of Permit2 amounts and anything beyond, nobody owns that many tokens
static bool ethIsUnlimited(const uint8_t amount[32]) {
  if (!isAllZero(amount, 12)) return true;
  for (int i = 12; i < 32; i++) {
    if (amount[i] != 0xff) return false;
  }
  return true;
}

static std::string ethFormatAllowance(const uint8_t amount[32], const EthTokenInfo* token) {
  if (ethIsUnlimited(amount)) return std::string("ALL your ") + (token ? token->symbol : "tokens");
  return ethFormatTokenAmount(amount, token);
}

// Plain language summary of EIP-712 typed data, like ethFormatTransaction(). Only exact
// schemas of token permits are recognized, everything else sets `isBlind`.
// Call after EIP712::computeTypedDataHash() succeeded: values are known to be valid then.
std::string ethFormatTypedData(JsonDocument& doc, bool* isBlind) {
  JsonObjectConst types = doc["types"].as<JsonObjectConst>();
  JsonObjectConst domain = doc["domain"].as<JsonObjectConst>();
  JsonObjectConst message = doc["message"].as<JsonObjectConst>();
  const char* primaryType = doc["primaryType"] | "";
  auto is = [&](const char* type, const char* fields) {
    return strcmp(primaryType, type) == 0 && EIP712::hasFields(types, type, fields);
  };

  // domain fields count only if they are part of the signed EIP712Domain type
  uint64_t chainId = 0;
  bool hasChain = false, hasContract = false;
  uint8_t contract[ADDRESS_LENGTH], num[32];
  for (JsonVariantConst field : types["EIP712Domain"].as<JsonArrayConst>()) {
    const char* name = field["name"] | "";
    const char* type = field["type"] | "";
    if (strcmp(name, "chainId") == 0 && strncmp(type, "uint", 4) == 0) {
      hasChain = EIP712::readUint(domain[name], num) && ethToUint64(num, chainId);
    } else if (strcmp(name, "verifyingContract") == 0 && strcmp(type, "address") == 0) {
      hasContract = EIP712::readAddress(domain[name], contract);
    }
  }
  const EthChainInfo* chain = hasChain ? ethFindChain(chainId) : nullptr;
  uint8_t permit2[ADDRESS_LENGTH];
  fromHex(PERMIT2_ADDRESS, permit2, ADDRESS_LENGTH);

  std::string text, alerts;
  if (isBlind) *isBlind = false;
  uint8_t spender[ADDRESS_LENGTH], token[ADDRESS_LENGTH], amount[32], deadline[32];

  auto spenderLines = [&](const char* label) {
    text += std::string(label) + "\n" + ethFormatAddress(spender) + "\n";
    if (memcmp(spender, permit2, ADDRESS_LENGTH) == 0) text += DISPLAY_LINE_NOTE "Permit2, Uniswap's approval contract\n";
  };

  bool isDai = is("Permit", "address holder,address spender,uint256 nonce,uint256 expiry,bool allowed");
  if (hasContract && (isDai || is("Permit", "address owner,address spender,uint256 value,uint256 nonce,uint256 deadline"))) {
    // ERC-2612 permit, or DAI's older one with an allowed flag and expiry 0 = never
    const EthTokenInfo* info = hasChain ? ethFindToken(chainId, contract) : nullptr;
    EIP712::readAddress(message["spender"], spender);
    if (isDai) {
      memset(amount, message["allowed"].as<bool>() ? 0xff : 0, 32);
      EIP712::readUint(message["expiry"], deadline);
      if (isAllZero(deadline, 32)) memset(deadline, 0xff, 32);
    } else {
      EIP712::readUint(message["value"], amount);
      EIP712::readUint(message["deadline"], deadline);
    }
    std::string symbol = info ? info->symbol : "tokens";

    if (isAllZero(amount, 32)) {
      text += DISPLAY_LINE_TITLE "REMOVE SPENDING PERMISSION\n";
      spenderLines(("Who can no longer spend your " + symbol + ":").c_str());
    } else {
      text += std::string(ethIsUnlimited(amount) ? DISPLAY_LINE_DANGER : DISPLAY_LINE_WARNING) + "ALLOW SPENDING\n";
      text += DISPLAY_LINE_BIG + ethFormatAllowance(amount, info) + "\n";
      spenderLines("Who can spend it:");
      text += "Until: " + ethFormatDate(deadline) + "\n";
      text += DISPLAY_LINE_NOTE "Signing is free, but they can take it later without asking you. Allow only apps you trust.\n";
    }
    if (!info) text += "Token contract:\n" + ethFormatAddress(contract) + "\n";

  } else if (hasContract && EIP712::hasFields(types, "PermitDetails", "address token,uint160 amount,uint48 expiration,uint48 nonce") &&
             (is("PermitSingle", "PermitDetails details,address spender,uint256 sigDeadline") ||
              is("PermitBatch", "PermitDetails[] details,address spender,uint256 sigDeadline"))) {
    // Permit2 allowance for one or more tokens
    JsonArrayConst list = message["details"].as<JsonArrayConst>();
    std::vector<JsonVariantConst> details;
    if (list.isNull()) {
      details.push_back(message["details"]);
    } else {
      for (JsonVariantConst d : list) details.push_back(d);
    }
    EIP712::readAddress(message["spender"], spender);

    bool anyUnlimited = false;
    std::string tokens;
    for (size_t i = 0; i < details.size() && i < TYPED_DATA_MAX_TOKENS_SHOWN; i++) {
      EIP712::readAddress(details[i]["token"], token);
      EIP712::readUint(details[i]["amount"], amount);
      EIP712::readUint(details[i]["expiration"], deadline);
      const EthTokenInfo* info = hasChain ? ethFindToken(chainId, token) : nullptr;
      anyUnlimited |= ethIsUnlimited(amount);
      tokens += DISPLAY_LINE_BIG + ethFormatAllowance(amount, info) + "\n";
      if (!info) tokens += "Token contract:\n" + ethFormatAddress(token) + "\n";
      tokens += "Until: " + ethFormatDate(deadline) + "\n";
    }
    text += std::string(anyUnlimited ? DISPLAY_LINE_DANGER : DISPLAY_LINE_WARNING) + "ALLOW SPENDING\n" + tokens;
    if (details.size() > TYPED_DATA_MAX_TOKENS_SHOWN) {
      alerts += DISPLAY_LINE_ALERT "And " + std::to_string(details.size() - TYPED_DATA_MAX_TOKENS_SHOWN) +
          " more tokens not shown!\n";
    }
    spenderLines("Who can spend it:");
    text += DISPLAY_LINE_NOTE "Signing is free, but they can take it later without asking you. Allow only apps you trust.\n";
    if (memcmp(contract, permit2, ADDRESS_LENGTH) != 0) alerts += DISPLAY_LINE_ALERT "Not the real Permit2 contract!\n";

  } else if (hasContract && EIP712::hasFields(types, "TokenPermissions", "address token,uint256 amount") &&
             (is("PermitTransferFrom", "TokenPermissions permitted,address spender,uint256 nonce,uint256 deadline") ||
              is("PermitBatchTransferFrom", "TokenPermissions[] permitted,address spender,uint256 nonce,uint256 deadline"))) {
    // Permit2 one-time transfer
    JsonArrayConst list = message["permitted"].as<JsonArrayConst>();
    std::vector<JsonVariantConst> permitted;
    if (list.isNull()) {
      permitted.push_back(message["permitted"]);
    } else {
      for (JsonVariantConst p : list) permitted.push_back(p);
    }
    EIP712::readAddress(message["spender"], spender);
    EIP712::readUint(message["deadline"], deadline);

    text += DISPLAY_LINE_WARNING "ALLOW ONE TRANSFER\n";
    for (size_t i = 0; i < permitted.size() && i < TYPED_DATA_MAX_TOKENS_SHOWN; i++) {
      EIP712::readAddress(permitted[i]["token"], token);
      EIP712::readUint(permitted[i]["amount"], amount);
      const EthTokenInfo* info = hasChain ? ethFindToken(chainId, token) : nullptr;
      text += DISPLAY_LINE_BIG + ethFormatAllowance(amount, info) + "\n";
      if (!info) text += "Token contract:\n" + ethFormatAddress(token) + "\n";
    }
    if (permitted.size() > TYPED_DATA_MAX_TOKENS_SHOWN) {
      alerts += DISPLAY_LINE_ALERT "And " + std::to_string(permitted.size() - TYPED_DATA_MAX_TOKENS_SHOWN) +
          " more tokens not shown!\n";
    }
    spenderLines("Who can take it:");
    text += "Until: " + ethFormatDate(deadline) + "\n";
    text += DISPLAY_LINE_NOTE "They can take this once, without asking you again.\n";
    if (memcmp(contract, permit2, ADDRESS_LENGTH) != 0) alerts += DISPLAY_LINE_ALERT "Not the real Permit2 contract!\n";

  } else {
    if (isBlind) *isBlind = true;
    text += DISPLAY_LINE_WARNING "SIGN DATA\n";
    text += DISPLAY_LINE_NOTE "This device can't read what it allows. Sign only if you trust the app.\n";
    text += "App: " + ethSanitize(domain["name"] | "?") + "\n";
    text += "Type: " + ethSanitize(primaryType) + "\n";
    if (hasContract) text += "Contract:\n" + ethFormatAddress(contract) + "\n";
  }

  text += "\nNetwork: ";
  if (!hasChain) {
    text += "any\n";
    alerts += DISPLAY_LINE_ALERT "Valid on every network!\n";
  } else {
    text += chain ? chain->name : "unknown (ID " + std::to_string(chainId) + ")";
    text += "\n";
    if (chain && chain->isTestnet) text += DISPLAY_LINE_NOTE "Test network, no real money\n";
    if (!chain) alerts += DISPLAY_LINE_ALERT "Unknown network: check it in your app\n";
  }

  if (!alerts.empty()) text += "\n" + alerts;
  while (!text.empty() && text.back() == '\n') text.pop_back();
  return text;
}
