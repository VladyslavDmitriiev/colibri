// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "Stateless.h"

#ifdef STATELESS_MODE

  #include "../ui/ui.h"
  #include "../utils/Kef.h"
  #include "SdCard.h"
  #include "Wallet.h"

extern "C" {
  #include <bip39.h>
  #include <memzero.h>
}

extern Wallet wallet;

  #define KERN_MNEMONICS_DIR "/kern/mnemonics"
  #define KERN_MNEMONIC_EXT ".kef"
  #define STATELESS_MAX_WALLETS 6  // buttons that fit the screen

struct KernWallet {
  std::string name;
  std::vector<uint8_t> envelope;
};

static void wipe(std::string& s) {
  memzero((void*)s.data(), s.size());
  s.clear();
}

static void wipe(std::vector<uint8_t>& v) {
  memzero(v.data(), v.size());
  v.clear();
}

// wallet names come from the SD card, keep only what the font can show
static std::string printable(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (out.size() >= 24) break;
    out += (c >= 0x20 && c < 0x7f) ? c : '?';
  }
  return out;
}

static bool readKernWallets(std::vector<KernWallet>& wallets) {
  wallets.clear();
  std::vector<std::string> files;
  if (!sdListFiles(KERN_MNEMONICS_DIR, KERN_MNEMONIC_EXT, files)) return false;
  for (const std::string& file : files) {
    if (wallets.size() >= STATELESS_MAX_WALLETS) break;
    std::string base64;
    KernWallet w;
    std::string id;
    if (!sdReadFile(std::string("/") + KERN_MNEMONICS_DIR + "/" + file, base64, 2 * KEF_MAX_ENVELOPE)) continue;
    if (!base64Decode(base64, w.envelope) || !kefReadId(w.envelope, id)) {
      log_w("Not a KEF envelope: %s", file.c_str());
      continue;
    }
    w.name = printable(id);
    wallets.push_back(w);
  }
  return true;
}

void statelessShowLocked() { displayMessage(DISPLAY_LOCKED_STATELESS); }

bool statelessUnlock(TextScanner scan) {
  displayMessage(DISPLAY_READING_SD);
  std::vector<KernWallet> wallets;
  bool hasCard = sdMount();
  bool listed = hasCard && readKernWallets(wallets);
  sdUnmount();
  if (!hasCard) {
    waitForDismiss(DISPLAY_NO_SD_CARD);
    return false;
  }
  if (!listed || wallets.empty()) {
    waitForDismiss(DISPLAY_NO_KERN_WALLET);
    return false;
  }

  size_t chosen = 0;
  if (wallets.size() > 1) {
    std::vector<std::string> names;
    for (const KernWallet& w : wallets) names.push_back(w.name);
    int choice = chooseOption("Which wallet?", names);
    if (choice < 0) return false;
    chosen = choice;
  }
  KernWallet& w = wallets[chosen];

  std::vector<uint8_t> entropy;
  while (true) {
    std::string password;
    if (!readText(("Password of " + w.name).c_str(), password, true, scan)) return false;
    displayMessage(DISPLAY_DECRYPTING);
    uint64_t start = millis();
    KefResult r = kefDecrypt(w.envelope, password, entropy);
    log_i("KEF decryption: %d after %d ms", r, (int)(millis() - start));
    wipe(password);
    if (r == KefOk) break;
    if (r != KefWrongPassword) {
      waitForDismiss(DISPLAY_KEF_UNSUPPORTED);
      return false;
    }
    displayMessage(DISPLAY_WRONG_PASSWORD);
    delay(1500);
  }

  // Kern encrypts the compact SeedQR: the BIP39 entropy
  bool validEntropy = entropy.size() >= 16 && entropy.size() <= 32 && entropy.size() % 4 == 0;
  const char* mnemonic = validEntropy ? mnemonic_from_data(entropy.data(), entropy.size()) : nullptr;
  wipe(entropy);
  if (!mnemonic) {
    waitForDismiss(DISPLAY_KEF_UNSUPPORTED);
    return false;
  }

  std::string passphrase;
  int choice = chooseOption("BIP39 passphrase?", {"No passphrase", "Enter passphrase"});
  if (choice < 0 || (choice == 1 && !readText("BIP39 passphrase", passphrase, true, scan))) {
    mnemonic_clear();
    return false;
  }

  displayMessage(DISPLAY_DECRYPTING);
  WalletResponse r = wallet.unlockStateless(mnemonic, passphrase.c_str());
  mnemonic_clear();
  wipe(passphrase);
  if (r.status < Ok) {
    waitForDismiss(DISPLAY_KEF_UNSUPPORTED);
    return false;
  }

  // the user compares the address with the one their wallet app knows
  // like wallet apps: start and end big, as people compare those, the whole address below
  std::string address = wallet.getAddress();
  std::string grouped = address.substr(0, 6);
  for (size_t i = 6; i < address.size(); i += 4) grouped += " " + address.substr(i, 4);
  std::string confirm = DISPLAY_LINE_TITLE "YOUR WALLET\n" + w.name + "\n\n" DISPLAY_LINE_BIG + address.substr(0, 6) +
      "..." + address.substr(address.size() - 4) + "\n" DISPLAY_LINE_NOTE + grouped +
      "\n\n" DISPLAY_LINE_NOTE "Same start and end as in your wallet app? A typo in the passphrase gives another wallet.";
  if (!waitForApproval(confirm.c_str())) {
    wallet.lock();
    return false;
  }
  return true;
}

#endif
