#pragma once

#include <Arduino.h>


class Clock
{

public:

    static void begin();


    static String datetime();


    static bool valid();


};