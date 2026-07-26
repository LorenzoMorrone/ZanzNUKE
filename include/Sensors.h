#pragma once

#include <Arduino.h>


class Sensors
{

public:

    static void begin();


    static bool tankFull();

    static bool tankEmpty();


};