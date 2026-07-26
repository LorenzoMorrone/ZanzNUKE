#include "SafetyManager.h"


#include "Outputs.h"
#include "Sensors.h"
#include "FlowMeter.h"
#include "IrrigationManager.h"
#include "RGBLed.h"
#include "Config.h"


/*
 * Flow/leak detection tuning below all comes from config (see
 * Config.h's "Advanced: flow/leak detection tuning" fields and
 * the config page's Advanced section) rather than being fixed
 * at compile time, since these directly control how sensitive
 * leak/stuck-valve detection is:
 *
 *   config.flowLeakTolerancePulses      - see FLOW_CLOSED_TOLERANCE_PULSES's old doc
 *   config.flowLeakGraceSeconds         - see FLOW_CLOSED_GRACE_MS's old doc
 *   config.flowLeakRebaselineSeconds    - see FLOW_CLOSED_REBASELINE_MS's old doc
 *   config.flowStallTimeoutSeconds      - see FLOW_STALL_TIMEOUT_MS's old doc
 */


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


static bool flowIsExpected(IrrigationState state)
{
    /*
     * Wash reuses FILLING_TANK (see IrrigationManager::
     * doStartWash()) rather than having its own state, so this
     * one check already covers both irrigation and wash fills.
     */
    return state == IrrigationState::FILLING_TANK;
}


ErrorCode SafetyManager::currentError =
    ErrorCode::NONE;


volatile bool SafetyManager::pendingClear = false;


uint32_t SafetyManager::lastFlowCount = 0;

uint32_t SafetyManager::lastFlowChange = 0;


uint32_t SafetyManager::flowClosedBaseline = 0;

uint32_t SafetyManager::flowClosedBaselineAt = 0;

volatile bool SafetyManager::manualFlowAllowed = false;



void SafetyManager::begin()
{

    currentError =
        ErrorCode::NONE;

    pendingClear = false;

    lastFlowCount = 0;

    lastFlowChange = 0;

    flowClosedBaseline =
        FlowMeter::pulses();

    flowClosedBaselineAt =
        millis();

    Serial.println(
        "[Safety] Manager initialized"
    );


    /*
     * Catch a corrupted/blank NVS config (or a bad manual
     * value) before it can cause a divide-by-zero or a
     * runaway timer once irrigation starts.
     */

    if(!config.valid())
    {

        Serial.println(
            "[Safety] Triggered: invalid configuration at boot"
        );

        trigger(
            ErrorCode::CONFIG_INVALID
        );

    }

}



void SafetyManager::update()
{

    if(pendingClear)
    {

        pendingClear = false;

        clear();

    }


    /*
     * A fault is already latched: nothing below adds any new
     * information (the cause is already recorded and outputs are
     * already stopped), and re-evaluating it every single loop()
     * iteration would otherwise re-log the same "Triggered: ..."
     * line at full loop speed until the fault is cleared.
     */

    if(hasError())
        return;



    IrrigationState state =
        IrrigationManager::state();



    /*
     * Impossible float condition
     *
     * Both floats can never be legitimately active at once:
     * the empty float sits below the full float. This means a
     * wiring fault, a stuck switch, or a disconnected sensor.
     */

    if(
        Sensors::tankFull()
        &&
        Sensors::tankEmpty()
    )
    {

        Serial.println(
            "[Safety] Triggered: impossible float state"
        );

        trigger(
            ErrorCode::SENSOR_FAILURE
        );

        return;

    }



    /*
     * Tank overflow protection - unconditional emergency stop.
     *
     * The top float closing is NEVER legitimate, in any state:
     * normal fills stop at a configured liters target that
     * should always sit comfortably below the physical float
     * height, so this should only ever close if something is
     * genuinely wrong (valve stuck open during IDLE, a sensor
     * glitch during DOSING_FERTILIZER, mains water pressure
     * pushing water in during IRRIGATING, etc.) - deliberately
     * NOT scoped to FILLING_TANK, so it stops everything and
     * notifies regardless of what the system is doing at the
     * moment it closes. Independent from (and redundant with)
     * IrrigationManager's own FILLING_TANK-only check: this is
     * the last line of defense, so it stays even if that guard
     * were ever changed.
     */

    if(Sensors::tankFull())
    {

        Serial.println(
            "[Safety] Triggered: top float active"
        );

        trigger(
            ErrorCode::TOP_FLOAT_ACTIVE
        );

        return;

    }


    /*
     * NOTE: tankEmpty() during IRRIGATING is deliberately NOT
     * treated as an error here. It is the normal, expected
     * signal that the pump has finished emptying the tank and
     * IrrigationManager already handles it as a successful
     * completion. Flagging it here as BOTTOM_FLOAT_ACTIVE would
     * raise a false alarm (and latch a red/error state) on
     * every single successful irrigation cycle.
     */



    /*
     * Flow sensor stall check
     *
     * Valve is deliberately open (filling or washing) but no
     * pulses have been seen for config.flowStallTimeoutSeconds:
     * the mains supply may be off, the valve may have failed to
     * open, or the flow sensor may be dead/miswired.
     */

    if(flowIsExpected(state))
    {


        uint32_t pulses =
            FlowMeter::pulses();



        if(
            pulses != lastFlowCount
        )
        {

            lastFlowCount =
                pulses;


            lastFlowChange =
                millis();

        }



        if(
            millis()
            -
            lastFlowChange
            >
            config.flowStallTimeoutSeconds * 1000UL
        )
        {

            Serial.println(
                "[Safety] Triggered: no flow detected"
            );

            trigger(
                ErrorCode::NO_FLOW
            );

            return;

        }

    }



    /*
     * Unexpected flow check
     *
     * The valve is supposed to be fully closed (any state other
     * than FILLING_TANK, which covers both irrigation and wash
     * fills) but the flow meter keeps counting pulses well
     * beyond the settle tolerance: the valve is stuck open or
     * leaking.
     */

    if(
        !flowIsExpected(state)
        &&
        !manualFlowAllowed
    )
    {

        uint32_t now =
            millis();


        if(
            now - flowClosedBaselineAt
            <
            config.flowLeakGraceSeconds * 1000UL
        )
        {

            /*
             * Still settling right after the valve closed:
             * residual pressure/momentum can keep the meter
             * spinning briefly. Keep sliding the baseline so
             * none of this counts once the grace period ends.
             */

            flowClosedBaseline =
                FlowMeter::pulses();

        }
        else
        {

            uint32_t pulses =
                FlowMeter::pulses();


            if(
                pulses - flowClosedBaseline
                >
                config.flowLeakTolerancePulses
            )
            {

                Serial.println(
                    "[Safety] Triggered: unexpected flow with valve closed"
                );

                trigger(
                    ErrorCode::UNEXPECTED_FLOW
                );

                return;

            }


            if(
                now - flowClosedBaselineAt
                >
                config.flowLeakRebaselineSeconds * 1000UL
            )
            {

                /*
                 * Comfortably under tolerance for a full window:
                 * slide the baseline forward so an isolated
                 * stray pulse doesn't sit there adding up
                 * against the next one that arrives an hour
                 * later. A real leak keeps generating pulses
                 * fast enough to cross the tolerance within a
                 * single window instead.
                 */

                flowClosedBaseline = pulses;

                flowClosedBaselineAt = now;

            }

        }

    }


}



void SafetyManager::trigger(
    ErrorCode error
)
{

    /*
     * Route through IrrigationManager so the state machine
     * itself moves to ERROR_STATE (see externalTrip() doc).
     * This in turn calls back into notifyError() below, which
     * latches SafetyManager's own error flag and sets the LED.
     */

    IrrigationManager::externalTrip(error);

}



void SafetyManager::notifyError(
    ErrorCode error
)
{

    if(
        currentError
        !=
        ErrorCode::NONE
    )
        return;



    currentError =
        error;



    Outputs::stopAll();

    Serial.print(
        "[Safety] Fault raised: "
    );
    Serial.println(
        errorName(error)
    );

    RGBLed::set(
        LedMode::RED
    );


}



void SafetyManager::onStateChanged(
    IrrigationState newState
)
{

    if(flowIsExpected(newState))
    {

        /*
         * Entering a state where the valve is opened: (re)start
         * the stall-detection clock from now, and treat "no
         * pulses yet" as normal until the timeout.
         */

        lastFlowCount =
            FlowMeter::pulses();

        lastFlowChange =
            millis();

    }
    else
    {

        /*
         * Entering a state where the valve should be closed:
         * re-baseline the unexpected-flow tolerance from the
         * current pulse count, so pulses already accumulated
         * during the previous open-valve state don't
         * immediately look like a leak. This also (re)starts
         * the settling grace period.
         */

        flowClosedBaseline =
            FlowMeter::pulses();

        flowClosedBaselineAt =
            millis();

    }

}



bool SafetyManager::hasError()
{
    return error() != ErrorCode::NONE;
}



ErrorCode SafetyManager::error()
{
    if(
        currentError
        !=
        ErrorCode::NONE
    )
        return currentError;

    return IrrigationManager::error();
}



void SafetyManager::requestClear()
{
    pendingClear = true;
}



void SafetyManager::setManualFlowAllowed(
    bool allowed
)
{

    manualFlowAllowed = allowed;


    if(!allowed)
    {

        /*
         * Test just ended: re-baseline (and restart the settling
         * grace period) so pulses generated during the test
         * don't immediately read as a leak once the override
         * lifts.
         */

        flowClosedBaseline =
            FlowMeter::pulses();

        flowClosedBaselineAt =
            millis();

    }

}



void SafetyManager::clear()
{

    currentError =
        ErrorCode::NONE;

    lastFlowCount = 0;

    lastFlowChange = 0;

    /*
     * Re-baseline from the CURRENT pulse count (and restart the
     * settling grace period). Without this, pulses accumulated
     * during whatever open-valve state was active when the fault
     * hit would make the very next unexpected-flow check fire
     * immediately after clearing.
     */

    flowClosedBaseline =
        FlowMeter::pulses();

    flowClosedBaselineAt =
        millis();

    IrrigationManager::begin();

    Serial.println(
        "[Safety] Fault cleared"
    );


    RGBLed::set(
        LedMode::GREEN
    );

}