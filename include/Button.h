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


    /*
     * How long (ms) the button has been continuously held down,
     * post-debounce; 0 while not pressed. Lets a caller detect a
     * long-press (e.g. "hold 5s to force WiFi setup mode") on top
     * of the normal short-press action from pressed() - relies on
     * pressed() being polled every loop() to keep the debounced
     * state current, same as everywhere else this class is used.
     */
    uint32_t heldMs() const;


private:

    uint8_t pin;

    uint32_t lastChangeMs;

    uint32_t pressStartMs;

    bool debouncedState;

    bool rawStateLast;

    bool latched;

};
