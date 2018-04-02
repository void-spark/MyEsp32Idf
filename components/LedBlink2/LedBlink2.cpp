
#include "LedBlink2.h"

extern "C" {
    static void timerCallback(TimerHandle_t xTimer) {
        LedBlink2 * ledBlink = (LedBlink2 *)pvTimerGetTimerID( xTimer );
        ledBlink->_trigger();
    }
}

//TODO: Destructor?
LedBlink2::LedBlink2(gpio_num_t pin){
    gpio_pad_select_gpio(pin);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, 0);

    _pin = pin;
}

//TODO: concurrent access between loop and timer? can we mux in timer? shouldnt block..
void LedBlink2::setPattern(long* pattern) {
    _pattern = pattern;
    _state = 0;
    _infinite = true;
    _times = 0;
    if(_timer == NULL) {
        _timer = xTimerCreate("ledBlinkTimer", 1, pdFALSE, this, timerCallback );
    }
    _trigger();    
}

void LedBlink2::setPattern(long* pattern, int times) {
    _pattern = pattern;
    _state = 0;
    _infinite = false;
    _times = times;
    if(_timer == NULL) {
        _timer = xTimerCreate("ledBlinkTimer", 1, pdFALSE, this, timerCallback );
    }
    _trigger();    
}

void LedBlink2::_trigger() {
    printf("state: %d times: %d timer %p\n", _state, _times, _timer);
    if(_state == 0) {
        if(!_infinite) {
            if(_times == 0) {
                _pattern = 0;
                xTimerStop(_timer, 0);
                return;
            }
            _times -= 1;
        }
        _state = 1;
        gpio_set_level(_pin, _state);
        xTimerChangePeriod(_timer, pdMS_TO_TICKS(_pattern[0]), 0);
    } else {
        _state = 0;
        gpio_set_level(_pin, _state);
        xTimerChangePeriod(_timer, pdMS_TO_TICKS(_pattern[1]), 0);
    }
}
