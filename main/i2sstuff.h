#ifndef I2SSTUFF_H_
#define I2SSTUFF_H_

#include "freertos/FreeRTOS.h"
#include "driver/i2s.h"

void i2s_setup(i2s_port_t port, int bck, int lck, int din, int buf_cnt, int buf_len, i2s_bits_per_sample_t bits, int sampleRate);

#endif
