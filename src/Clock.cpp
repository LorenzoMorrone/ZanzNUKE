#include "Clock.h"

#include <time.h>



void Clock::begin()
{

    configTime(
        3600,
        3600,
        "pool.ntp.org",
        "time.nist.gov"
    );

}



void Clock::resync()
{

    /*
     * Calling configTime() again forces the SNTP client to fire
     * an immediate sync attempt rather than waiting for its normal
     * background poll interval - important right after reconnecting,
     * since otherwise the clock could sit un-resynced (running on
     * free-running drift alone) for up to an hour.
     */

    configTime(
        3600,
        3600,
        "pool.ntp.org",
        "time.nist.gov"
    );

}



bool Clock::valid()
{

    time_t now =
        time(nullptr);


    return now > 1700000000;

}



String Clock::datetime()
{

    if(!valid())
        return "No time";


    time_t now =
        time(nullptr);



    struct tm timeinfo;


    localtime_r(
        &now,
        &timeinfo
    );


    char buffer[32];


    sprintf(
        buffer,
        "%04d-%02d-%02d %02d:%02d:%02d",
        timeinfo.tm_year + 1900,
        timeinfo.tm_mon + 1,
        timeinfo.tm_mday,
        timeinfo.tm_hour,
        timeinfo.tm_min,
        timeinfo.tm_sec
    );


    return String(buffer);

}