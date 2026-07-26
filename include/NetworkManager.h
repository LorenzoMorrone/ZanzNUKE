#pragma once

#include <Arduino.h>


class NetworkManager
{

public:

    static void begin();

    static void update();

    static bool connected();

    static String ip();


    /*
     * True once we've fallen back to running our own access
     * point (either no SSID was configured, or the configured
     * network couldn't be reached within the connect timeout).
     * WebServerManager uses this to decide whether to serve the
     * WiFi setup wizard or the normal dashboard at "/".
     */
    static bool isAPMode();


private:

    static bool wifiConnected;

    static bool apMode;

    static uint32_t disconnectedSinceMs;

    static uint32_t lastReconnectAttemptMs;

    static void startAP();

    static bool connectSaved();

    /*
     * Minimal, self-contained WiFi setup page + save handler,
     * on its own dedicated AsyncWebServer instance that only
     * ever runs while apMode is true. Deliberately kept as
     * simple as possible (plain HTML form, no JS, no JSON API,
     * no shared state with WebServerManager) - this is the one
     * thing that MUST work reliably even through a phone's
     * restrictive captive-portal mini browser, so it stays
     * isolated from the rest of the web UI's complexity.
     */
    static void beginSetupServer();

};