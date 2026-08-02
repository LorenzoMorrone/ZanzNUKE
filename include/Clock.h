#pragma once

#include <Arduino.h>


class Clock
{

public:

    static void begin();


    /*
     * Re-kicks the SNTP client so time resyncs right away instead
     * of waiting for its background poll interval (up to an hour).
     * Call this whenever WiFi comes back after being lost - between
     * calls, system time keeps advancing on its own off the
     * internal RTC/oscillator, so it stays "close enough" even
     * through an extended outage; this just corrects any drift
     * once a network is available again.
     */
    static void resync();


    static String datetime();


    static bool valid();


};