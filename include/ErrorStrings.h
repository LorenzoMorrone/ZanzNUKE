#pragma once

#include "Types.h"



inline String errorToString(
    ErrorCode error
)
{

switch(error)
{

case ErrorCode::NONE:
return "None";


case ErrorCode::TOP_FLOAT_ACTIVE:
return "Tank overflow";


case ErrorCode::BOTTOM_FLOAT_ACTIVE:
return "Tank empty";


case ErrorCode::FILL_TIMEOUT:
return "Valve timeout - not enough water reached";


case ErrorCode::NO_FLOW:
return "No water flow detected";


case ErrorCode::UNEXPECTED_FLOW:
return "Unexpected flow with valve closed (possible leak/stuck valve)";


case ErrorCode::SENSOR_FAILURE:
return "Sensor failure (impossible float state)";


case ErrorCode::CONFIG_INVALID:
return "Invalid configuration";


case ErrorCode::IRRIGATION_TIMEOUT:
return "Irrigation pump ran too long without emptying the tank";


case ErrorCode::USER_STOP:
return "Stopped by user";


default:
return "Unknown";

}


}



/*
 * isWash: wash cycles share FILLING_TANK/IRRIGATING with normal
 * irrigation (see IrrigationManager::doStartWash()) - this only
 * affects which label those two states get, not the state
 * machine itself.
 */
inline String stateToString(
    IrrigationState state,
    bool isWash = false
)
{

switch(state)
{

case IrrigationState::IDLE:
return "Idle";

case IrrigationState::DOSING_FERTILIZER:
return "Dosing concentrate";

case IrrigationState::FILLING_TANK:
return isWash ? "Filling tank (wash)" : "Filling tank";

case IrrigationState::IRRIGATING:
return isWash ? "Washing system" : "Watering";

case IrrigationState::COMPLETE:
return "Cycle complete";

case IrrigationState::ERROR_STATE:
return "ERROR";

default:
return "Unknown";

}


}