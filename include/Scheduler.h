#pragma once

#include <Arduino.h>


constexpr uint8_t MAX_SCHEDULES = 21;



struct ScheduleEntry
{

    bool enabled;

    /*
     * 0 = Sunday
     * 1 = Monday
     *
     * 6 = Saturday
     */
    uint8_t weekday;


    uint8_t hour;

    uint8_t minute;


};



struct SchedulerConfig
{

    ScheduleEntry entries[MAX_SCHEDULES];


    bool load();

    bool save();


    void reset();

};



class Scheduler
{

public:


    static void begin();


    static void update();


    /*
     * Thread-safe accessors for schedulerConfig.entries[].
     *
     * The web server's /schedule/save handler runs on a
     * different FreeRTOS task than loop() (where Scheduler::
     * update() reads the same array every tick). These wrap the
     * access in a short critical section so a handler can never
     * observe/leave a torn (partially written) entry.
     */
    static void setEntry(
        uint8_t index,
        const ScheduleEntry &entry
    );

    static ScheduleEntry getEntry(
        uint8_t index
    );


    /*
     * Persists the current entries[] (thread-safe) and returns
     * a human-readable description of the next enabled run, or
     * "Not scheduled" if none are enabled / time isn't synced.
     */
    static void saveEntries();

    static String nextRunDescription();



private:


    static uint32_t lastTriggeredMinute;


};

extern SchedulerConfig schedulerConfig;