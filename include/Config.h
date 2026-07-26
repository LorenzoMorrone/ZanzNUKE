#pragma once

#include <Arduino.h>


struct IrrigationConfig
{
    /*
     * Flow meter calibration
     */
    float pulsesPerLiter = 450.0;


    /*
     * Amount of water per irrigation
     */
    float irrigationLiters = 20.0;


    /*
     * Water used for washing
     */
    float washLiters = 10.0;


    /*
     * Fertilizer pump calibration
     *
     * seconds of pump operation
     * per liter of water
     */
    float fertilizerSecondsPerLiter = 5.0;


    /*
     * Safety timers
     */

    uint32_t valveTimeoutSeconds = 300;

    uint32_t irrigationTimeoutSeconds = 600;


    /*
     * WiFi
     */

    String wifiSSID;

    String wifiPassword;


    /*
     * Pushover
     */

    String pushoverToken;

    String pushoverUser;

    /*
     * Save/load
     */

    bool load();

    bool save();

    void reset();


    /*
     * Sanity-checks values so a corrupted/blank NVS entry
     * (or a bad web form submission) can't produce a
     * divide-by-zero or a runaway timer.
     */
    bool valid() const;
};


extern IrrigationConfig config;