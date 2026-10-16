// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "../../config.h"

// STATELESS_MODE: nothing secret is stored on the device. The seed comes from a Kern wallet
// (KEF envelope in /kern/mnemonics on the SD card), its password is typed on the touch screen,
// and the key lives in RAM until the wallet locks.
#ifdef STATELESS_MODE

void statelessShowLocked();
// blocking, true if the wallet got unlocked; the camera must be off, touch active
bool statelessUnlock();

#endif
