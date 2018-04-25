#ifndef I2S_SINK_H_
#define I2S_SINK_H_

#include <stdint.h>
#include <stddef.h>

#define DATA_BLOCK_SIZE 256

#define I2S_BUF_COUNT 8
#define I2S_BUF_SIZE 256

struct dataBlock {
    uint8_t reset;
    size_t used;
    uint8_t data[DATA_BLOCK_SIZE];
};

void i2sSetup();
void renderSample16(int16_t left, int16_t right);
void renderSamples16(int16_t *samplesLeft, int16_t *samplesRight, size_t num_samples);
void renderSamples32(uint8_t *samples32, size_t num_samples);
void silenceBuffers();
 
#endif
