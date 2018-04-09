#include "math.h"
#include "sound.h"
#include "tunes.h"
#include "freertos/FreeRTOS.h"
#include "driver/i2s.h"

#define SAMPLE_RATE     (44100) // 44100
#define I2S_NUM         (I2S_NUM_0)
#define BITS            (I2S_BITS_PER_SAMPLE_16BIT)

#define WAVE_FREQ_HZ    (262) // 262
#define SAMPLE_PER_CYCLE (SAMPLE_RATE/WAVE_FREQ_HZ)

void i2s_setup(int bck, int lck, int din) {
    //for 36Khz sample rates, we create 100Hz sine wave, every cycle need 36000/100 = 360 samples (4-bytes or 8-bytes each sample)
    //depend on bits_per_sample
    //using 6 buffers, we need 60-samples per buffer
    //if 2-channels, 16-bit each channel, total buffer is 360*4 = 1440 bytes
    i2s_config_t i2s_config {};
    i2s_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    i2s_config.sample_rate = SAMPLE_RATE;
    i2s_config.bits_per_sample = BITS;
    i2s_config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    i2s_config.communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB);
    i2s_config.dma_buf_count = 8;
    i2s_config.dma_buf_len = 256;
    // i2s_config.dma_buf_count = 8;
    // i2s_config.dma_buf_len = 64;
    // i2s_config.dma_buf_count = 6;
    // i2s_config.dma_buf_len = 60;
    i2s_config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;

    i2s_pin_config_t pin_config {};
    pin_config.bck_io_num = bck;
    pin_config.ws_io_num = lck;
    pin_config.data_out_num = din;
    pin_config.data_in_num = -1;

    i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM, &pin_config);

    i2s_set_sample_rates(I2S_NUM, SAMPLE_RATE);
}



const double vol = 0.4;
const double _top = (pow(2, BITS)/2 - 1) * vol;


static unsigned int calc() {


    const double noteSeconds = 2.000;//1.000;
    const double attack = 0.010;
    const double decay = 0.010;
    const int attackSamples = (int)(attack * SAMPLE_RATE);
    const int decaySamples = (int)(decay * SAMPLE_RATE);

    static int notePos = 0;

    static int noteSamples = 0;
    static int pauseSamples = 0;

    static int waveFreqHz = 0;
    static double samplePerCycle = 0;

    static unsigned int noteSample = 0;


    if(noteSample == 0 || noteSample >=  noteSamples + pauseSamples ) {
        noteSample = 0;
        waveFreqHz = marchMelody[notePos];
        noteSamples = (int)( ( (noteSeconds / marchDurations[notePos]) * SAMPLE_RATE) );
        pauseSamples = (int)( ( ((noteSeconds * 0.10 /*0.30*/) / marchDurations[notePos]) * SAMPLE_RATE) );
        notePos++;
        if(notePos == marchSize) {
            notePos = 0;
            pauseSamples += 2.0 * SAMPLE_RATE;
        }

        if( waveFreqHz != 0) {
            samplePerCycle = (SAMPLE_RATE/waveFreqHz);
        }

    //    printf("%d, %d, %d\n" , noteSamples, pauseSamples, decaySamples);
    }

    if(waveFreqHz == 0) {
        noteSample++;
        return 0;
    }


    double envelope = 1.0;
    if(noteSample < attackSamples ) {
        envelope = ((float)noteSample / (float)attackSamples);
    }else  if(noteSample >= noteSamples ) {
        int decaySample = noteSample - noteSamples;
        if(decaySample < decaySamples) {
            envelope = 1.0 - ((float)decaySample / (float)decaySamples);
        } else {
            envelope = 0.0;
        }
    } else {
       envelope = 1.0;
   }

    double pi2 = M_TWOPI;
    double sin_float = sin((noteSample * pi2) / samplePerCycle) * _top * envelope;

    unsigned int sample_val = 0;
    sample_val += (short) sin_float;
    sample_val = sample_val << 16;
    sample_val += (short) sin_float;

    noteSample++;
    return sample_val;
}

void step_triangle_sine_waves() {

    // Keep the value in a static, so if it's not sent, we'll reuse the same value next time.
    // TODO: Prepare buffer sized chunks, and send those out? Should always be buffer size empty? ?? ???
    static unsigned int sample_val = calc();
    while( i2s_push_sample(I2S_NUM, (const char *)(&sample_val), 0) != 0) {
        sample_val = calc();
    }


}
