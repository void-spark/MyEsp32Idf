#include "math.h"
#include "sound.h"
#include "tunes.h"

static i2s_port_t _port;
static i2s_bits_per_sample_t _bits;
static int _sampleRate;
static double _vol;
static double _top;

void setup_triangle_sine_waves(i2s_port_t port, i2s_bits_per_sample_t bits, int sampleRate) {
    _port = port;
    _sampleRate = sampleRate;
    _bits = bits;
    _vol = 0.4;
    _top = (pow(2, _bits)/2 - 1) * _vol;
}

static unsigned int calc() {


    const double noteSeconds = 2.000;//1.000;
    const double attack = 0.010;
    const double decay = 0.010;
    const int attackSamples = (int)(attack * _sampleRate);
    const int decaySamples = (int)(decay * _sampleRate);

    static int notePos = 0;

    static int noteSamples = 0;
    static int pauseSamples = 0;

    static int waveFreqHz = 0;
    static double samplePerCycle = 0;

    static unsigned int noteSample = 0;


    if(noteSample == 0 || noteSample >=  noteSamples + pauseSamples ) {
        noteSample = 0;
        waveFreqHz = marchMelody[notePos];
        noteSamples = (int)( ( (noteSeconds / marchDurations[notePos]) * _sampleRate) );
        pauseSamples = (int)( ( ((noteSeconds * 0.10 /*0.30*/) / marchDurations[notePos]) * _sampleRate) );
        notePos++;
        if(notePos == marchSize) {
            notePos = 0;
            pauseSamples += 2.0 * _sampleRate;
        }

        if( waveFreqHz != 0) {
            samplePerCycle = (_sampleRate/waveFreqHz);
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

void tsk_triangle_sine_waves(void *pvParameters) {

    while(true) {
        unsigned int sample_val = calc();
        i2s_push_sample(_port, (const char *)(&sample_val), portMAX_DELAY);
    }
}
