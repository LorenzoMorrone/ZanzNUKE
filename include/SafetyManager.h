#pragma once

#include <Arduino.h>

#include "Types.h"



class SafetyManager
{

public:


    static void begin();


    static void update();



    static bool hasError();



    static ErrorCode error();



    /*
     * Safe to call from any task: only sets a flag, actually
     * cleared from update() on the main loop task.
     */
    static void requestClear();

    static void notifyError(
        ErrorCode error
    );

    static void onStateChanged(
        IrrigationState newState
    );


    /*
     * Lets a manual diagnostics valve test (WebServer's /test
     * page, run while IrrigationManager is IDLE) tell the
     * unexpected-flow check that flow is intentional right now.
     * Overflow and impossible-float protection stay fully
     * active regardless - only the "valve should be shut"
     * flow check is relaxed while this is set.
     */
    static void setManualFlowAllowed(
        bool allowed
    );



private:


    static ErrorCode currentError;


    static volatile bool pendingClear;



    static uint32_t lastFlowCount;

    static uint32_t lastFlowChange;


    /*
     * Baseline pulse count captured whenever we enter a state
     * where the valve should be fully closed (i.e. anything
     * other than FILLING_TANK, which covers both irrigation and
     * wash fills). Used to catch a stuck-open or leaking valve.
     */
    static uint32_t flowClosedBaseline;

    /*
     * Timestamp the baseline above was last (re)established.
     * Used both for the post-close settling grace period and to
     * periodically re-baseline during a long closed period, so
     * occasional stray pulses (electrical noise, vibration) that
     * arrive minutes apart don't slowly accumulate past the
     * tolerance - only pulses arriving close together (a real
     * leak/stuck valve) can trip it.
     */
    static uint32_t flowClosedBaselineAt;

    static volatile bool manualFlowAllowed;


    static void clear();

    static void trigger(
        ErrorCode error
    );

};