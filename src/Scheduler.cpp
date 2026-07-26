#include "Scheduler.h"

#include <time.h>

#include "Config.h"

#include "IrrigationManager.h"

#include "Clock.h"

#include <Preferences.h>



uint32_t Scheduler::lastTriggeredMinute = 0;



SchedulerConfig schedulerConfig;


/*
 * Guards schedulerConfig.entries[] against tearing between the
 * web server task (writes, via setEntry()) and the main loop
 * task (reads every tick in update()).
 */
static portMUX_TYPE scheduleMux = portMUX_INITIALIZER_UNLOCKED;



void Scheduler::begin()
{

    schedulerConfig.load();
    Serial.println(
        "[Scheduler] Loaded schedule configuration"
    );

}



void Scheduler::setEntry(
    uint8_t index,
    const ScheduleEntry &entry
)
{

    if(index >= MAX_SCHEDULES)
        return;


    portENTER_CRITICAL(&scheduleMux);

    schedulerConfig.entries[index] = entry;

    portEXIT_CRITICAL(&scheduleMux);

}



ScheduleEntry Scheduler::getEntry(
    uint8_t index
)
{

    ScheduleEntry copy{};


    if(index >= MAX_SCHEDULES)
        return copy;


    portENTER_CRITICAL(&scheduleMux);

    copy = schedulerConfig.entries[index];

    portEXIT_CRITICAL(&scheduleMux);


    return copy;

}



void Scheduler::saveEntries()
{

    /*
     * NVS/flash I/O can block for a while and must never happen
     * inside a portENTER_CRITICAL section (that disables
     * interrupts / spins a lock and would stall the whole
     * core, plus NVS internals may need to yield). Each
     * individual entry access elsewhere is already made atomic
     * via setEntry()/getEntry(), so a plain read-through save
     * here is safe enough: worst case is persisting one entry
     * that's mid-update by a concurrent request, not torn data.
     */

    schedulerConfig.save();

}



void Scheduler::update()
{


    if(!Clock::valid())
        return;



    time_t now =
        time(nullptr);



    struct tm timeinfo;


    localtime_r(
        &now,
        &timeinfo
    );



    for(
        uint8_t i=0;
        i<MAX_SCHEDULES;
        i++
    )
    {


        ScheduleEntry event =
            getEntry(i);



        if(!event.enabled)
            continue;



        if(
            event.weekday
            !=
            timeinfo.tm_wday
        )
            continue;



        if(
            event.hour
            !=
            timeinfo.tm_hour
        )
            continue;



        if(
            event.minute
            !=
            timeinfo.tm_min
        )
            continue;



        /*
         * Avoid starting twice
         * during same minute
         */

        uint32_t uniqueMinute =
            ((timeinfo.tm_year + 1900) * 1000U)
            +
            (timeinfo.tm_yday * 1440U)
            +
            (timeinfo.tm_hour * 60U)
            +
            timeinfo.tm_min;



        if(
            uniqueMinute
            ==
            lastTriggeredMinute
        )
            continue;



        lastTriggeredMinute =
            uniqueMinute;



        Serial.print(
            "[Scheduler] Triggered irrigation at "
        );
        Serial.print(event.hour);
        Serial.print(":" );
        Serial.println(event.minute);

        IrrigationManager::requestStart();



    }


}



String Scheduler::nextRunDescription()
{

    if(!Clock::valid())
        return "Unknown (clock not synced)";


    time_t now =
        time(nullptr);


    struct tm timeinfo;

    localtime_r(
        &now,
        &timeinfo
    );


    int nowMinutes =
        timeinfo.tm_wday * 1440
        +
        timeinfo.tm_hour * 60
        +
        timeinfo.tm_min;


    int bestDelta = -1;

    ScheduleEntry best{};


    for(
        uint8_t i=0;
        i<MAX_SCHEDULES;
        i++
    )
    {

        ScheduleEntry e =
            getEntry(i);


        if(!e.enabled)
            continue;


        int entryMinutes =
            (e.weekday % 7) * 1440
            +
            (e.hour % 24) * 60
            +
            (e.minute % 60);


        int delta =
            entryMinutes - nowMinutes;


        if(delta <= 0)
            delta += 7 * 1440;


        if(bestDelta < 0 || delta < bestDelta)
        {

            bestDelta = delta;

            best = e;

        }

    }


    if(bestDelta < 0)
        return "Not scheduled";


    static const char* weekdays[] =
    {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };


    char buffer[24];

    snprintf(
        buffer,
        sizeof(buffer),
        "%s %02d:%02d",
        weekdays[best.weekday % 7],
        best.hour % 24,
        best.minute % 60
    );


    return String(buffer);

}

Preferences schedulerPrefs;



bool SchedulerConfig::load()
{

    schedulerPrefs.begin(
        "schedule",
        true
    );


    for(
        uint8_t i = 0;
        i < MAX_SCHEDULES;
        i++
    )
    {

        String key =
            "e" + String(i);



        uint32_t value =
            schedulerPrefs.getUInt(
                key.c_str(),
                0
            );



        entries[i].enabled =
            value & 0x01;



        entries[i].weekday =
            (value >> 1)
            &
            0x07;



        entries[i].hour =
            (value >> 4)
            &
            0x1F;



        entries[i].minute =
            (value >> 9)
            &
            0x3F;

    }


    schedulerPrefs.end();


    return true;

}





bool SchedulerConfig::save()
{

    schedulerPrefs.begin(
        "schedule",
        false
    );


    for(
        uint8_t i = 0;
        i < MAX_SCHEDULES;
        i++
    )
    {


        uint32_t value = 0;



        value |=
            entries[i].enabled;



        value |=
            (entries[i].weekday & 0x07)
            << 1;



        value |=
            (entries[i].hour & 0x1F)
            << 4;



        value |=
            (entries[i].minute & 0x3F)
            << 9;



        schedulerPrefs.putUInt(
            ("e"+String(i)).c_str(),
            value
        );


    }



    schedulerPrefs.end();


    return true;

}





void SchedulerConfig::reset()
{

    for(
        uint8_t i=0;
        i<MAX_SCHEDULES;
        i++
    )
    {

        entries[i].enabled=false;

        entries[i].weekday=0;

        entries[i].hour=0;

        entries[i].minute=0;

    }


    save();

}