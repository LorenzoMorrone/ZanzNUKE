#include "Outputs.h"

#include "Pins.h"



void Outputs::begin()
{

    pinMode(
        PIN_PERISTALTIC,
        OUTPUT
    );


    pinMode(
        PIN_VALVE,
        OUTPUT
    );


    pinMode(
        PIN_PUMP,
        OUTPUT
    );


    stopAll();
}



void Outputs::relayWrite(
    uint8_t pin,
    bool state
)
{

    bool level;


    if(RELAY_ACTIVE_HIGH)
        level = state;
    else
        level = !state;


    digitalWrite(
        pin,
        level
    );
}



void Outputs::peristalticOn()
{
    relayWrite(
        PIN_PERISTALTIC,
        true
    );
}


void Outputs::peristalticOff()
{
    relayWrite(
        PIN_PERISTALTIC,
        false
    );
}



void Outputs::valveOpen()
{
    relayWrite(
        PIN_VALVE,
        true
    );
}


void Outputs::valveClose()
{
    relayWrite(
        PIN_VALVE,
        false
    );
}



void Outputs::pumpOn()
{
    relayWrite(
        PIN_PUMP,
        true
    );
}


void Outputs::pumpOff()
{
    relayWrite(
        PIN_PUMP,
        false
    );
}



void Outputs::stopAll()
{

    peristalticOff();

    valveClose();

    pumpOff();

}