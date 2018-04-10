#ifndef SOUND_H_
#define SOUND_H_

#include "freertos/FreeRTOS.h"
#include "driver/i2s.h"

void setup_triangle_sine_waves(i2s_port_t port, i2s_bits_per_sample_t bits, int sampleRate);
void tsk_triangle_sine_waves(void *pvParameters);

#endif
