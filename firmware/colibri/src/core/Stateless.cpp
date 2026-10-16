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

static std::string unlockedName;

// the user compares the address with the one their wallet app knows; like wallet apps
// the start and end are big, as people compare those, the whole address below
static bool confirmAddress(const char* title) {
  std::string address = wallet.getAddress();
  std::string grouped = address.substr(0, 6);
  for (size_t i = 6; i < address.size(); i += 4) grouped += " " + address.substr(i, 4);
  std::string confirm = std::string(DISPLAY_LINE_TITLE) + title + "\n" + unlockedName +
      (wallet.hasStatelessPassphrase() ? " + passphrase" : "") + "\n\n" DISPLAY_LINE_BIG + address.substr(0, 6) +
      "..." + address.substr(address.size() - 4) + "\n" DISPLAY_LINE_NOTE + grouped +
      "\n\n" DISPLAY_LINE_NOTE "Same start and end as in your wallet app?" +
      (wallet.hasStatelessPassphrase() ? " A typo in the passphrase gives another wallet." : "");
  return waitForApproval(confirm.c_str());
}

// derives the wallet with `passphrase`, back to the previous one if the user rejects the address
static void applyPassphrase(const std::string& passphrase) {
  std::string previous = wallet.getStatelessPassphrase();
  displayMessage(DISPLAY_DECRYPTING);
  WalletResponse r = wallet.setStatelessPassphrase(passphrase);
  if (r.status >= Ok && !confirmAddress(passphrase.empty() ? "WITHOUT PASSPHRASE" : "PASSPHRASE WALLET")) {
    wallet.setStatelessPassphrase(previous);
  }
  wipe(previous);
}

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

  // as in Kern a passphrase is added later from the menu
  displayMessage(DISPLAY_DECRYPTING);
  WalletResponse r = wallet.unlockStateless(mnemonic, "");
  mnemonic_clear();
  if (r.status < Ok) {
    waitForDismiss(DISPLAY_KEF_UNSUPPORTED);
    return false;
  }
  unlockedName = w.name;
  if (!confirmAddress("YOUR WALLET")) {
    wallet.lock();
    return false;
  }
  return true;
}

void statelessSetPassphrase(TextScanner scan) {
  std::string passphrase;
  if (!readText("BIP39 passphrase", passphrase, true, scan) || passphrase.empty()) {
    wipe(passphrase);
    return;
  }
  applyPassphrase(passphrase);
  wipe(passphrase);
}

void statelessRemovePassphrase() {
  std::string none;
  applyPassphrase(none);
}

#endif
