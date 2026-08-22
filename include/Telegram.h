#pragma once

#include <Arduino.h>

class Telegram
{
public:
    static void begin();
    static bool send(String title, String message);
    static bool sendKeyboard(const String &keyboardJson);
    static bool sendKeyboardTo(const String &chatId, const String &keyboardJson);
    static bool sendTo(const String &chatId, String title, String message);
    static void flushPending();
    static uint8_t pendingCount();
    static void update();
};
