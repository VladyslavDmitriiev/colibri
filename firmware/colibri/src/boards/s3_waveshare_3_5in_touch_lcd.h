// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

/*
 * Waveshare ESP32-S3-Touch-LCD-3.5 (with OV5640 camera)
 * https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-3.5
 *
 * Arduino IDE: `Tools > PSRAM > OPI PSRAM`, `Tools > Flash Size > 16MB`
 */

#include <Wire.h>

// No user LED (LED_BUILTIN would be GPIO48 = camera D2), BOOT is the only programmable button
#define LED_DISABLED
#define BUTTON_GPIO_OK 0

// Shared I2C bus: AXP2101 PMIC, TCA9554 IO expander, FT6336 touch, camera SCCB
#define BOARD_GPIO_I2C_SDA 8
#define BOARD_GPIO_I2C_SCL 7
#define BOARD_I2C_ADDR_AXP2101 0x34
#define BOARD_I2C_ADDR_TCA9554 0x20
#define BOARD_TCA9554_LCD_RST_BIT 1
#define BOARD_TCA9554_SD_D3_BIT 3  // SDMMC DAT3: high, or the card stays in SPI mode

// Onboard 3.5" ST7796 display config
#define DISPLAY_ENABLED
#define DISPLAY_ST7796
#define DISPLAY_WIDTH 320
#define DISPLAY_HEIGHT 480
#define DISPLAY_INVERT true

// Display pins (reset via IO expander, see `boardInit()`)
#define DISPLAY_GPIO_SPI_MOSI 1
#define DISPLAY_GPIO_SPI_MISO 2
#define DISPLAY_GPIO_SPI_SCLK 5
#define DISPLAY_GPIO_SPI_DC 3
#define DISPLAY_GPIO_BACKLIGHT 6

// SD card, SDMMC 1-bit
#define SD_MMC_GPIO_CLK 11
#define SD_MMC_GPIO_CMD 10
#define SD_MMC_GPIO_D0 9

// FT6336 touch, shares the I2C bus with the camera's SCCB
#define TOUCH_FT5X06_I2C_ADDR 0x38
#define TOUCH_GPIO_I2C_SDA BOARD_GPIO_I2C_SDA
#define TOUCH_GPIO_I2C_SCL BOARD_GPIO_I2C_SCL

// Onboard OV5640 camera config
#define CAMERA_ENABLED
#define CAMERA_GPIO_PWDN -1
#define CAMERA_GPIO_RESET -1
#define CAMERA_GPIO_XCLK 38
#define CAMERA_GPIO_SIOD BOARD_GPIO_I2C_SDA  // own SCCB driver, `boardInit()` releases the bus
#define CAMERA_GPIO_SIOC BOARD_GPIO_I2C_SCL
#define CAMERA_GPIO_D7 21
#define CAMERA_GPIO_D6 39
#define CAMERA_GPIO_D5 40
#define CAMERA_GPIO_D4 42
#define CAMERA_GPIO_D3 46
#define CAMERA_GPIO_D2 48
#define CAMERA_GPIO_D1 47
#define CAMERA_GPIO_D0 45
#define CAMERA_GPIO_VSYNC 17
#define CAMERA_GPIO_HREF 18
#define CAMERA_GPIO_PCLK 41
#define CAMERA_XCLK_FREQ 10000000
#define CAMERA_FRAME_SIZE FRAMESIZE_QVGA
#define CAMERA_VFLIP 1

static void boardWriteRegister(uint8_t addr, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

static uint8_t boardReadRegister(uint8_t addr, uint8_t reg) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(addr, (uint8_t)1);
  return Wire.available() ? Wire.read() : 0;
}

static void boardInit() {
  Wire.begin(BOARD_GPIO_I2C_SDA, BOARD_GPIO_I2C_SCL);

  // AXP2101: camera power, BLDO1 = 1.5V (DVDD), BLDO2 = 2.8V (AVDD); 0.5V + n * 0.1V
  boardWriteRegister(BOARD_I2C_ADDR_AXP2101, 0x96, 10);
  boardWriteRegister(BOARD_I2C_ADDR_AXP2101, 0x97, 23);
  uint8_t ldoEnable = boardReadRegister(BOARD_I2C_ADDR_AXP2101, 0x90);
  boardWriteRegister(BOARD_I2C_ADDR_AXP2101, 0x90, ldoEnable | (1 << 4) | (1 << 5));

  // TCA9554: SD DAT3 high, pulse LCD reset
  const uint8_t outputs = (1 << BOARD_TCA9554_LCD_RST_BIT) | (1 << BOARD_TCA9554_SD_D3_BIT);
  uint8_t ioConfig = boardReadRegister(BOARD_I2C_ADDR_TCA9554, 0x03);
  boardWriteRegister(BOARD_I2C_ADDR_TCA9554, 0x03, ioConfig & ~outputs);
  uint8_t ioOutput = boardReadRegister(BOARD_I2C_ADDR_TCA9554, 0x01) | (1 << BOARD_TCA9554_SD_D3_BIT);
  boardWriteRegister(BOARD_I2C_ADDR_TCA9554, 0x01, ioOutput & ~(1 << BOARD_TCA9554_LCD_RST_BIT));
  delay(10);
  boardWriteRegister(BOARD_I2C_ADDR_TCA9554, 0x01, ioOutput | (1 << BOARD_TCA9554_LCD_RST_BIT));
  delay(120);

  // hand the bus over to the camera's SCCB driver
  Wire.end();
}
#define BOARD_INIT boardInit
