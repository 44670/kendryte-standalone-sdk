#pragma once

#include "ps2.h"
#include "sdcard.h"
#include "lcd.h"

#define GPIOHS_WIFI_EN (0)
#define GPIOHS_LCD_RST (1)
#define GPIOHS_LCD_DCX (2)
#define GPIOHS_AUDIO_ENABLE (3)
#define GPIOHS_PS2_CS (4)
#define GPIOHS_PS2_CLK (5)
#define GPIOHS_PS2_MOSI (8)
#define GPIOHS_PS2_MISO (9)
#define GPIOHS_SDCARD_CS (7)


static inline void setupHardware() {
	// ps2
    fpioa_set_function(21, FUNC_GPIOHS0 + GPIOHS_PS2_CS);   //ss
    fpioa_set_function(23, FUNC_GPIOHS0 + GPIOHS_PS2_CLK); //clk
    fpioa_set_function(22, FUNC_GPIOHS0 + GPIOHS_PS2_MOSI); //mosi--DO/CMD
    fpioa_set_function(20, FUNC_GPIOHS0 + GPIOHS_PS2_MISO); //miso--DI/DAT

	// i2s
	fpioa_set_function(34, FUNC_I2S0_OUT_D0);
	fpioa_set_function(35, FUNC_I2S0_SCLK);
	fpioa_set_function(33, FUNC_I2S0_WS);
	fpioa_set_function(32, FUNC_GPIOHS0 + GPIOHS_AUDIO_ENABLE);
	gpiohs_set_drive_mode(GPIOHS_AUDIO_ENABLE, GPIO_DM_OUTPUT);
	gpiohs_set_pin(GPIOHS_AUDIO_ENABLE, 1);

	// lcd
	fpioa_set_function(37, FUNC_GPIOHS0 + RST_GPIONUM);
	fpioa_set_function(38, FUNC_GPIOHS0 + DCX_GPIONUM);
	fpioa_set_function(36, FUNC_SPI0_SS3);
	fpioa_set_function(39, FUNC_SPI0_SCLK);
	sysctl_set_spi0_dvp_data(1);

	// sdcard
	fpioa_set_function(27, FUNC_SPI1_SCLK);
	fpioa_set_function(28, FUNC_SPI1_D0);
	fpioa_set_function(26, FUNC_SPI1_D1);
	fpioa_set_function(29, FUNC_GPIOHS0 + SD_CS_PIN);
	fpioa_set_function(25, FUNC_SPI0_SS0 + SD_SS);

	// wifi disabled
	fpioa_set_function(8, FUNC_GPIOHS0 + GPIOHS_WIFI_EN);
	gpiohs_set_drive_mode(GPIOHS_WIFI_EN, GPIO_DM_OUTPUT);
	gpiohs_set_pin(GPIOHS_WIFI_EN, 0);
}