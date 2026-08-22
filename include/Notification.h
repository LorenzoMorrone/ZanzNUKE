#pragma once

#include <Arduino.h>

class Notification
{
public:
    static void begin();
    static bool send(String title, String message);
    static void flushPending();
    static uint8_t pendingCount();
};
