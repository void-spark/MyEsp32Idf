#include "math.h"
#include "sound.h"
#include "tunes.h"
#include "i2s_sink.h"

static i2s_bits_per_sample_t _bits;
static int _sampleRate;
static double _vol;
static double _top;

void setup_triangle_sine_waves(i2s_bits_per_sample_t bits, int sampleRate) {
    _sampleRate = sampleRate;
    _bits = bits;
    _vol = 0.4;
    _top = (pow(2, _bits)/2 - 1) * _vol;
}

static void calc() {

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
        renderSample16((uint16_t) 0, (uint16_t) 0);
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
    //double sin_float = sin((noteSample * pi2) / samplePerCycle) * _top * envelope;
    
    double sin_float = remainder((noteSample * 1.0f) / samplePerCycle, 1.0) * _top * envelope;

    renderSample16((uint16_t) sin_float, (uint16_t) sin_float);

    noteSample++;
}

void tsk_triangle_sine_waves(void *pvParameters) {

    while(true) {
        calc();
    }
}
