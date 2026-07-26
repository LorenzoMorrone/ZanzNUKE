#include "FlowMeter.h"

#include "Pins.h"



volatile uint32_t FlowMeter::pulseCounter = 0;



void IRAM_ATTR FlowMeter::pulseISR()
{
    pulseCounter++;
}



void FlowMeter::begin()
{

    pinMode(
        PIN_FLOW,
        INPUT_PULLUP
    );


    attachInterrupt(
        digitalPinToInterrupt(PIN_FLOW),
        pulseISR,
        FALLING
    );

}



uint32_t FlowMeter::pulses()
{
    return pulseCounter;
}



void FlowMeter::reset()
{

    noInterrupts();

    pulseCounter = 0;

    interrupts();

}