#pragma once

#include <Arduino.h>



class Pushover
{

public:


    static void begin();


    static bool send(
        String title,
        String message
    );



};