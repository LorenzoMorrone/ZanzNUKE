#include <Arduino.h>

#include "esp_task_wdt.h"
#include "esp_idf_version.h"


#include "Config.h"

#include "Pins.h"

#include "Outputs.h"
#include "Sensors.h"
#include "FlowMeter.h"

#include "RGBLed.h"

#include "SafetyManager.h"
#include "IrrigationManager.h"

#include "NetworkManager.h"
#include "WebServer.h"
#include "Clock.h"

#include "Scheduler.h"

#include "Button.h"

#include "Pushover.h"

#include "OTAUpdate.h"

#include "EventLog.h"

#include "ErrorStrings.h"



ErrorCode lastReportedError =
    ErrorCode::NONE;


/*
 * Two independent physical buttons, each with its own
 * debounce/edge-detection state (see Button.h):
 *   - startButton on PIN_BUTTON: same as before, requests an
 *     irrigation cycle.
 *   - resetButton on PIN_FAULT_RESET: hardware equivalent of the
 *     web UI's "Clear Error" button.
 */

Button startButton(PIN_BUTTON);

Button resetButton(PIN_FAULT_RESET);



void setup()
{

    Serial.begin(115200);


    delay(500);


    Serial.println();
    Serial.println(
        "=== Irrigation Controller boot ==="
    );



    /*
     * FIRST THING:
     *
     * Make sure nothing can start
     */

    Outputs::begin();

    Outputs::stopAll();



    /*
     * Load saved configuration
     */

    config.load();



    /*
     * Hardware initialization
     */

    Sensors::begin();

    FlowMeter::begin();

    RGBLed::begin();

    startButton.begin();

    resetButton.begin();



    /*
     * Check impossible sensor state
     */

    if(
        Sensors::tankFull()
        &&
        Sensors::tankEmpty()
    )
    {

        Serial.println(
            "Sensor fault detected"
        );

        RGBLed::set(
            LedMode::RED
        );

    }



    /*
     * Network
     */

    NetworkManager::begin();


    Clock::begin();


    Pushover::begin();


    /*
     * The full dashboard/config/schedule web UI only starts once
     * we're actually connected to a real network. While running
     * as the setup access point, NetworkManager owns port 80
     * itself with a minimal, dedicated WiFi setup page (see
     * NetworkManager::beginSetupServer()) - keeping that flow
     * completely isolated from the rest of the web UI is what
     * makes it reliable through a phone's restrictive
     * captive-portal mini browser.
     */

    if(!NetworkManager::isAPMode())
    {
        WebServerManager::begin();
    }


    OTAUpdate::begin();



    /*
     * Controllers
     *
     * IrrigationManager::begin() runs BEFORE SafetyManager::begin()
     * on purpose: SafetyManager::begin() may immediately trip
     * CONFIG_INVALID (via IrrigationManager::externalTrip()) if
     * the loaded configuration is bad, and it must not be
     * clobbered back to IDLE by a later IrrigationManager::begin().
     */

    Scheduler::begin();

    IrrigationManager::begin();

    SafetyManager::begin();



    /*
     * Watchdog
     *
     * esp_task_wdt_init()'s signature changed between
     * arduino-esp32 2.x (ESP-IDF 4.x) and 3.x (ESP-IDF 5.x).
     * platformio.ini doesn't pin the platform version, so
     * support both.
     *
     * Timeout comes from config (Advanced section of the config
     * page) rather than a fixed constant, clamped defensively:
     * too low risks a reboot loop during a
     * legitimately slow operation (flash write, TLS handshake),
     * too high defeats the point of having a watchdog at all.
     * Only takes effect after a restart, since this only runs
     * once in setup().
     */

    uint32_t watchdogSeconds =
        config.watchdogTimeoutSeconds;

    if(watchdogSeconds < 5) watchdogSeconds = 5;
    if(watchdogSeconds > 120) watchdogSeconds = 120;

#if ESP_IDF_VERSION_MAJOR >= 5

    esp_task_wdt_config_t wdtConfig = {
        .timeout_ms = watchdogSeconds * 1000,
        .idle_core_mask = 0,
        .trigger_panic = true
    };

    esp_err_t wdtInitResult =
        esp_task_wdt_init(&wdtConfig);

#else

    esp_err_t wdtInitResult =
        esp_task_wdt_init(
            watchdogSeconds,
            true
        );

#endif


    /*
     * ESP_ERR_INVALID_STATE just means the framework already
     * initialized the task watchdog before setup() ran, which is
     * harmless. Anything else is worth surfacing: a watchdog that
     * silently isn't covering the loop task would let a hang go
     * unrecovered.
     */

    if(
        wdtInitResult != ESP_OK
        &&
        wdtInitResult != ESP_ERR_INVALID_STATE
    )
    {
        Serial.print(
            "[Main] WARNING: watchdog init failed: "
        );
        Serial.println(
            esp_err_to_name(wdtInitResult)
        );
    }


    esp_err_t wdtAddResult =
        esp_task_wdt_add(
            NULL
        );

    if(wdtAddResult != ESP_OK)
    {
        Serial.print(
            "[Main] WARNING: could not register loop task with watchdog: "
        );
        Serial.println(
            esp_err_to_name(wdtAddResult)
        );
    }



    EventLog::add(
        "Boot completed"
    );



    RGBLed::set(
        LedMode::GREEN
    );


    Serial.println(
        "System ready"
    );

    Serial.println(
        "[Main] Boot complete; monitoring started"
    );

}





void loop()
{

    /*
     * Feed watchdog
     */

    esp_task_wdt_reset();



    /*
     * Network services
     */

    NetworkManager::update();

    OTAUpdate::update();



    /*
     * Automatic scheduler
     */

    Scheduler::update();



    /*
     * SAFETY FIRST
     */

    SafetyManager::update();



    /*
     * Main irrigation state machine
     */

    IrrigationManager::update();


    /*
     * Manual diagnostic relay tests requested from the web UI
     */

    WebServerManager::update();



    /*
     * Physical buttons
     */

    if(
        startButton.pressed()
    )
    {

        Serial.println(
            "[Main] Manual start button pressed"
        );


        IrrigationManager::requestStart();


        EventLog::add(
            "Manual start"
        );

    }


    if(
        resetButton.pressed()
    )
    {

        Serial.println(
            "[Main] Fault reset button pressed"
        );


        SafetyManager::requestClear();


        EventLog::add(
            "Manual fault reset"
        );

    }


    /*
     * Hold the fault-reset button for WIFI_RESET_HOLD_MS to force
     * the device back into WiFi setup mode (broadcasting
     * ZanzNuke-Setup) - the physical way to reconfigure it when
     * it's not reachable over the network to do that from the
     * Config page instead (e.g. it's quietly retrying a network
     * that's no longer there in Networkless mode). A plain press
     * already requested a fault clear above the moment it was
     * detected; holding past the threshold is additive, not a
     * different gesture - fires once per hold via wifiResetFired,
     * consumed here and re-armed once the button is released.
     */

    constexpr uint32_t WIFI_RESET_HOLD_MS = 5000;

    static bool wifiResetFired = false;


    if(resetButton.heldMs() >= WIFI_RESET_HOLD_MS)
    {

        if(!wifiResetFired)
        {

            wifiResetFired = true;

            Serial.println(
                "[Main] Fault reset button held - forcing WiFi setup mode"
            );

            EventLog::add(
                "Forced WiFi setup mode (button held)"
            );

            RGBLed::set(LedMode::YELLOW);

            RGBLed::update();


            NetworkManager::forceSetupMode();

        }

    }
    else
    {

        wifiResetFired = false;

    }





    /*
     * LED status
     */

    if(
        SafetyManager::hasError()
    )
    {

        RGBLed::set(
            LedMode::RED
        );

    }
    else
    {

        switch(
            IrrigationManager::state()
        )
        {


        case IrrigationState::IDLE:

            RGBLed::set(
                LedMode::GREEN
            );

            break;



        case IrrigationState::DOSING_FERTILIZER:

        case IrrigationState::FILLING_TANK:

        case IrrigationState::IRRIGATING:

            RGBLed::set(
                LedMode::BLUE
            );

            break;



        default:

            break;

        }

    }


    RGBLed::update();





    /*
     * Send error notification once per error occurrence.
     *
     * lastReportedError must be reset back to NONE as soon as
     * the fault clears - otherwise, after a clear + retrigger of
     * the SAME error type, currentError == lastReportedError
     * would still hold from the previous occurrence and the
     * notification (and this Serial print) would silently never
     * fire again, even though a brand new fault just happened.
     */

    ErrorCode currentError =
        SafetyManager::error();


    if(currentError == ErrorCode::NONE)
    {

        lastReportedError =
            ErrorCode::NONE;

    }
    else if(
        currentError != lastReportedError
    )
    {


        lastReportedError =
            currentError;



        String message =
            errorToString(
                currentError
            );



        EventLog::add(
            "ERROR: "
            +
            message
        );



        Pushover::send(
            "ZanzNuke ERROR",
            message
        );


        Serial.println(
            message
        );

    }



}