#include "RcReceiver.h"

// Clean up some things first?
// The boolean stuff? replace by comparing against patterns?
// Or do we buffer non patterny, and then sorting out later??
// Print starting times?
// Currently using a very coarse selection, x kinda normal lenght pulses, and one long one.. Which works rather ok?
// Wouldn't work ok for doorbell though, hmm
// We can statistically guess afterwards what the actual protocol was, and all that.
// are we sure this gives more constant interrupt handling times then just checking the last MAX timings each time?

RcReceiver::RcReceiver(uint32_t bits_, uint32_t patterns) {

    _bits = bits_;
    _pulses = _bits * 2;

    resultQueue = xQueueCreate( patterns, sizeof( struct BitData ) * _bits );

    timings = new uint32_t[_pulses]();
    bits = new uint8_t[_bits * 2]();
    matching = new bool[_bits * 2]();

    trisHolder = new uint8_t[(_bits - 1) / 2 ]();

    bitDataHolder = new struct BitData[_bits]();
}

RcReceiver::~RcReceiver() {
  delete [] timings;
  delete [] bits;
  delete [] matching;
  delete [] trisHolder;
}

void RcReceiver::printInfo() {
  printf("Bits: %d.\n", _bits);
  printf("Low pass filter threshold: %d micro seconds.\n", LOW_PASS_LIMIT);
}


/**
 * The method which should be called on each CHANGE interrupt.
 * This implements a 'poor mans' low pass filter, and passes any remaining durations between
 * two interrupts to the handleDuration method.
*/
IRAM_ATTR void RcReceiver::handleInterrupt(const uint32_t time) {

  if (lastInterruptTime == 0) {
    lastInterruptTime = time;
    return;
  }

  const uint32_t interruptDuration = time - lastInterruptTime;
  lastInterruptTime = time;

  // Short pulse, we will ignore it by adding it's length to the last pulse, and also adding the length of the next pulse to last pulse.
  // Only then will we handle the last pulse again.
  if (interruptDuration < LOW_PASS_LIMIT) {
    // Far too short, ignore
    lastInterruptDuration += interruptDuration;
    addNext = true;
    return;
  }
  // The last duration was a short pulse, which we're ignoring, so only add the current pulse to last pulse, and don't handle it.
  if (addNext) {
    lastInterruptDuration += interruptDuration;
    addNext = false;
    return;
  }

  // The current pulse is not a short one, so we won't add it to the last pulse, so we can now handle the last pulse.
  if (lastInterruptDuration > 0) {
    handleDuration(lastInterruptDuration);
  }

  // Store the current duration, we might be adding to it if it's followed by short pulses.
  lastInterruptDuration = interruptDuration;
}

IRAM_ATTR uint8_t RcReceiver::getBitType(const uint32_t lastDuration, const uint32_t duration) {
  if (SYNC_FACTOR * lastDuration < duration ) {
    return BIT_S;
  } else if (lastDuration < duration) {
    return BIT_0;
  } else if ( lastDuration > duration) {
    return BIT_1;
  }
  return BIT_INVALID;
}

IRAM_ATTR void RcReceiver::handleDuration(const uint32_t duration) {

  // Store the current duration in the buffer.
  timings[bufferPos] = duration;

  const uint32_t bitLength = lastDuration + duration;
  const uint32_t lastBitLength = thirdLastDuration + secondLastDuration;

  const double bitLengthFact = (double)(bitLength) / (double)(lastBitLength);

  // How much is the length of each bit (two pulses) allowed to differ from the previous one?
  double fuzz = 1.5;
  bool bitLengthMatches = bitLengthFact > (1.0 / fuzz) && bitLengthFact < (1.0 * fuzz);

  // Determine the bit encoded by the current and last duration
  const uint8_t currentBit = getBitType(lastDuration, duration);
  // Store the current bit in the buffer.
  bits[bufferPos] = currentBit;

  // Update buffers size and pos
  bufferPos = (bufferPos + 1) % (_bits * 2);
  if (bufferSize < (_bits * 2)) {
    bufferSize++;
  }

  bool last = true;
  for (int index = 0; index < (_bits * 2); index++) {
    const bool org = matching[index];

    bool cur;
    if (index % 2 == 0) {
      // Can only check pairs
      cur = true;
    } else if (index != ((_bits * 2) - 1)) {
      cur = ((index == 1) || bitLengthMatches) && ((currentBit == BIT_0) || (currentBit == BIT_1));
    } else {
      cur = (currentBit == BIT_S);
    }
    matching[index] = last && cur;
    last = org;
  }

  if (matching[(_bits * 2) - 1]) {

        struct BitData temp[_bits];

        for(int pos = 0 ; pos < _bits; pos++) {
            // Position of the first pulse for this bit, might be past end of buffer
            const int bufPos = (bufferPos + pos * 2);
            // First pulse, wrapped around if needed.
            temp[pos].firstPulse = timings[bufPos % _pulses];
            // Second pulse, wrapped around if needed.
            temp[pos].secondPulse = timings[(bufPos + 1) % _pulses];
            // Bit value, which is at the second pulse position (need two pulses for a bit value).
            temp[pos].value = bits[(bufPos + 1) % (_bits * 2)];
        }

        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xQueueSendToBackFromISR( resultQueue, &temp, &xHigherPriorityTaskWoken );
        if( xHigherPriorityTaskWoken == pdTRUE ) {
            portYIELD_FROM_ISR();
        }
  }

  thirdLastDuration = secondLastDuration;
  secondLastDuration = lastDuration;
  lastDuration = duration;
}

/**
 * Waits for a received sequence, and returns a pointer to it.
 * The storage is reused, so don't call again until you're done with the previous sequence.
 */
struct BitData* RcReceiver::getReceivedBits () {
    xQueueReceive(resultQueue, bitDataHolder, portMAX_DELAY);
    printf("PATTERNS LEFT: %d\n", uxQueueMessagesWaiting(resultQueue));
    return bitDataHolder;
}

uint8_t* RcReceiver::getTris(const struct BitData* bitData) {

    for (int pos = 0; pos < (_bits -1) / 2; pos++) {
        int bitsIndex = pos * 2;
        uint8_t bitA = bitData[bitsIndex].value;
        uint8_t bitB = bitData[bitsIndex + 1].value;

        if (bitA == BIT_0 && bitB == BIT_0 ) {
            trisHolder[pos] = TRI_0;
        } else  if (bitA == BIT_1 && bitB == BIT_1 ) {
            trisHolder[pos] = TRI_1;
        } else    if (bitA == BIT_0 && bitB == BIT_1 ) {
            trisHolder[pos] = TRI_F;
        } else {
            trisHolder[pos] = TRI_INVALID;
        }
    }

  return trisHolder;
}

void RcReceiver::printAcTriState(const uint8_t tris[]) {
  printf("AC:");
  for (int pos = 0; pos < (_bits - 1) / 2; pos++) {
    if (tris[pos] == TRI_0) {
      printf("0");
    } else if (tris[pos] == TRI_1) {
      printf("1");
    } else if (tris[pos] == TRI_F) {
      printf("f");
    } else {
      printf("?");
    }
  }
  printf("\n");
}

void RcReceiver::printTimings(const struct BitData* bitData, bool bits, bool durations) {

  if (bits && !durations) {
    printf("BITS: ");
  } else if (!bits && durations) {
    printf("DURATIONS: ");
  } else if (bits && durations) {
    printf("BITS+DURATIONS: ");
  }

  for (int pos = 0; pos < _bits; pos++) {
    if (pos != 0) {
      if (durations) {
        printf(", ");
      }
    }
    const uint32_t durationA = bitData[pos].firstPulse;
    const uint32_t durationB = bitData[pos].secondPulse;
    const char* first;
    const char* second;

    const uint8_t currentBit = getBitType(durationA, durationB);
    if (currentBit == BIT_0) {
      first = ".";
      second = "-";
    } else if (currentBit == BIT_1) {
      first = "-";
      second = ".";
    } else if (currentBit == BIT_S) {
      first = ".";
      second = "S";
    } else {
      first = "?";
      second = "?";
    }

    if (bits) {
      printf("%s", first);
    }
    if (durations) {
      printf("%d, ", durationA);
    }
    if (bits) {
      printf("%s", second);
    }
    if (durations) {
      printf("%d", durationB);
    }
  }
  printf("\n");
}

void RcReceiver::printAcTimings(const struct BitData* bitData) {
  printTimings( bitData, true, false );
  printTimings( bitData, true, true );
  //printTimings( bitData, false, true );
}

void RcReceiver::printDetails(const struct BitData* bitData) {
  double maxBitLengthFact = 0.0;

  uint32_t lastBitLength = 0;

  uint32_t bit0Cnt = 0;
  uint32_t bit0FirstTotal = 0;
  uint32_t bit0SecondTotal = 0;

  uint32_t bit1Cnt = 0;
  uint32_t bit1FirstTotal = 0;
  uint32_t bit1SecondTotal = 0;

  for (int pos = 0; pos < _bits - 1; pos++) {
    const uint32_t durationA = bitData[pos].firstPulse;
    const uint32_t durationB = bitData[pos].secondPulse;
    const uint32_t bitLength = durationA + durationB;
    if (lastBitLength > 0) {
      double bitLengthFact = (double)(bitLength) / (double)(lastBitLength);
      if (bitLengthFact < 1.0) {
        bitLengthFact = 1.0 / bitLengthFact;
      }
      if (maxBitLengthFact < bitLengthFact) {
        maxBitLengthFact = bitLengthFact;
      }
    }
    uint8_t bitType = getBitType(durationA, durationB);
    if (bitType == BIT_0) {
      bit0Cnt++;
      bit0FirstTotal += durationA;
      bit0SecondTotal += durationB;
    }
    if (bitType == BIT_1) {
      bit1Cnt++;
      bit1FirstTotal += durationA;
      bit1SecondTotal += durationB;
    }
    lastBitLength = bitLength;
  }
  printf("MAX FACTOR: %f - BIT: %d - BIT0: %d, %d - BIT1: %d, %d - SYNC: %d, %d, %s\n",
    maxBitLengthFact,
    (bit0FirstTotal + bit0SecondTotal + bit1FirstTotal + bit1SecondTotal) / (bit0Cnt + bit1Cnt),
    bit0FirstTotal / bit0Cnt,bit0SecondTotal / bit0Cnt,
    bit1FirstTotal / bit1Cnt,
    bit1SecondTotal / bit1Cnt,
    bitData[_bits - 1].firstPulse,
    bitData[_bits - 1].secondPulse,
    getBitType(bitData[_bits - 1].firstPulse, bitData[_bits - 1].secondPulse) == BIT_S ? "YES" : "NO"
   );
}
