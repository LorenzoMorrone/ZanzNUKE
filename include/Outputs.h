#pragma once

#include <Arduino.h>


class Outputs
{

public:

    static void begin();


    static void peristalticOn();

    static void peristalticOff();


    static void valveOpen();

    static void valveClose();


    static void pumpOn();

    static void pumpOff();


    static void stopAll();


private:

    static void relayWrite(
        uint8_t pin,
        bool state
    );

};