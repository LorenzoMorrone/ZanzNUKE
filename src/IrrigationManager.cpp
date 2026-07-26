#include "IrrigationManager.h"

#include "Outputs.h"
#include "Sensors.h"
#include "FlowMeter.h"
#include "Config.h"
#include "SafetyManager.h"


static const char* stateName(IrrigationState state)
{
    switch(state)
    {
    case IrrigationState::IDLE: return "IDLE";
    case IrrigationState::DOSING_FERTILIZER: return "DOSING_FERTILIZER";
    case IrrigationState::FILLING_TANK: return "FILLING_TANK";
    case IrrigationState::IRRIGATING: return "IRRIGATING";
    case IrrigationState::COMPLETE: return "COMPLETE";
    case IrrigationState::ERROR_STATE: return "ERROR_STATE";
    }
    return "UNKNOWN";
}


static const char* errorName(ErrorCode error)
{
    switch(error)
    {
    case ErrorCode::NONE: return "NONE";
    case ErrorCode::TOP_FLOAT_ACTIVE: return "TOP_FLOAT_ACTIVE";
    case ErrorCode::BOTTOM_FLOAT_ACTIVE: return "BOTTOM_FLOAT_ACTIVE";
    case ErrorCode::FILL_TIMEOUT: return "FILL_TIMEOUT";
    case ErrorCode::NO_FLOW: return "NO_FLOW";
    case ErrorCode::UNEXPECTED_FLOW: return "UNEXPECTED_FLOW";
    case ErrorCode::SENSOR_FAILURE: return "SENSOR_FAILURE";
    case ErrorCode::CONFIG_INVALID: return "CONFIG_INVALID";
    case ErrorCode::IRRIGATION_TIMEOUT: return "IRRIGATION_TIMEOUT";
    case ErrorCode::USER_STOP: return "USER_STOP";
    }
    return "UNKNOWN";
}


IrrigationState IrrigationManager::currentState =
    IrrigationState::IDLE;


ErrorCode IrrigationManager::currentError =
    ErrorCode::NONE;



uint32_t IrrigationManager::stateStartTime = 0;


float IrrigationManager::targetLiters = 0;


uint32_t IrrigationManager::fertilizerTimeMs = 0;


bool IrrigationManager::washCycle = false;


volatile bool IrrigationManager::pendingStart = false;

volatile bool IrrigationManager::pendingWash = false;

volatile bool IrrigationManager::pendingStop = false;



void IrrigationManager::begin()
{

    Outputs::stopAll();

    currentError =
        ErrorCode::NONE;

    currentState =
        IrrigationState::IDLE;

    stateStartTime = 0;

    targetLiters = 0;

    fertilizerTimeMs = 0;

    washCycle = false;

    pendingStart = false;

    pendingWash = false;

    pendingStop = false;

    Serial.println(
        "[Irrigation] State reset to IDLE"
    );

}



void IrrigationManager::changeState(
    IrrigationState newState
)
{

    Outputs::stopAll();


    Serial.print(
        "[Irrigation] State -> "
    );
    Serial.println(
        stateName(newState)
    );

    currentState = newState;


    stateStartTime = millis();


    SafetyManager::onStateChanged(newState);


    enterState();

}



void IrrigationManager::enterState()
{

    switch(currentState)
    {


    case IrrigationState::DOSING_FERTILIZER:

        /*
         * No FlowMeter::reset() here: dosing is time-based, not
         * flow-based, and resetting the pulse counter here would
         * make SafetyManager's "unexpected flow while the valve
         * should be closed" baseline go stale immediately
         * (the counter would drop below the just-captured
         * baseline, underflowing the unsigned comparison).
         */

        Outputs::peristalticOn();

        break;



    case IrrigationState::FILLING_TANK:

        FlowMeter::reset();


        Outputs::valveOpen();

        break;



    case IrrigationState::IRRIGATING:


        Outputs::pumpOn();

        break;



    case IrrigationState::ERROR_STATE:


        Outputs::stopAll();

        break;



    default:

        break;

    }

}



void IrrigationManager::update()
{

    processPendingCommands();

    processState();

}



void IrrigationManager::processPendingCommands()
{

    /*
     * Stop always wins and is handled first, regardless
     * of what else was requested in the same tick.
     */

    if(pendingStop)
    {

        pendingStop = false;

        pendingStart = false;

        pendingWash = false;

        doStop();

        return;

    }


    if(pendingStart)
    {

        pendingStart = false;

        doStartIrrigation();

    }


    if(pendingWash)
    {

        pendingWash = false;

        doStartWash();

    }

}



void IrrigationManager::requestStart()
{
    pendingStart = true;
}



void IrrigationManager::requestWash()
{
    pendingWash = true;
}



void IrrigationManager::requestStop()
{
    pendingStop = true;
}



void IrrigationManager::processState()
{

    uint32_t elapsed =
        millis() - stateStartTime;



    switch(currentState)
    {


    /*
     * --------------------
     * IDLE
     * --------------------
     */

    case IrrigationState::IDLE:

        break;



    /*
     * --------------------
     * Fertilizer dosing
     * --------------------
     */

    case IrrigationState::DOSING_FERTILIZER:


        if(elapsed >= fertilizerTimeMs)
        {

            Outputs::peristalticOff();


            Serial.print(
                "[Irrigation] Fertilizer complete, moving to "
            );
            Serial.println(
                stateName(IrrigationState::FILLING_TANK)
            );

            changeState(
                IrrigationState::FILLING_TANK
            );

        }

        break;



    /*
     * --------------------
     * Filling tank
     * --------------------
     */

    case IrrigationState::FILLING_TANK:
    {


        /*
         * Defensive: config.valid() is checked before we ever
         * enter this state, but guard the division anyway so a
         * config change mid-cycle can never produce NaN/Inf.
         */
        float liters =
            (config.pulsesPerLiter > 0.0f)
            ?
            (FlowMeter::pulses() / config.pulsesPerLiter)
            :
            0.0f;



        if(Sensors::tankFull())
        {

            setError(
                ErrorCode::TOP_FLOAT_ACTIVE
            );

            break;

        }



        if(liters >= targetLiters)
        {

            Outputs::valveClose();


            /*
             * The flow meter says the target volume went in, but
             * the bottom float switch still doesn't confirm water
             * is actually present at the minimum level. That's a
             * contradiction (miscalibrated pulsesPerLiter,
             * blocked plumbing, a stuck float...) and starting
             * the pump on an unconfirmed tank is exactly the
             * dry-run risk this float exists to prevent. Refuse
             * rather than start the pump and immediately stop it.
             */

            if(Sensors::tankEmpty())
            {

                Serial.println(
                    "[Irrigation] Fill target reached but bottom "
                    "float still reads empty - refusing to start pump"
                );

                setError(
                    ErrorCode::BOTTOM_FLOAT_ACTIVE
                );

                break;

            }


            Serial.print(
                "[Irrigation] Tank filled, moving to "
            );
            Serial.println(
                stateName(IrrigationState::IRRIGATING)
            );

            changeState(
                IrrigationState::IRRIGATING
            );

            break;

        }



        if(
            elapsed >
            config.valveTimeoutSeconds * 1000UL
        )
        {

            setError(
                ErrorCode::FILL_TIMEOUT
            );

        }


        break;

    }



    /*
     * --------------------
     * Irrigation pump
     * --------------------
     */

    case IrrigationState::IRRIGATING:


        if(Sensors::tankEmpty())
        {

            Outputs::pumpOff();


            changeState(
                IrrigationState::COMPLETE
            );

            break;

        }



        if(
            elapsed >
            config.irrigationTimeoutSeconds
            *
            1000UL
        )
        {

            /*
             * Pump has been running far longer than the liters
             * target should ever take and the tank still isn't
             * reporting empty. Either the pump/plumbing failed,
             * or the empty float is stuck/miswired. Stop rather
             * than risk running the pump dry indefinitely.
             */

            setError(
                ErrorCode::IRRIGATION_TIMEOUT
            );

        }

        break;



    case IrrigationState::COMPLETE:


        Outputs::stopAll();


        Serial.print(
            "[Irrigation] Cycle complete, moving to "
        );
        Serial.println(
            stateName(IrrigationState::IDLE)
        );

        changeState(
            IrrigationState::IDLE
        );

        break;



    case IrrigationState::ERROR_STATE:

        /*
         * Waiting for user reset
         */

        break;


    }

}




void IrrigationManager::doStartIrrigation()
{

    if(
        currentState !=
        IrrigationState::IDLE
    )
        return;



    /*
     * Config sanity check.
     *
     * config.pulsesPerLiter <= 0 (or other bad values) would
     * turn the liters computation into a divide-by-zero /
     * NaN comparison further down the state machine, which
     * can behave unpredictably. Refuse to start instead.
     */

    if(!config.valid())
    {

        Serial.println(
            "[Irrigation] Start blocked: invalid configuration"
        );

        setError(
            ErrorCode::CONFIG_INVALID
        );

        return;

    }



    /*
     * Safety check.
     *
     * An EMPTY tank is the normal, expected state before a
     * fill cycle (that's the whole point of FILLING_TANK) so
     * it must NOT block starting. What we actually want to
     * refuse is starting a fresh dose+fill cycle when the tank
     * is already FULL: that risks overflowing it (the valve
     * hasn't even opened yet) and over-dosing fertilizer into
     * water that's about to be immediately pumped back out
     * without ever being topped up.
     */

    if(Sensors::tankFull())
    {

        Serial.println(
            "[Irrigation] Start blocked: tank already full"
        );

        setError(
            ErrorCode::TOP_FLOAT_ACTIVE
        );

        return;

    }



    washCycle = false;

    targetLiters =
        config.irrigationLiters;



    fertilizerTimeMs =
        (uint32_t)(
            targetLiters
            *
            config.fertilizerSecondsPerLiter
            *
            1000.0f
        );



    Serial.print(
        "[Irrigation] Starting irrigation cycle with target "
    );
    Serial.print(targetLiters);
    Serial.println(" L");

    changeState(
        IrrigationState::DOSING_FERTILIZER
    );

}



void IrrigationManager::doStartWash()
{

    if(
        currentState !=
        IrrigationState::IDLE
    )
        return;


    if(!config.valid())
    {

        Serial.println(
            "[Irrigation] Wash blocked: invalid configuration"
        );

        setError(
            ErrorCode::CONFIG_INVALID
        );

        return;

    }


    if(Sensors::tankFull())
    {

        Serial.println(
            "[Irrigation] Wash blocked: tank already full"
        );

        setError(
            ErrorCode::TOP_FLOAT_ACTIVE
        );

        return;

    }


    /*
     * Wash shares the normal FILLING_TANK -> IRRIGATING path
     * (fill the tank, then pump/spray it back out through the
     * actual irrigation lines) so it rinses the lines and
     * nozzles, not just the tank - it just skips
     * DOSING_FERTILIZER and uses washLiters as the target.
     */

    washCycle = true;

    targetLiters =
        config.washLiters;


    fertilizerTimeMs = 0;



    Serial.print(
        "[Irrigation] Starting wash cycle with target "
    );
    Serial.print(config.washLiters);
    Serial.println(" L");

    changeState(
        IrrigationState::FILLING_TANK
    );

}



void IrrigationManager::doStop()
{

    if(
        currentState ==
        IrrigationState::IDLE
    )
        return;


    setError(
        ErrorCode::USER_STOP
    );

}



float IrrigationManager::targetLitersValue()
{
    return targetLiters;
}



bool IrrigationManager::isWashCycle()
{
    return washCycle;
}



uint32_t IrrigationManager::stateElapsedMs()
{
    return millis() - stateStartTime;
}



void IrrigationManager::externalTrip(
    ErrorCode error
)
{
    setError(error);
}



void IrrigationManager::setError(
    ErrorCode error
)
{

    /*
     * Latch: once we're in ERROR_STATE, further calls (e.g. a
     * secondary symptom noticed by SafetyManager while we're
     * already stopped) must NOT overwrite the original cause.
     * Outputs are already stopped and staying stopped; this
     * guard only protects which error code is reported.
     */

    if(
        currentState == IrrigationState::ERROR_STATE
        &&
        currentError != ErrorCode::NONE
    )
        return;


    Outputs::stopAll();


    currentError =
        error;


    currentState =
        IrrigationState::ERROR_STATE;

    Serial.print(
        "[Irrigation] Error: "
    );
    Serial.println(
        errorName(error)
    );


    SafetyManager::notifyError(error);


}



IrrigationState IrrigationManager::state()
{
    return currentState;
}



ErrorCode IrrigationManager::error()
{
    return currentError;
}