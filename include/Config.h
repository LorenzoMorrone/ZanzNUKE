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
     * When true, the device assumes WiFi is only occasionally
     * available (e.g. a phone hotspot turned on now and then, a
     * weak/far router) rather than treating any extended outage as
     * something to recover from by rebooting. With it on,
     * NetworkManager never auto-restarts on a lost connection and
     * never auto-falls-back to broadcasting ZanzNuke-Setup just
     * because a saved network couldn't be reached - it simply
     * keeps retrying in the background, indefinitely, without
     * interrupting anything. See NetworkManager.cpp for the full
     * behavior. Irrigation/safety/scheduling never depended on
     * WiFi to begin with; this only changes how NetworkManager
     * itself reacts to not having it.
     */
    bool networklessMode = false;


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
     * Telegram
     */
    bool telegramEnabled = false;

    String telegramBotToken;

    String telegramChatId;

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