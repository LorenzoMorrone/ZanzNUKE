#pragma once

#include <Arduino.h>


class WebServerManager
{

public:

    static void begin();

    /*
     * Must be called every loop() iteration. Turns off manual
     * diagnostic relay tests once their timer expires and
     * starts newly requested ones - see /test, /relay/* routes.
     * Keeps all Outputs::* access on the main loop task.
     */
    static void update();


private:

    static void setupRoutes();

};