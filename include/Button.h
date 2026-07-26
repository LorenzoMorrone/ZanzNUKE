#pragma once

#include <Arduino.h>


class Button
{

public:

    static void begin();


    /*
     * Returns true exactly once per physical press
     * (debounced rising-to-falling edge), not repeatedly
     * while held down.
     */
    static bool pressed();


private:

    static uint32_t lastChangeMs;

    static bool debouncedState;

    static bool rawStateLast;

    static bool latched;

};