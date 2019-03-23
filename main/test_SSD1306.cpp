#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <u8g2.h>

#include "sdkconfig.h"
#include "u8g2_esp32_hal.h"

// CLK - GPIO14
#define PIN_CLK GPIO_NUM_14

// MOSI - GPIO 13
#define PIN_MOSI GPIO_NUM_13

// RESET - GPIO 17
#define PIN_RESET GPIO_NUM_17

// DC - GPIO 16
#define PIN_DC GPIO_NUM_16

// CS - GPIO 15
#define PIN_CS GPIO_NUM_15

static char tag[] = "test_SSD1306";

void task_test_SSD1306(void *ignore) {
	u8g2_esp32_hal_t u8g2_esp32_hal = U8G2_ESP32_HAL_DEFAULT;
	u8g2_esp32_hal.clk   = PIN_CLK;
	u8g2_esp32_hal.mosi  = PIN_MOSI;
	u8g2_esp32_hal.cs    = PIN_CS;
	u8g2_esp32_hal.dc    = PIN_DC;
	u8g2_esp32_hal.reset = PIN_RESET;
	u8g2_esp32_hal_init(u8g2_esp32_hal);


	u8g2_t u8g2; // a structure which will contain all the data for one display
	u8g2_Setup_ssd1306_128x64_noname_f(
		&u8g2,
		U8G2_R0,
		u8g2_esp32_spi_byte_cb,
		u8g2_esp32_gpio_and_delay_cb);  // init u8g2 structure

	u8g2_InitDisplay(&u8g2); // send init sequence to the display, display is in sleep mode after this,

	u8g2_SetPowerSave(&u8g2, 0); // wake up display

	u8g2_SetContrast(&u8g2, 255);

	const TickType_t xDelay = 5 / portTICK_PERIOD_MS;

	setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
	tzset();

	int pos = 0;
	while(true) {
		u8g2_ClearBuffer(&u8g2);

		// 128 wide, 64 high. 16 pixels yellow, 48 blue

		u8g2_SetFont(&u8g2, u8g2_font_9x15B_tr);
		u8g2_DrawStr(&u8g2, 0, u8g2_GetAscent(&u8g2), "Hello dragon!");

		u8g2_DrawBox(&u8g2, pos, 20, 15, 5);

		pos++;
		if(pos + 15 > 128) {
			pos = 0;
		}

		time_t now;
		struct tm timeinfo;
	    time(&now);
		localtime_r(&now, &timeinfo);
		if(timeinfo.tm_year >= (2016 - 1900)) {
			char strftime_buf[64];

			strftime(strftime_buf, sizeof(strftime_buf), "%a %e %b %Y", &timeinfo);
			u8g2_SetFont(&u8g2, u8g2_font_profont12_tr);
			u8g2_DrawStr(&u8g2, 0, 16 + 10 + u8g2_GetAscent(&u8g2), strftime_buf);


			strftime(strftime_buf, sizeof(strftime_buf), "%H:%M:%S", &timeinfo);
			u8g2_SetFont(&u8g2, u8g2_font_profont12_mn);
			u8g2_DrawStr(&u8g2, 0, 16 + 10 + 12 + u8g2_GetAscent(&u8g2), strftime_buf);
		}

		u8g2_SendBuffer(&u8g2);

		vTaskDelay( xDelay );
	}

	ESP_LOGD(tag, "All done!");

	vTaskDelete(NULL);
}
