#include "esp32_digital_led_lib.h"

#include <esp_intr.h>
#include <driver/gpio.h>
#include <driver/rmt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdio.h>
#include <string.h>

static DRAM_ATTR const uint16_t DIVIDER  =  4;  // 8 still seems to work, but timings become marginal

// Minimum time of a single RMT duration based on clock ns (1 second / 80mhz)
static DRAM_ATTR const double RMT_DURATION_NS = 12.5;

typedef struct {
  uint16_t buf_len;
  rmt_item32_t* buf_data;
  rmt_item32_t pulsePairMap[2];
} digitalLeds_stateData;

static strand_t * localStrands;
static int localStrandCnt = 0;

void fillRmt(uint8_t byteVal, uint16_t byteOffset, rmt_item32_t* dest, rmt_item32_t pulsePairMap[2]);


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

    pState->buf_len = (pStrand->numPixels * ledParams.bytesPerPixel * 8);
    pState->buf_data = static_cast<rmt_item32_t*>(malloc(pState->buf_len * sizeof(rmt_item32_t)));
    if (pState->buf_data == nullptr) {
      return -1;
    }

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

    ESP_ERROR_CHECK(rmt_driver_install(chan, 0, 0));
  }
  
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

  // Wait for any previously updating pixels.
  ESP_ERROR_CHECK(rmt_wait_tx_done(static_cast<rmt_channel_t>(pStrand->rmtChannel), portMAX_DELAY));

  ledParams_t ledParams = ledParamsAll[pStrand->ledType];

  // Pack pixels into transmission buffer
  if (ledParams.bytesPerPixel == 3) {
    for (uint16_t pixelIndex = 0; pixelIndex < pStrand->numPixels; pixelIndex++) {
      const uint16_t byteOffset = pixelIndex * 3;

      // Color order is translated from RGB to GRB
      fillRmt(pStrand->pixels[pixelIndex].g, byteOffset + 0, pState->buf_data, pState->pulsePairMap);
      fillRmt(pStrand->pixels[pixelIndex].r, byteOffset + 1, pState->buf_data, pState->pulsePairMap);
      fillRmt(pStrand->pixels[pixelIndex].b, byteOffset + 2, pState->buf_data, pState->pulsePairMap);
    }
  } else if (ledParams.bytesPerPixel == 4) {
    for (uint16_t pixelIndex = 0; pixelIndex < pStrand->numPixels; pixelIndex++) {
      const uint16_t byteOffset = pixelIndex * 3;

      // Color order is translated from RGBW to GRBW
      fillRmt(pStrand->pixels[pixelIndex].g, byteOffset + 0, pState->buf_data, pState->pulsePairMap);
      fillRmt(pStrand->pixels[pixelIndex].r, byteOffset + 1, pState->buf_data, pState->pulsePairMap);
      fillRmt(pStrand->pixels[pixelIndex].b, byteOffset + 2, pState->buf_data, pState->pulsePairMap);
      fillRmt(pStrand->pixels[pixelIndex].w, byteOffset + 3, pState->buf_data, pState->pulsePairMap);
    }    
  } else {
    return -1;
  }

  // Handle the reset bit by stretching duration1 for the final bit in the stream
  pState->buf_data[pState->buf_len - 1].duration1 = ledParams.TRS / (RMT_DURATION_NS * DIVIDER);

  // Start transmitting
  ESP_ERROR_CHECK(rmt_write_items(static_cast<rmt_channel_t>(pStrand->rmtChannel), pState->buf_data, pState->buf_len, false));

  return 0;
}

void IRAM_ATTR fillRmt(uint8_t byteVal, uint16_t byteOffset, rmt_item32_t* dest, rmt_item32_t pulsePairMap[2]) {
  // Shift bits out, MSB first, setting RMTMEM.chan[n].data32[x] to
  // the rmtPulsePair value corresponding to the buffered bit value
  for (uint16_t bit = 0; bit < 8; bit++) {
    // Get the value for the current bit, with bit 0 is MSB, bit 7 is LSB.
    const uint16_t bitval = (byteVal & (1 << (7-bit))) != 0;
    // Set the pulse values from the lookup table/map for 0 and 1 bit
    dest[(byteOffset * 8) + bit].val = pulsePairMap[bitval].val;
  }  
}