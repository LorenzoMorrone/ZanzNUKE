#include "Sensors.h"

#include "Pins.h"



void Sensors::begin()
{

    pinMode(
        PIN_FLOAT_FULL,
        INPUT_PULLUP
    );


    pinMode(
        PIN_FLOAT_EMPTY,
        INPUT_PULLUP
    );

}



/*
 * True when the given float switch is CLOSED (engaged) - i.e.
 * the physical condition that pulls the switch's contact shut,
 * regardless of what that means for either sensor's meaning.
 * Both floats are wired identically (GPIO---switch---GND, see
 * Pins.h), so this one wire-level helper is shared by both.
 */
static bool floatSwitchClosed(uint8_t pin)
{

    bool raw =
        digitalRead(pin)
        == LOW;

    return FLOAT_ACTIVE_LOW ? raw : !raw;

}



bool Sensors::tankFull()
{

    /*
     * Top float: closed means the rising water has lifted it and
     * engaged the switch - the tank is full.
     */

    return floatSwitchClosed(PIN_FLOAT_FULL);

}



bool Sensors::tankEmpty()
{

    /*
     * Bottom float: closed means water is present at or above
     * the minimum level - the OPPOSITE meaning from the top
     * float. It only opens once the level drops below the float,
     * which is what "empty" (for pump dry-run protection)
     * actually means here.
     */

    return !floatSwitchClosed(PIN_FLOAT_EMPTY);

}