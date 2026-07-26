#include "OTAUpdate.h"

#include <ArduinoOTA.h>
#include <ESPmDNS.h>


/*
 * Also doubles as the mDNS hostname: ArduinoOTA.begin() calls
 * MDNS.begin(hostname) internally (see ArduinoOTA.cpp), so
 * setting this is what makes the device reachable at
 * "zanzstop.local" on the LAN without needing to know its IP -
 * no separate MDNS.begin() call needed (and calling it a second
 * time ourselves would just conflict with this one).
 */
constexpr const char* MDNS_HOSTNAME = "zanzstop";


void OTAUpdate::begin()
{

    ArduinoOTA.setHostname(
        MDNS_HOSTNAME
    );


    ArduinoOTA
    .onStart(
    []()
    {
        Serial.println(
            "OTA started"
        );
    });


    ArduinoOTA
    .onEnd(
    []()
    {
        Serial.println(
            "OTA finished"
        );
    });


    ArduinoOTA.begin();


    /*
     * Advertise the web dashboard too, so it shows up by name in
     * mDNS-aware network browsers/apps, not just resolvable by
     * exact hostname.
     */

    MDNS.addService(
        "http",
        "tcp",
        80
    );

    Serial.print(
        "[OTA] Reachable at http://"
    );
    Serial.print(MDNS_HOSTNAME);
    Serial.println(
        ".local (and via OTA at the same address)"
    );

}



void OTAUpdate::update()
{

    ArduinoOTA.handle();

}