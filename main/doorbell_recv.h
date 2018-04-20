#ifndef DOORBELL_RECV_H
#define DOORBELL_RECV_H

#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_system.h"

const int DRB_SEQ_LENGTH = 80;

enum { UNUSED, SHORT, LONG };


extern QueueHandle_t drbResultQueue;


void setupDoorbell();
void IRAM_ATTR handleDoorbell(const unsigned long time);
uint8_t* getDrbReceivedPulses();
void printDoorbellWord(const uint8_t pulses[], const int len);

#endif

