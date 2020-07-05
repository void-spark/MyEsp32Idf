#ifndef RcReceiver_h
#define RcReceiver_h

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// more ext
enum { TRI_0, TRI_1, TRI_F, TRI_INVALID };


// more int
enum { BIT_0, BIT_1, BIT_S, BIT_INVALID };

// The shortest high or low pulse duration we won't filter out.
static const int LOW_PASS_LIMIT = 80;

// Size difference between the two pulses in a sync bit.
// Doesn't need to be accurate, as long as it sorts out 'plain' and sync bits.
static const int SYNC_FACTOR = 10;

struct BitData {
    uint8_t value;
    uint32_t firstPulse;
    uint32_t secondPulse;
};

class RcReceiver {
  public:
    // bits = max bits, including sync
    RcReceiver(uint32_t bits_, uint32_t patterns);

    void printInfo();

    void handleInterrupt(const uint32_t time);
    struct BitData* getReceivedBits();
    uint8_t* getTris(const struct BitData* bitData);
    void printAcTimings(const struct BitData* bitData);
    void printAcTriState(const uint8_t pulses[]);
    void printDetails(const struct BitData* bitData);

    ~RcReceiver();
  private:

    uint32_t _bits;
    uint32_t _pulses;
    // uint32_t _patterns;

    // volatile uint32_t head = 0;
    // volatile uint32_t tail = 0;

    QueueHandle_t resultQueue;

    //// handleDuration
    // Last three durations
    uint32_t lastDuration = 0;
    uint32_t secondLastDuration = 0;
    uint32_t thirdLastDuration = 0;
    // Count of pulses/durations we have stored.
    uint32_t bufferSize = 0;
    // Index into the ring buffer(s), at the next position we'll write to.
    uint32_t bufferPos = 0;
    // Buffer with the most recent durations
    uint32_t *timings;
    // Buffer with bits, note that each bit covers the duration at that position,
    // and the duration at the previous position, since a bit is encoded in two durations.
    uint8_t *bits;
    // Buffer with pattern matching data
    bool *matching;

    //handleInterrupt
    uint32_t lastInterruptTime = 0;
    uint32_t lastInterruptDuration = 0;
    bool addNext = false;

    //// The buffers we return to callers..
    uint8_t *trisHolder;

    struct BitData* bitDataHolder;


    uint8_t getBitType(const uint32_t lastDuration, const uint32_t duration);
    void handleDuration(const uint32_t duration);
    void printTimings(const struct BitData* bitData, bool bits, bool durations);

};

#endif
