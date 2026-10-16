// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "../../config.h"

#include <string>
#include <vector>

// SD card on the SDMMC bus, mounted only while it's read
#if defined(SD_MMC_GPIO_CLK)
  #define SD_CARD_ENABLED

bool sdMount();
void sdUnmount();
// names of the files in `dir` ending in `extension`, sorted
bool sdListFiles(const char* dir, const char* extension, std::vector<std::string>& names);
bool sdReadFile(const std::string& path, std::string& content, size_t maxSize);
  #ifdef DEBUG_REMOTE_CONTROL
// !sdls, !sdput NAME BASE64, !sdrm NAME on files in /kern/mnemonics, to set up tests
void sdDebugCommand(const std::string& cmd);
  #endif
#endif
