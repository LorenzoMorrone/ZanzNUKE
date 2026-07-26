#pragma once

#include <Arduino.h>


enum class LedMode
{
    OFF,

    GREEN,

    BLUE,

    RED,

    YELLOW
};



class RGBLed
{

public:

    static void begin();


    static void set(
        LedMode mode
    );


    static void update();



private:

    static LedMode currentMode;

    static uint32_t lastBlink;

    static bool blinkState;


    static void writeColor(
        uint8_t r,
        uint8_t g,
        uint8_t b
    );

};