#include "RGBLed.h"

#include <FastLED.h>

#include "Pins.h"



#define NUM_LEDS 1


CRGB leds[NUM_LEDS];


LedMode RGBLed::currentMode =
    LedMode::OFF;


uint32_t RGBLed::lastBlink = 0;

bool RGBLed::blinkState = false;



void RGBLed::begin()
{

    FastLED.addLeds<WS2812, PIN_RGB, GRB>(
        leds,
        NUM_LEDS
    );


    FastLED.setBrightness(
        80
    );


    set(
        LedMode::GREEN
    );

}



void RGBLed::writeColor(
    uint8_t r,
    uint8_t g,
    uint8_t b
)
{

    leds[0] =
        CRGB(
            r,
            g,
            b
        );


    FastLED.show();

}



void RGBLed::set(
    LedMode mode
)
{

    currentMode =
        mode;

}



void RGBLed::update()
{

    switch(currentMode)
    {


    case LedMode::GREEN:

        writeColor(
            0,
            255,
            0
        );

        break;



    case LedMode::BLUE:

        writeColor(
            0,
            0,
            255
        );

        break;



    case LedMode::RED:

        writeColor(
            255,
            0,
            0
        );

        break;



    case LedMode::YELLOW:

        writeColor(
            255,
            150,
            0
        );

        break;



    default:

        writeColor(
            0,
            0,
            0
        );

        break;


    }

}