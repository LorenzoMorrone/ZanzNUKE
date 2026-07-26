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
     * Advanced: flow/leak detection tuning (SafetyManager).
     * Previously hardcoded constants - exposed here since they
     * directly control how sensitive leak/stuck-valve detection
     * is, which is exactly the kind of thing worth tuning without
     * a firmware reflash.
     */

    uint32_t flowStallTimeoutSeconds = 15;

    uint32_t flowLeakTolerancePulses = 30;

    uint32_t flowLeakGraceSeconds = 4;

    uint32_t flowLeakRebaselineSeconds = 60;


    /*
     * Advanced: WiFi resilience tuning (NetworkManager).
     */

    uint32_t wifiReconnectIntervalSeconds = 15;

    uint32_t wifiGiveUpRestartMinutes = 3;


    /*
     * Advanced: system watchdog (main.cpp). Only takes effect
     * after a restart - the watchdog is initialized once in
     * setup() and can't be reconfigured while running.
     */

    uint32_t watchdogTimeoutSeconds = 10;


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