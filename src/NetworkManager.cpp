#include "NetworkManager.h"
#include <Pushover.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>

#include "Config.h"
#include "WebStyle.h"
#include "Clock.h"


/*
 * Dedicated to the setup wizard only - see beginSetupServer().
 * A completely separate AsyncWebServer instance from
 * WebServerManager's, and only ever begin()'s while apMode is
 * true, so the two never listen on port 80 at the same time.
 */
static AsyncWebServer setupServer(80);



bool NetworkManager::wifiConnected=false;

bool NetworkManager::apMode=false;

uint32_t NetworkManager::disconnectedSinceMs = 0;

uint32_t NetworkManager::lastReconnectAttemptMs = 0;


/*
 * How often to actively retry a lost STA connection, and how
 * long to keep retrying before giving up and rebooting, both
 * come from config (see Config.h's "Advanced: WiFi resilience
 * tuning" fields and the config page's Advanced section):
 *
 *   config.wifiReconnectIntervalSeconds - retry WiFi.reconnect()
 *     this often rather than relying solely on the arduino-esp32
 *     core's implicit auto-reconnect, which isn't guaranteed to
 *     kick in after every kind of disconnect (e.g. one following
 *     a failed TLS handshake can leave the WiFi/TCP stack in a
 *     state it doesn't cleanly recover from on its own).
 *
 *   config.wifiGiveUpRestartMinutes - if retrying hasn't worked
 *     for this long, stop trying to be clever and just reboot. A
 *     full restart resets the radio, TCP stack and heap
 *     unconditionally, which is a strictly stronger recovery than
 *     anything reconnect() can do from inside a possibly-wedged
 *     WiFi driver state. Irrigation state is not preserved across
 *     reboot by design (see IrrigationManager / requirements), so
 *     this is a safe fallback, not a data-loss risk - and it also
 *     guarantees the device eventually falls back to broadcasting
 *     ZanzNuke-Setup if the saved network is gone for good,
 *     rather than sitting disconnected forever.
 */



void NetworkManager::begin()
{

    Serial.println(
        "[WiFi] Starting in station mode"
    );

    WiFi.mode(WIFI_STA);



    if(
        connectSaved()
    )
    {
        wifiConnected=true;
        apMode=false;
        Serial.println(
            "[WiFi] Connected to saved network"
        );
        return;
    }



    Serial.println(
        "[WiFi] No saved network connection, starting AP"
    );
    startAP();

}





bool NetworkManager::connectSaved()
{


    if(
        config.wifiSSID.length()==0
    )
    {
        Serial.println(
            "[WiFi] No SSID configured"
        );
        return false;
    }


    Serial.print(
        "[WiFi] Trying to connect to: "
    );
    Serial.println(
        config.wifiSSID
    );


    WiFi.begin(
        config.wifiSSID.c_str(),
        config.wifiPassword.c_str()
    );


    uint32_t start =
        millis();



    while(
        millis()-start < 30000
    )
    {

        wl_status_t status =
            WiFi.status();

        if(
            status
            ==
            WL_CONNECTED
        )
        {
            Serial.print(
                "[WiFi] Connected. IP: "
            );
            Serial.println(
                WiFi.localIP().toString()
            );
            Pushover::send(
                "ZanzNuke - New IP address",
                WiFi.localIP().toString()
            );
            return true;

        }

        if(
            status != WL_IDLE_STATUS
        )
        {
            Serial.print(
                "[WiFi] Connection status: "
            );
            Serial.println(
                status
            );
        }


        delay(1000);

    }


    Serial.println(
        "[WiFi] Connection timed out"
    );

    /*
     * Cleanly abandon the failed STA attempt before anything
     * else touches the radio (startAP() switches mode next).
     * Without this, the WiFi driver can be left mid-scan/
     * mid-associate, and a mode switch straight to WIFI_AP_STA
     * from that state can result in softAP() reporting success
     * (a valid IP is assigned) while the AP never actually
     * broadcasts beacons - it looks "started" in the log but is
     * invisible to any device scanning for it.
     */

    WiFi.disconnect(true);

    return false;

}





void NetworkManager::startAP()
{

    Serial.println(
        "[WiFi] Switching to access point mode"
    );

    /*
     * Fully reset the radio before reconfiguring it. Going
     * straight from a just-failed STA attempt to WIFI_AP_STA
     * has been observed to leave the AP registered (softAP()
     * returns success, softAPIP() is valid) but not actually
     * transmitting beacons - i.e. invisible to scanning devices
     * even though the logs say it "started". A clean OFF ->
     * settle -> desired mode -> settle sequence avoids that.
     */

    WiFi.mode(
        WIFI_OFF
    );

    delay(100);


    /*
     * Plain AP, not AP_STA: the setup server (beginSetupServer(),
     * below) never scans for networks - a scan would force the
     * shared radio to hop channels, cutting the very AP link a
     * connected phone/laptop is using to reach it in the first
     * place. Staying single-role AP keeps the beacon rock solid.
     */
    WiFi.mode(
        WIFI_AP
    );

    delay(100);


    bool apStarted =
        WiFi.softAP(
            "ZanzNuke-Setup"
        );

    apMode = true;

    if(apStarted)
    {
        Serial.print(
            "[WiFi] AP started. IP: "
        );
        Serial.println(
            WiFi.softAPIP().toString()
        );
    }
    else
    {
        Serial.println(
            "[WiFi] Failed to start AP"
        );
    }


    beginSetupServer();

}



void NetworkManager::beginSetupServer()
{

    /*
     * exact() matchers, for the same reason as WebServerManager::
     * setupRoutes() - a bare string route in ESPAsyncWebServer
     * also prefix-matches "<uri>/...", so a "/" route registered
     * first would shadow everything registered after it.
     */

    setupServer.on(
        AsyncURIMatcher::exact("/"),
        HTTP_GET,
        [](AsyncWebServerRequest *request)
        {

            String html = htmlHead("ZanzNuke - WiFi Setup");

            html += "<h1>ZanzNuke Setup</h1>";

            html += "<div class='card'>"
                    "<p class='hint' style='margin-top:0'>Connected to the "
                    "<b>ZanzNuke-Setup</b> access point. Enter your home "
                    "network's details below to connect the device to it.</p>"
                    "</div>";

            html += "<form action='/save' method='get'>";

            html += "<div class='card'>";

            html += "<label>Network name (SSID)</label>";
            html += "<input name='ssid' type='text' autocapitalize='off' autocorrect='off'>";

            html += "<label>Password</label>";
            html += "<input name='pass' type='password'>";

            html += "</div><button>Save and Connect</button></form>";

            html += htmlFoot();

            request->send(
                200,
                "text/html",
                html
            );

        }
    );


    setupServer.on(
        AsyncURIMatcher::exact("/save"),
        HTTP_GET,
        [](AsyncWebServerRequest *request)
        {

            if(
                !request->hasParam("ssid")
                ||
                request->getParam("ssid")->value().length() == 0
            )
            {

                String html = htmlHead("ZanzNuke - WiFi Setup");

                html += "<h1>ZanzNuke Setup</h1>"
                        "<div class='card'>"
                        "<p class='err' style='margin-top:0'>Please enter a network name.</p>"
                        "<a class='btn btn-secondary' href='/'>Back</a>"
                        "</div>";

                html += htmlFoot();

                request->send(400, "text/html", html);

                return;

            }


            config.wifiSSID =
                request->getParam("ssid")->value();


            config.wifiPassword =
                request->hasParam("pass")
                ? request->getParam("pass")->value()
                : "";


            Serial.print(
                "[WiFi] Saving credentials for SSID: "
            );
            Serial.println(config.wifiSSID);


            config.save();


            String html = htmlHead("ZanzNuke - WiFi Setup");

            html += "<h1>ZanzNuke Setup</h1>"
                    "<div class='card'>"
                    "<p class='ok' style='margin-top:0'>Saved. Restarting&hellip;</p>"
                    "<p class='hint'>Reconnect to your normal network in about a minute, "
                    "or rejoin <b>ZanzNuke-Setup</b> if it can't connect.</p>"
                    "</div>";

            html += htmlFoot();

            request->send(200, "text/html", html);


            /*
             * Short delay so the response above is actually
             * flushed to the client before the reboot, then
             * restart to re-run NetworkManager::begin() with the
             * newly saved credentials. Blocking the async task
             * here is acceptable: this server exists only during
             * setup and has nothing else to serve.
             */

            delay(1000);

            ESP.restart();

        }
    );


    setupServer.begin();


    Serial.println(
        "[WiFi] Setup server listening on port 80"
    );

}





void NetworkManager::update()
{

    if(
        wifiConnected
        &&
        !connected()
    )
    {
        wifiConnected = false;

        disconnectedSinceMs =
            millis();

        /*
         * connectSaved() already just performed a full WiFi.begin()
         * attempt before we ever got here, so there's no point
         * retrying again immediately - start the retry clock from
         * now.
         */

        lastReconnectAttemptMs =
            millis();

        Serial.println(
            "[WiFi] Connection lost"
        );
    }

    if(
        !wifiConnected
        &&
        connected()
    )
    {
        wifiConnected = true;

        disconnectedSinceMs = 0;

        Serial.print(
            "[WiFi] Reconnected. IP: "
        );
        Serial.println(
            WiFi.localIP().toString()
        );

        /*
         * The device kept running on its own while offline
         * (irrigation/scheduling/safety never depend on WiFi -
         * see IrrigationManager/SafetyManager/Scheduler), free-
         * running the clock off its internal timer in the
         * meantime. Now that a network is back: pull the clock
         * back in line immediately rather than waiting for SNTP's
         * own poll interval, and push out anything that piled up
         * in the notification backlog while we couldn't reach
         * Pushover.
         */

        Clock::resync();

        Pushover::flushPending();

    }


    /*
     * Actively try to recover a lost STA connection instead of
     * just sitting there disconnected. Never runs while
     * deliberately operating as the setup access point.
     */

    if(
        !apMode
        &&
        !wifiConnected
        &&
        disconnectedSinceMs != 0
    )
    {

        uint32_t now =
            millis();


        /*
         * 0 means "never give up on my own" - irrigation, safety
         * and the schedule all keep running fine off the internal
         * clock while disconnected (see Scheduler/SafetyManager/
         * IrrigationManager, none of which check WiFi state), so
         * forcing a reboot here is a choice, not a requirement.
         * Falling back to broadcasting ZanzNuke-Setup on a long
         * outage is exactly the behavior some setups don't want
         * (it silently stops the scheduler until someone notices
         * and reconnects it by hand), so respect 0 as "keep
         * retrying WiFi.reconnect() forever, don't reboot".
         */

        if(
            config.wifiGiveUpRestartMinutes > 0
            &&
            now - disconnectedSinceMs
            >
            config.wifiGiveUpRestartMinutes * 60UL * 1000UL
        )
        {

            Serial.println(
                "[WiFi] Disconnected too long, restarting to recover"
            );

            delay(200);

            ESP.restart();

        }


        if(
            now - lastReconnectAttemptMs
            >
            config.wifiReconnectIntervalSeconds * 1000UL
        )
        {

            lastReconnectAttemptMs = now;

            Serial.println(
                "[WiFi] Attempting to reconnect..."
            );

            WiFi.reconnect();

        }

    }

}





bool NetworkManager::connected()
{

    return
    WiFi.status()
    ==
    WL_CONNECTED;

}





String NetworkManager::ip()
{

    if(connected())
        return WiFi.localIP().toString();


    return WiFi.softAPIP().toString();

}



bool NetworkManager::isAPMode()
{
    return apMode;
}
