#include <stdio.h>

#include "doorbell_recv.h"

// 584 bits in a full transmission (15*24 + 14*16).
// ~52054 samples, at 44100 hz = ~1180363 micro seconds
// ~89.13 samples per bit
// ~2021.17 micro seconds per bit.

// Averages:
// HIGH_SHORT:  472 micros
// LOW_SHORT:   554 micros
// HIGH_LONG:  1471 micros
// LOW_LONG:   1548 micros

// Short lower threshold: 333 micro seconds. ~15 frames
// Short upper threshold: 693 micro seconds. ~31 frames
// Long lower threshold: 1343 micro seconds. ~59 frames
// Long upper threshold: 1675 micro seconds. ~74 frames

// Measured average short pulse
static const int shortMicros = 513;
// Measured average long pulse
static const int longMicros = 1509;
// Variance, roughly measured based on a big set.
static const double shortVariance = 0.35;
// Variance, roughly measured based on a big set.
static const double longVariance = 0.11;

static const int shortMin = (int)(0.5 + shortMicros * (1.0 - shortVariance) );
static const int shortMax = (int)(0.5 + shortMicros * (1.0 + shortVariance));
static const int longMin = (int)(0.5 + longMicros * (1.0 - longVariance));
static const int longMax = (int)(0.5 + longMicros * (1.0 + longVariance));

// 111100 000111 111100 110000 - 111101 111101 1100
// 001100 111011 110011 110011 - 111111 111101 1100
// 0 = HIGH_LONG, LOW_SHORT
// 1= HIGH_SHORT, LOW_LONG


static const uint8_t PATTERN1[] = {SHORT, LONG, SHORT, LONG, SHORT, LONG, SHORT, LONG, LONG, SHORT, SHORT, LONG, SHORT, LONG, SHORT, LONG, SHORT, LONG, SHORT, LONG, LONG, SHORT, SHORT, LONG, SHORT, LONG, SHORT, LONG, LONG, SHORT, LONG, SHORT};
static const uint8_t PATTERN2[] = {SHORT, LONG, SHORT, LONG, SHORT, LONG, SHORT, LONG, SHORT, LONG, SHORT, LONG, SHORT, LONG, SHORT, LONG, SHORT, LONG, SHORT, LONG, LONG, SHORT, SHORT, LONG, SHORT, LONG, SHORT, LONG, LONG, SHORT, LONG, SHORT};
static const int PATTERN_LEN = sizeof(PATTERN1) / sizeof(*PATTERN1);
 QueueHandle_t drbResultQueue;
void setupDoorbell() {
  printf("Short lower threshold: %d micro seconds.\n", shortMin);
  printf("Short upper threshold: %d micro seconds.\n", shortMax);
  printf("Long lower threshold: %d micro seconds.\n", longMin);
  printf("Long upper threshold: %d micro seconds.\n", longMax);

  drbResultQueue = xQueueCreate(1, DRB_SEQ_LENGTH);
}

void IRAM_ATTR handleDoorbell(const unsigned long time) {

  static unsigned long lastTime = 0;
  static unsigned int size = 0;
  static unsigned int curIndex = 0;
  static uint8_t timings[DRB_SEQ_LENGTH];
  static  bool matching[PATTERN_LEN] = {};

  const unsigned int duration = time - lastTime;

  if (duration < shortMin) {
    return;
  }

  if (duration > longMax) {
    for (int index = 0; index < PATTERN_LEN; index++) {
      matching[index] =  false;
    }
    size = 0;
    curIndex = 0;

    for (int pos = 0; pos < DRB_SEQ_LENGTH; pos++) {
      timings[pos] = UNUSED;
    }
    lastTime = time;
    return;
  }

  bool isLong = duration >= longMin && duration <= longMax;
  bool isShort = duration >= shortMin && duration <= shortMax;
  if (isLong || isShort) {
    int recordedPulse;
    if (isLong) {
      recordedPulse = LONG;
    } else {
      recordedPulse = SHORT;
    }
    timings[curIndex] = recordedPulse;
    curIndex = (curIndex + 1) % DRB_SEQ_LENGTH;
    if (size < DRB_SEQ_LENGTH) {
      size++;
    }
    lastTime = time;


    bool last = true;
    for (int index = 0; index < PATTERN_LEN; index++) {
      bool org = matching[index];
      matching[index] =  last && (recordedPulse == PATTERN1[index] || recordedPulse == PATTERN2[index]);
      last = org;
    }

    if (matching[PATTERN_LEN - 1] && size >= DRB_SEQ_LENGTH) {
      uint8_t receivedPulses[DRB_SEQ_LENGTH];
      for (int pos = 0; pos < DRB_SEQ_LENGTH; pos++) {
        int bufPos = (DRB_SEQ_LENGTH + (curIndex - DRB_SEQ_LENGTH) + pos) % DRB_SEQ_LENGTH;
        receivedPulses[pos] = timings[bufPos];
      }
      BaseType_t xHigherPriorityTaskWoken = pdFALSE;
      xQueueSendToBackFromISR( drbResultQueue, &receivedPulses, &xHigherPriorityTaskWoken );
      if( xHigherPriorityTaskWoken == pdTRUE ) {
          portYIELD_FROM_ISR();
      }
    }
  }
}

uint8_t* getDrbReceivedPulses() {
    static uint8_t result[DRB_SEQ_LENGTH];
    xQueueReceive(drbResultQueue, result, portMAX_DELAY);
    printf("PATTERNS LEFT: %d\n", uxQueueMessagesWaiting(drbResultQueue));
    return result;
}

void printDoorbellWord(const uint8_t pulses[], const int len) {
  printf("BELL:");
  for (int pos = 0; pos < len / 2; pos++) {
    const uint8_t p1 = pulses[pos * 2];
    const uint8_t p2 = pulses[pos * 2 + 1];
    if (p1 == SHORT && p2 == LONG) {
      printf("0");
    } else if (p1 == LONG && p2 == SHORT) {
      printf("1");
    } else {
      printf("?");
    }
  }
  printf("\n");
}

