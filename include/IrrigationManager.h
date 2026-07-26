#pragma once

#include <Arduino.h>

#include "Types.h"


class IrrigationManager
{

public:

    static void begin();


    static void update();


    /*
     * User commands.
     *
     * These are safe to call from ANY task (e.g. the
     * ESPAsyncWebServer callback task, which runs on a
     * different FreeRTOS task/core than loop()). They only
     * set a flag; the actual state-machine mutation happens
     * exclusively inside update(), which always runs on the
     * main loop task. This avoids torn/interleaved reads of
     * currentState / stateStartTime across tasks.
     */
    static void requestStart();

    static void requestWash();

    static void requestStop();


    /*
     * Called by SafetyManager when it detects a fault on its
     * own (e.g. NO_FLOW, UNEXPECTED_FLOW, an impossible float
     * combination). This forces the state machine itself into
     * ERROR_STATE, not just SafetyManager's own error flag -
     * otherwise processState() would keep running the current
     * state's logic (with relays nominally stopped, but able to
     * re-energize them on the next state transition) even
     * though a fault has been raised.
     */
    static void externalTrip(
        ErrorCode error
    );


    /*
     * Status
     */
    static IrrigationState state();

    static ErrorCode error();


    /*
     * Target liters for the in-progress operation
     * (irrigation or wash). Used by the status API.
     */
    static float targetLitersValue();


    /*
     * True while the in-progress (or most recently completed)
     * cycle is a wash rather than a fertilized irrigation. Wash
     * shares FILLING_TANK/IRRIGATING with normal irrigation
     * (fill, then pump/spray) - it only skips DOSING_FERTILIZER
     * and uses washLiters as the target - so this flag is what
     * lets the status API and logs tell the two apart.
     */
    static bool isWashCycle();



private:

    static IrrigationState currentState;

    static ErrorCode currentError;


    static uint32_t stateStartTime;


    static float targetLiters;


    static uint32_t fertilizerTimeMs;


    static bool washCycle;


    /*
     * Command queue, written from any task, only ever
     * read/cleared from update() on the main loop task.
     */
    static volatile bool pendingStart;

    static volatile bool pendingWash;

    static volatile bool pendingStop;


    static void processPendingCommands();


    static void doStartIrrigation();

    static void doStartWash();

    static void doStop();



    static void changeState(
        IrrigationState newState
    );


    static void enterState();


    static void processState();



    static void setError(
        ErrorCode error
    );


};