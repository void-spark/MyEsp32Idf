#ifndef I2S_SINK_H_
#define I2S_SINK_H_

#include <stdint.h>
#include <stddef.h>

void i2sSetup();
void renderSample16(int16_t left, int16_t right);
void renderSamples16(int16_t *samplesLeft, int16_t *samplesRight, size_t num_samples);
void renderSamples32(uint8_t *samples32, size_t num_samples);
 
#endif
