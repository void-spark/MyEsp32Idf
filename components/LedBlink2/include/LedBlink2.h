#ifndef LedBlink2_h
#define LedBlink2_h

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

class LedBlink2 {
  public:
    LedBlink2(gpio_num_t pin);
    void setPattern(long* pattern);
    void setPattern(long* pattern, int times);
    void _trigger();
  private:
    gpio_num_t _pin;
    long* _pattern = 0;
    bool _infinite = true;
    int _times = 0;
    int _state = 0;
    TimerHandle_t _timer = NULL;
};

#endif
