#pragma once

#include <Arduino.h>



class Pushover
{

public:


    static void begin();


    /*
     * Sends immediately if possible. If WiFi is down, or the
     * request itself fails, the notification is queued instead of
     * being silently dropped - call flushPending() once
     * connectivity returns to deliver everything that backed up
     * while offline, in order.
     */
    static bool send(
        String title,
        String message
    );


    /*
     * Attempts to deliver every queued notification, oldest first.
     * Safe to call any time (e.g. speculatively on every loop) -
     * it's a no-op when there's nothing pending or WiFi is still
     * down. Intended to be called right after NetworkManager
     * detects a reconnect.
     */
    static void flushPending();


    static uint8_t pendingCount();



};