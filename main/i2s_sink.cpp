#include "i2s_sink.h"

#include <stdio.h>

#include "sys/param.h"

#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"

#include "driver/i2s.h"

#define I2S_NUM         (I2S_NUM_0)

#define BCK 23
#define DIN 22
#define LCK 21

void i2s_setup(i2s_port_t port, int bck, int lck, int din, int buf_cnt, int buf_len, i2s_bits_per_sample_t bits, int sampleRate) {
    i2s_config_t i2s_config {};
    i2s_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    i2s_config.sample_rate = sampleRate;
    i2s_config.bits_per_sample = bits;
    i2s_config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    i2s_config.communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB);
    i2s_config.dma_buf_count = buf_cnt;
    i2s_config.dma_buf_len = buf_len;
    i2s_config.use_apll = false,
    i2s_config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;

    i2s_pin_config_t pin_config {};
    pin_config.bck_io_num = bck;
    pin_config.ws_io_num = lck;
    pin_config.data_out_num = din;
    pin_config.data_in_num = -1;

    i2s_driver_install(port, &i2s_config, 0, NULL);
    i2s_set_pin(port, &pin_config);
}


void i2sSetup() {
        //for 36Khz sample rates, we create 100Hz sine wave, every cycle need 36000/100 = 360 samples (4-bytes or 8-bytes each sample)
    //depend on bits_per_sample
    //using 6 buffers, we need 60-samples per buffer
    //if 2-channels, 16-bit each channel, total buffer is 360*4 = 1440 bytes

    // i2s_setup(I2S_NUM, BCK, LCK, DIN, 8, 64); // old pcm/mp3?
    // i2s_setup(I2S_NUM, BCK, LCK, DIN, 6, 60, I2S_BITS_PER_SAMPLE_16BIT, 44100); //sine

    i2s_setup(I2S_NUM, BCK, LCK, DIN, I2S_BUF_COUNT, I2S_BUF_SIZE, I2S_BITS_PER_SAMPLE_16BIT, 44100);
}


/**
 * Render a left and right 16 bit sample.
 * This method blocks till the sample is sent.
 */
void renderSample16(int16_t left, int16_t right) {
    uint32_t sample_val = 0;
    sample_val |= left;
    sample_val <<= 16;
    sample_val |= right;
    size_t written;
    i2s_write(I2S_NUM, (const char *)(&sample_val), sizeof(sample_val), &written, portMAX_DELAY);    
}


/**
 * Render several left and right 16 bit samples.
 * Input endian-ness is reversed.
 * This method blocks till the samples are sent.
 */
void renderSamples16(int16_t *samplesLeft, int16_t *samplesRight, size_t num_samples) {
    // Based on synth.c, at most 32 samples at a time.
    static char sampleBuf[32 * 2 * 2] = {};

    for(int pos = 0 ; pos < MIN(32, num_samples); pos++) {
        uint16_t ch0Sample = samplesLeft[pos];// / 2;
        uint16_t ch1Sample = samplesRight[pos];// / 2;

        sampleBuf[pos * 4 ] = ch0Sample & 0xff;
        sampleBuf[pos * 4 + 1] = (ch0Sample >> 8) & 0xff;

        sampleBuf[pos * 4 + 2] = ch1Sample  & 0xff;
        sampleBuf[pos * 4 + 3] = (ch1Sample >> 8) & 0xff;
    }

    // Using portMAX_DELAY means this blocks till all bytes are written
    size_t written;
    i2s_write(I2S_NUM_0, (const char *)sampleBuf, num_samples * 4, &written, portMAX_DELAY);
}


/**
 * Render several left and right 16 bit samples.
 * Input endian-ness is reversed.
 * This method blocks till the samples are sent.
 */
void renderSamples32(uint8_t *samples32, size_t num_samples) {
    static uint8_t sampleBuf[256] = {};

    for (size_t pos = 0; pos < num_samples; pos++) {
        size_t samplePos = pos * 4;
        sampleBuf[samplePos + 0] = samples32[samplePos+ 1];
        sampleBuf[samplePos + 1] = samples32[samplePos + 0];
        sampleBuf[samplePos + 2] = samples32[samplePos + 3];
        sampleBuf[samplePos + 3] = samples32[samplePos+ 2];
    }

    // Using portMAX_DELAY means this blocks till all bytes are written
    size_t written;
    i2s_write(I2S_NUM_0, (const char *)sampleBuf, num_samples * 4, &written, portMAX_DELAY);
}

/**
 * Fill the entire I2S buffer with silence, but only after playing what's left.
 */
void silenceBuffers() {
    int8_t empty[I2S_BUF_SIZE] = {};
    for (int pos = 0; pos < I2S_BUF_COUNT * 4; pos++) {
        // Using portMAX_DELAY means this blocks till all bytes are written
        size_t written;
        i2s_write(I2S_NUM_0, (const char *)empty, I2S_BUF_SIZE, &written, portMAX_DELAY);
    }
}
