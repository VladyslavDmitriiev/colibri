// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "SdCard.h"

#ifdef SD_CARD_ENABLED

  #include <FS.h>
  #include <SD_MMC.h>

  #include <algorithm>

  #define SD_MAX_FILES 16

static bool isMounted = false;

bool sdMount() {
  if (isMounted) return true;
  SD_MMC.setPins(SD_MMC_GPIO_CLK, SD_MMC_GPIO_CMD, SD_MMC_GPIO_D0);
  isMounted = SD_MMC.begin("/sdcard", true);  // 1-bit mode
  if (!isMounted) log_w("No SD card");
  return isMounted;
}

void sdUnmount() {
  if (!isMounted) return;
  SD_MMC.end();
  isMounted = false;
}

bool sdListFiles(const char* dir, const char* extension, std::vector<std::string>& names) {
  names.clear();
  File root = SD_MMC.open(dir);
  if (!root || !root.isDirectory()) return false;
  size_t extLen = strlen(extension);
  for (File f = root.openNextFile(); f && names.size() < SD_MAX_FILES; f = root.openNextFile()) {
    std::string name = f.name();
    bool matches = !f.isDirectory() && name.size() > extLen && name[0] != '.' &&
        name.compare(name.size() - extLen, extLen, extension) == 0;
    f.close();
    if (matches) names.push_back(name);
  }
  root.close();
  std::sort(names.begin(), names.end());
  return true;
}

bool sdReadFile(const std::string& path, std::string& content, size_t maxSize) {
  content.clear();
  File f = SD_MMC.open(path.c_str());
  if (!f || f.isDirectory() || f.size() > maxSize) return false;
  content.resize(f.size());
  size_t read = f.read((uint8_t*)content.data(), content.size());
  f.close();
  return read == content.size();
}

  #ifdef DEBUG_REMOTE_CONTROL
void sdDebugCommand(const std::string& cmd) {
  const char* dir = "/kern/mnemonics";
  bool wasMounted = isMounted;
  if (!sdMount()) {
    Serial.println("SD_ERR no card");
    return;
  }
  if (cmd == "!sdls") {
    std::vector<std::string> names;
    sdListFiles(dir, "", names);
    for (const std::string& n : names) Serial.printf("SD_FILE %s\n", n.c_str());
    Serial.println("SD_OK");
  } else if (cmd.rfind("!sdput ", 0) == 0) {
    size_t space = cmd.find(' ', 7);
    std::string name = cmd.substr(7, space - 7);
    bool ok = space != std::string::npos && name.find('/') == std::string::npos;
    if (ok) {
      SD_MMC.mkdir("/kern");
      SD_MMC.mkdir(dir);
      File f = SD_MMC.open((std::string(dir) + "/" + name).c_str(), FILE_WRITE);
      std::string data = cmd.substr(space + 1);
      ok = f && f.write((const uint8_t*)data.data(), data.size()) == data.size();
      f.close();
    }
    Serial.println(ok ? "SD_OK" : "SD_ERR write");
  } else if (cmd.rfind("!sdrm ", 0) == 0) {
    std::string name = cmd.substr(6);
    bool ok = name.find('/') == std::string::npos && SD_MMC.remove((std::string(dir) + "/" + name).c_str());
    Serial.println(ok ? "SD_OK" : "SD_ERR remove");
  }
  if (!wasMounted) sdUnmount();
}
  #endif

#endif
