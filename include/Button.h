#pragma once

#include <Arduino.h>


/*
 * One instance per physical button (e.g. the start button on
 * PIN_BUTTON and the fault-reset button on PIN_FAULT_RESET) -
 * each needs its own debounce/edge-detection state, so this is
 * a plain instantiable class rather than the static-singleton
 * style used by the hardware "manager" classes elsewhere, which
 * only ever have one real instance.
 */
class Button
{

public:

    explicit Button(
        uint8_t pin
    );


    void begin();


    /*
     * Returns true exactly once per physical press
     * (debounced rising-to-falling edge), not repeatedly
     * while held down.
     */
    bool pressed();


private:

    uint8_t pin;

    uint32_t lastChangeMs;

    bool debouncedState;

    bool rawStateLast;

    bool latched;

};
