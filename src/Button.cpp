#include "Button.h"

#include "Pins.h"



constexpr uint32_t DEBOUNCE_MS = 50;


uint32_t Button::lastChangeMs = 0;

bool Button::debouncedState = false;

bool Button::rawStateLast = false;

bool Button::latched = false;



void Button::begin()
{

    pinMode(
        PIN_BUTTON,
        INPUT_PULLUP
    );

    rawStateLast =
        (digitalRead(PIN_BUTTON) == LOW);

    debouncedState =
        rawStateLast;

}



bool Button::pressed()
{

    bool raw =
        (digitalRead(PIN_BUTTON) == LOW);


    if(raw != rawStateLast)
    {

        rawStateLast = raw;

        lastChangeMs = millis();

    }



    if(
        (millis() - lastChangeMs)
        >=
        DEBOUNCE_MS
    )
    {

        if(raw != debouncedState)
        {

            debouncedState = raw;


            /*
             * Fire only on the debounced
             * released -> pressed edge.
             */

            if(debouncedState && !latched)
            {

                latched = true;

                return true;

            }


            if(!debouncedState)
            {

                latched = false;

            }

        }

    }


    return false;

}