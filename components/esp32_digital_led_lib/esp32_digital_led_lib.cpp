/* 
 * Library for driving digital RGB(W) LEDs using the ESP32's RMT peripheral
 *
 * Modifications Copyright (c) 2017 Martin F. Falatic
 *
 * Based on public domain code created 19 Nov 2016 by Chris Osborn <fozztexx@fozztexx.com>
 * http://insentricity.com
 *
 */
/* 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "esp32_digital_led_lib.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(ARDUINO)
  #include "esp32-hal.h"
  #include "esp_intr.h"
  #include "driver/gpio.h"
  #include "driver/rmt.h"
  #include "driver/periph_ctrl.h"
  #include "freertos/semphr.h"
  #include "soc/rmt_struct.h"
#elif defined(ESP_PLATFORM)
  #include <esp_intr.h>
  #include <driver/gpio.h>
  #include <driver/rmt.h>
  #include <freertos/FreeRTOS.h>
  #include <freertos/semphr.h>
  #include <stdio.h>
  #include <string.h>  // memset, memcpy, etc. live here!
#endif

#ifdef __cplusplus
}
#endif

#include <algorithm>

static DRAM_ATTR const uint16_t MAX_PULSES = 32;  // A channel has a 64 "pulse" buffer - we use half per pass
static DRAM_ATTR const uint16_t DIVIDER    =  4;  // 8 still seems to work, but timings become marginal
// Minimum time of a single RMT duration based on clock ns (1 second / 80mhz)
static DRAM_ATTR const double   RMT_DURATION_NS = 12.5;

typedef struct {
  uint8_t * buf_data;
  uint16_t buf_pos, buf_len, buf_half, buf_isDirty;
  xSemaphoreHandle sem;
  rmt_item32_t pulsePairMap[2];
} digitalLeds_stateData;

static strand_t * localStrands;
static int localStrandCnt = 0;

static intr_handle_t rmt_intr_handle = nullptr;

// Forward declarations of local functions
static void copyToRmtBlock_half(strand_t * pStrand);
static void handleInterrupt(void *arg);

int digitalLeds_initStrands(strand_t strands [], int numStrands) {
  localStrands = strands;
  localStrandCnt = numStrands;
  if (localStrandCnt < 1 || localStrandCnt > 8) {
    return -1;
  }

  for (int index = 0; index < localStrandCnt; index++) {
    strand_t * pStrand = &localStrands[index];
    ledParams_t ledParams = ledParamsAll[pStrand->ledType];

    pStrand->pixels = static_cast<pixelColor_t*>(malloc(pStrand->numPixels * sizeof(pixelColor_t)));
    if (pStrand->pixels == nullptr) {
      return -1;
    }

    pStrand->_stateVars = static_cast<digitalLeds_stateData*>(malloc(sizeof(digitalLeds_stateData)));
    if (pStrand->_stateVars == nullptr) {
      return -1;
    }
    digitalLeds_stateData * pState = static_cast<digitalLeds_stateData*>(pStrand->_stateVars);

    pState->buf_len = (pStrand->numPixels * ledParams.bytesPerPixel);
    pState->buf_data = static_cast<uint8_t*>(malloc(pState->buf_len));
    if (pState->buf_data == nullptr) {
      return -1;
    }
    pState->sem = nullptr;

    rmt_channel_t chan = static_cast<rmt_channel_t>(pStrand->rmtChannel);

    rmt_config_t rmt_tx;
    // We'll be sending, TX mode
    rmt_tx.rmt_mode = RMT_MODE_TX;
    // The channel to use
    rmt_tx.channel = chan;
    // The GPIO pin to use
    rmt_tx.gpio_num = static_cast<gpio_num_t>(pStrand->gpioNum);
    // RMT channel counter divider, based on the 80Mhz APB CLK.
    rmt_tx.clk_div = DIVIDER;
    // Memory blocks to use, keep at 1 or we steal from the next channel.
    // Size is 64 * 32 bits per block.
    rmt_tx.mem_block_num = 1;
    // No carrier signal for us
    rmt_tx.tx_config.carrier_en = false;
    rmt_tx.tx_config.carrier_level = RMT_CARRIER_LEVEL_HIGH;
    rmt_tx.tx_config.loop_en = false;
    rmt_tx.tx_config.idle_output_en = true;
    rmt_tx.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;

    // Apply configuration
    ESP_ERROR_CHECK(rmt_config(&rmt_tx));

    // Disable low power mode
    ESP_ERROR_CHECK(rmt_set_mem_pd(chan, false));
  
    // RMT config for transmitting a '0' bit val to this LED strand
    pState->pulsePairMap[0].level0 = 1;
    pState->pulsePairMap[0].level1 = 0;
    pState->pulsePairMap[0].duration0 = ledParams.T0H / (RMT_DURATION_NS * DIVIDER);
    pState->pulsePairMap[0].duration1 = ledParams.T0L / (RMT_DURATION_NS * DIVIDER);
    
    // RMT config for transmitting a '1' bit val to this LED strand
    pState->pulsePairMap[1].level0 = 1;
    pState->pulsePairMap[1].level1 = 0;
    pState->pulsePairMap[1].duration0 = ledParams.T1H / (RMT_DURATION_NS * DIVIDER);
    pState->pulsePairMap[1].duration1 = ledParams.T1L / (RMT_DURATION_NS * DIVIDER);

    // Enable interrupts for having sent MAX_PULSES pulses
    ESP_ERROR_CHECK(rmt_set_tx_thr_intr_en(chan, true, MAX_PULSES));
    // Enable interrupts for finishing sending
    ESP_ERROR_CHECK(rmt_set_tx_intr_en(chan, true));
  }
  
  ESP_ERROR_CHECK(rmt_isr_register(handleInterrupt, nullptr, 0, &rmt_intr_handle));

  for (int i = 0; i < localStrandCnt; i++) {
    strand_t * pStrand = &localStrands[i];
    digitalLeds_resetPixels(pStrand);
  }

  return 0;
}

void digitalLeds_resetPixels(strand_t * pStrand) {
  memset(pStrand->pixels, 0, pStrand->numPixels * sizeof(pixelColor_t));
  digitalLeds_updatePixels(pStrand);
}

int IRAM_ATTR digitalLeds_updatePixels(strand_t * pStrand) {
  digitalLeds_stateData * pState = static_cast<digitalLeds_stateData*>(pStrand->_stateVars);

  if (pState->sem) {
    // Wait for any previously updating pixels.
    xSemaphoreTake(pState->sem, portMAX_DELAY);
    vSemaphoreDelete(pState->sem);
    pState->sem = nullptr;
  }

  ledParams_t ledParams = ledParamsAll[pStrand->ledType];

  // Pack pixels into transmission buffer
  if (ledParams.bytesPerPixel == 3) {
    for (uint16_t i = 0; i < pStrand->numPixels; i++) {
      // Color order is translated from RGB to GRB
      pState->buf_data[0 + i * 3] = pStrand->pixels[i].g;
      pState->buf_data[1 + i * 3] = pStrand->pixels[i].r;
      pState->buf_data[2 + i * 3] = pStrand->pixels[i].b;
    }
  } else if (ledParams.bytesPerPixel == 4) {
    for (uint16_t i = 0; i < pStrand->numPixels; i++) {
      // Color order is translated from RGBW to GRBW
      pState->buf_data[0 + i * 4] = pStrand->pixels[i].g;
      pState->buf_data[1 + i * 4] = pStrand->pixels[i].r;
      pState->buf_data[2 + i * 4] = pStrand->pixels[i].b;
      pState->buf_data[3 + i * 4] = pStrand->pixels[i].w;
    }    
  } else {
    return -1;
  }

  pState->buf_pos = 0;
  pState->buf_half = 0;

  copyToRmtBlock_half(pStrand);

  if (pState->buf_pos < pState->buf_len) {
    // Fill the other half of the buffer block
    copyToRmtBlock_half(pStrand);
  }

  pState->sem = xSemaphoreCreateBinary();

  ESP_ERROR_CHECK(rmt_tx_start(static_cast<rmt_channel_t>(pStrand->rmtChannel), true));

  return 0;
}

/**
 * This fills half an RMT block (32 pulses, which is 4 bytes worth, 1 bit per pulse)
 * When wraparound is happening, we want to keep the inactive half of the RMT block filled
 */
static IRAM_ATTR void copyToRmtBlock_half(strand_t * pStrand) {

  digitalLeds_stateData * pState = static_cast<digitalLeds_stateData*>(pStrand->_stateVars);
  const ledParams_t ledParams = ledParamsAll[pStrand->ledType];

  // Offset toggles between 0 and MAX_PULSES, which is beginning or half of the RMT memory block
  const uint16_t offset = pState->buf_half * MAX_PULSES;
  pState->buf_half = !pState->buf_half;

  // Length of the buffer content, or at most the maximum bytes that fit in half the RMT memory block (4).
  const uint16_t len = std::min(pState->buf_len - pState->buf_pos, MAX_PULSES / 8);

  if (!len) {
    // No data left/available.
    if (!pState->buf_isDirty) {
      // RMT memory block is already cleared
      return;
    }

    // Clear the channel's RMT memory block and return
    for (uint16_t index = 0; index < MAX_PULSES; index++) {
      // TODO: Should we clean the whole block? this is half. Should be enough since val = 0 stops the RMT
      // And I guess we write to the next half when we start again.
      RMTMEM.chan[pStrand->rmtChannel].data32[offset + index].val = 0;
    }

    // RMT memory block is cleared
    pState->buf_isDirty = 0;
    return;
  }

  // RMT memory block contains dirt
  pState->buf_isDirty = 1;

  // Step through the bytes in the buffer.
  for (uint16_t index = 0; index < len; index++) {
    // Get (a copy of) the current byte
    const uint16_t byteval = pState->buf_data[pState->buf_pos + index];

    // Shift bits out, MSB first, setting RMTMEM.chan[n].data32[x] to
    // the rmtPulsePair value corresponding to the buffered bit value
    for (uint16_t bit = 0; bit < 8; bit++) {
      // Get the value for the current bit, with bit 0 is MSB, bit 7 is LSB.
      const uint16_t bitval = (byteval & (1 << (7-bit))) != 0;
      // Get the index into the RMT memory block, which is the offset to the correct half + the byte index * 8 + the bit index.
      const uint16_t data32_idx = offset + index * 8 + bit;
      // Set the pulse values from the lookup table/map for 0 and 1 bit
      RMTMEM.chan[pStrand->rmtChannel].data32[data32_idx].val = pState->pulsePairMap[bitval].val;
    }

    // Handle the reset bit by stretching duration1 for the final bit in the stream
    if (pState->buf_pos + index == pState->buf_len - 1) {
      RMTMEM.chan[pStrand->rmtChannel].data32[offset + index * 8 + 7].duration1 =
        ledParams.TRS / (RMT_DURATION_NS * DIVIDER);
    }
  }

  // Clear the remainder of the RMT memory block (half) not set above
  // Setting val = 0 (duration0/1 = 0 especially) should stop the RMT tx at this pulse
  for (uint16_t data32_idx = len * 8; data32_idx < MAX_PULSES; data32_idx++) {
    RMTMEM.chan[pStrand->rmtChannel].data32[offset + data32_idx].val = 0;
  }

  pState->buf_pos += len;

  return;
}

static IRAM_ATTR void handleInterrupt(void *arg) {

  uint32_t intr_st = RMT.int_st.val;

  // Check each strand
  for (uint16_t index = 0; index < localStrandCnt; index++) {
    // Get the strand state data
    strand_t * pStrand = &localStrands[index];
    digitalLeds_stateData * pState = static_cast<digitalLeds_stateData*>(pStrand->_stateVars);

    if (intr_st & BIT(24 + pStrand->rmtChannel)) {  
      RMT.int_clr.val = BIT(24 + pStrand->rmtChannel); // Clear interrupt bit

      // Threshold interrupt for the channel is triggered, we sent half a memory block of pulses
      // Fill the used half, hopefully before the other half is sent.
      copyToRmtBlock_half(pStrand);
    } else if (intr_st & BIT((0 + pStrand->rmtChannel) * 3)) {  
      RMT.int_clr.val = BIT((0 + pStrand->rmtChannel) * 3); // Clear interrupt bit

      // We're done sending, notify the next (current or future) call to digitalLeds_updatePixels
      if(pState->sem) {
        portBASE_TYPE xHigherPriorityTaskWoken = pdFALSE;
        xSemaphoreGiveFromISR(pState->sem, &xHigherPriorityTaskWoken);
        if (xHigherPriorityTaskWoken == pdTRUE) {
            portYIELD_FROM_ISR();
        }
      }
    }
  }

  return;
}
