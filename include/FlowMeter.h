#pragma once

#include <Arduino.h>


class FlowMeter
{

public:

    static void begin();


    static uint32_t pulses();


    static void reset();


private:

    static volatile uint32_t pulseCounter;


    static void IRAM_ATTR pulseISR();

};