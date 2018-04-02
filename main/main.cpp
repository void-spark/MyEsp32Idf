/* Hello World Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "driver/gpio.h"
#include "esp32_digital_led_lib.h"

#include "RcReceiver.h"

#define LED1_EXT 27
#define LED2_EXT 32
#define LED3_EXT 25

#define WS2812_1 GPIO_NUM_26
#define NUM_PIXELS 8

#define RCV1_EXT GPIO_NUM_15

#define RC_BITS 25
#define TRIS (RC_BITS - 1) / 2

RcReceiver rcReceiver(RC_BITS, 12);

strand_t strands[] = { {.rmtChannel = 0, .gpioNum = WS2812_1, .ledType = LED_WS2812B_V3, .brightLimit = 32, .numPixels =  NUM_PIXELS,
   .pixels = nullptr, ._stateVars = nullptr} };

strand_t * strand = &strands[0];

void printAc();

IRAM_ATTR void myHandleInterrupt(void *) {
  const unsigned long time = esp_timer_get_time();
  //int state = digitalRead(RCV1_EXT);
  rcReceiver.handleInterrupt(time);
  //digitalWrite(TMP_OUT, state);
}

extern "C" void app_main() {


    gpio_pad_select_gpio(WS2812_1);
    gpio_set_direction(WS2812_1, GPIO_MODE_OUTPUT);
    gpio_set_level(WS2812_1, 0);



    printf("Hello world!\n");

    /* Print chip information */
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    printf("This is ESP32 chip with %d CPU cores, WiFi%s%s, ",
        chip_info.cores,
        (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
        (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

    printf("silicon revision %d, ", chip_info.revision);

    printf("%dMB %s flash\n", spi_flash_get_chip_size() / (1024 * 1024),
        (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");



    if (digitalLeds_initStrands(strands, 1)) {
        printf("Failed to initialize ws2812");
        while (true) {};
    }
    digitalLeds_resetPixels(strand);


    for (int i = 0; i < strand->numPixels; i++) {
        strand->pixels[i] = pixelFromRGB(5, 0, 0);
    }
    digitalLeds_updatePixels(strand);



    gpio_config_t io_conf;
    io_conf.pin_bit_mask = 1ULL << RCV1_EXT;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_ANYEDGE;
    gpio_config(&io_conf);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(RCV1_EXT, myHandleInterrupt, NULL);

    while(true) {
        printAc();
    }
}


//TODO: 'Move' this to ac_recv, tweak timings for more reliability??, print timings..,
//check at least 2/ like doorbell. (+ timeout/last????) Reset last on..??? change MAX/CODE_BIT_PULSES?
// USE ESP TIMERS??
// Check in/after  hasAcReceived, if it's all valid tri's!! And if address bits are correct?!
//   AGoal: no weird ones between two identical, which would prevent compare.
//    Use the longer buffer we have now ????
// boolean isBit/isSyncBit (isTri??) < array with 'pattern' of this?!
// Document: shouting on my 'superhetrodyne' cause high pulses to be longer.
// Could the signal getting to the pin be too low, or otherwise bad?
// short pulses do seem one of the issues

bool isCorrectCode(uint8_t* result) {
  return result[0] == TRI_0 && // A0 = GND
         ( result[1] == TRI_0 || result[1] == TRI_1 ) && // A1 = pin1 (1=UP)
         ( result[2] == TRI_0 || result[2] == TRI_1 ) && // A2 = pin2 (1=UP)
         ( result[3] == TRI_0 || result[3] == TRI_1 ) && // A3 = pin3 (1=UP)
         result[4] == TRI_0 && // A4 = GND
         result[5] == TRI_0 && // A5 = GND
         result[6] == TRI_0 && // A6 = GND
         result[7] == TRI_0 && // A7 = GND
         ( result[8] == TRI_0 || result[8] == TRI_1 ) && // D0 = switch
         result[9] == TRI_0 && // D1 = GND
         result[10] == TRI_0 && // D2 = GND
         result[11] == TRI_0 ;// D3 = GND
}

void printAc() {

    static uint8_t triggered[TRIS] = {};
    static uint8_t last[TRIS] = {};

    struct BitData *bitData = rcReceiver.getReceivedBits();
    uint8_t *result = rcReceiver.getTris(bitData);

    bool correctCode = isCorrectCode(result);

    rcReceiver.printDetails(bitData);
    if (!correctCode) {
        rcReceiver.printAcTimings(bitData);
    }
    rcReceiver.printAcTriState(result);
    if (!correctCode) {
        return;
    }

    bool sameAsTriggered = true;
    bool sameAsLast = true;
    for (int pos = 0; pos < TRIS; pos++) {
        if (result[pos] != last[pos]) {
            sameAsLast = false;
        }
        if (result[pos] != triggered[pos]) {
            sameAsTriggered = false;
        }
        last[pos] = result[pos];
    }
    if (sameAsLast) {
        bool stateOn = result[8] == TRI_1;

        uint8_t address = 0;
        address += result[1] == TRI_1 ? 0 : 1;
        address += result[2] == TRI_1 ? 0 : 2;
        address += result[3] == TRI_1 ? 0 : 4;
    }

    if (sameAsLast && !sameAsTriggered) {
        for (int pos = 0; pos < TRIS; pos++) {
            triggered[pos] = result[pos];
        }

        bool stateOn = result[8] == TRI_1;

        uint8_t address = 0;
        address += result[1] == TRI_1 ? 0 : 1;
        address += result[2] == TRI_1 ? 0 : 2;
        address += result[3] == TRI_1 ? 0 : 4;

        printf("Switch %d: %s\n", address + 1, stateOn ? "ON" : "OFF");

        if(stateOn) {
            strand->pixels[address] = pixelFromRGB(0, 0, 40);
        } else {
            strand->pixels[address] = pixelFromRGB(0, 0, 0);
        }
        digitalLeds_updatePixels(strand);
    }
}
