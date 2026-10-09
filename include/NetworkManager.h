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


    /*
     * Forces the setup access point back on, without touching the
     * saved SSID/password - for when the device needs to be
     * reconfigured (new network, moved house, etc.) but either
     * isn't currently reachable to do that through the normal
     * Config page WiFi fields (e.g. it's off retrying quietly in
     * Networkless mode), or the physical fault-reset button is
     * being used to request it directly (see main.cpp's long-press
     * handling). Sets a flag that survives the restart this
     * triggers (RTC memory, not NVS - deliberately doesn't survive
     * a full power loss, so a normal power-cycle still goes back
     * to trying the saved network rather than getting stuck
     * offering setup mode forever) and reboots immediately; on the
     * next boot, begin() sees the flag, consumes it, and jumps
     * straight to the setup AP without even attempting
     * connectSaved() - the saved credentials are left untouched,
     * so if nothing new gets saved, a subsequent power-cycle just
     * resumes trying them normally.
     */
    static void forceSetupMode();


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