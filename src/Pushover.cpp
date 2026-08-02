#include "Pushover.h"


#include <WiFiClientSecure.h>
#include <HTTPClient.h>


#include "Config.h"



/*
 * Backlog for notifications that couldn't go out (WiFi down, or
 * the request itself failed) so they aren't silently lost - they
 * get delivered once flushPending() is called after reconnecting.
 * Fixed-size ring, oldest-dropped-first on overflow: a handful of
 * queued Strings is negligible against the heap, but an unbounded
 * queue during a long outage is not.
 */

struct PendingNotification
{
    String title;
    String message;
};

constexpr uint8_t MAX_PENDING = 10;

static PendingNotification pending[MAX_PENDING];

static uint8_t pendingCount_ = 0;


static void enqueue(
    const String &title,
    const String &message
)
{

    if(pendingCount_ >= MAX_PENDING)
    {

        for(uint8_t i = 1; i < MAX_PENDING; i++)
            pending[i - 1] = pending[i];

        pendingCount_ = MAX_PENDING - 1;

    }


    pending[pendingCount_].title = title;

    pending[pendingCount_].message = message;

    pendingCount_++;

}



static bool sendNow(
    const String &title,
    const String &message
)
{

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



    /*
     * Try to clear out anything already backed up first, so
     * notifications stay in order rather than a brand new one
     * jumping ahead of older queued ones.
     */

    flushPending();



    if(
        WiFi.status()
        !=
        WL_CONNECTED
    )
    {

        enqueue(title, message);

        return false;

    }



    bool ok =
        sendNow(title, message);


    if(!ok)
        enqueue(title, message);


    return ok;

}



void Pushover::flushPending()
{

    if(pendingCount_ == 0)
        return;


    if(
        config.pushoverToken.length()
        ==
        0
    )
    {

        /*
         * Notifications were disabled while some were queued -
         * nowhere to deliver them, drop rather than hold onto
         * them forever.
         */

        pendingCount_ = 0;

        return;

    }


    if(
        WiFi.status()
        !=
        WL_CONNECTED
    )
        return;



    uint8_t count =
        pendingCount_;

    pendingCount_ = 0;


    for(uint8_t i = 0; i < count; i++)
    {

        /*
         * Best-effort: a failure here just means this one is
         * lost rather than re-queued - retrying forever on a
         * message the Pushover API keeps rejecting would only
         * ever push newer, more relevant notifications further
         * back in the queue.
         */

        sendNow(pending[i].title, pending[i].message);

    }

}



uint8_t Pushover::pendingCount()
{

    return pendingCount_;

}