#include "Pushover.h"


#include <WiFiClientSecure.h>
#include <HTTPClient.h>


#include "Config.h"



void Pushover::begin()
{

}



bool Pushover::send(
    String title,
    String message
)
{

    if(
        config.pushoverToken.length()
        ==
        0
    )
        return false;



    if(
        WiFi.status()
        !=
        WL_CONNECTED
    )
        return false;



    WiFiClientSecure client;


    client.setInsecure();

    /*
     * Pushover::send() runs synchronously on the main loop task,
     * right after a safety fault has already stopped all relays
     * (see main.cpp / IrrigationManager::setError()). A hung
     * TCP connection would otherwise block the safety loop for
     * the platform's default timeout (tens of seconds); cap it
     * well under the watchdog timeout instead.
     */

    client.setTimeout(5000);



    HTTPClient http;

    http.setConnectTimeout(3000);

    http.setTimeout(5000);



    http.begin(
        client,
        "https://api.pushover.net/1/messages.json"
    );



    http.addHeader(
        "Content-Type",
        "application/x-www-form-urlencoded"
    );



    String body =
        "token="
        +
        config.pushoverToken
        +
        "&user="
        +
        config.pushoverUser
        +
        "&title="
        +
        title
        +
        "&message="
        +
        message;



    int result =
        http.POST(body);



    http.end();



    return result == 200;

}