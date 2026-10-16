// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "../../config.h"
#include "../ui/ui.h"

// STATELESS_MODE: nothing secret is stored on the device. The seed comes from a Kern wallet
// (KEF envelope in /kern/mnemonics on the SD card), its password is typed on the touch screen,
// and the key lives in RAM until the wallet locks.
#ifdef STATELESS_MODE

void statelessShowLocked();
// blocking, true if the wallet got unlocked; the camera must be off, touch active.
// With `scan` passwords and the passphrase can also come from a QR code.
bool statelessUnlock(TextScanner scan = nullptr);
// adds or changes the BIP39 passphrase of the unlocked wallet, or removes it, after the
// user confirmed the new address; the wallet stays as it was otherwise
void statelessSetPassphrase(TextScanner scan = nullptr);
void statelessRemovePassphrase();

#endif
