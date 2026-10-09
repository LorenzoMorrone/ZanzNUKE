#include "Button.h"


constexpr uint32_t DEBOUNCE_MS = 50;


Button::Button(
    uint8_t pin
)
    : pin(pin),
      lastChangeMs(0),
      pressStartMs(0),
      debouncedState(false),
      rawStateLast(false),
      latched(false)
{
}



void Button::begin()
{

    pinMode(
        pin,
        INPUT_PULLUP
    );

    rawStateLast =
        (digitalRead(pin) == LOW);

    debouncedState =
        rawStateLast;

}



bool Button::pressed()
{

    bool raw =
        (digitalRead(pin) == LOW);


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

                pressStartMs = millis();

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



uint32_t Button::heldMs() const
{

    if(!debouncedState)
        return 0;


    return millis() - pressStartMs;

}
