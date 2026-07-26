#pragma once

#include <Arduino.h>


enum class IrrigationState
{
    IDLE,

    DOSING_FERTILIZER,

    FILLING_TANK,

    IRRIGATING,

    COMPLETE,

    ERROR_STATE
};


enum class ErrorCode
{
    NONE,

    TOP_FLOAT_ACTIVE,

    BOTTOM_FLOAT_ACTIVE,

    FILL_TIMEOUT,

    NO_FLOW,

    UNEXPECTED_FLOW,

    SENSOR_FAILURE,

    CONFIG_INVALID,

    IRRIGATION_TIMEOUT,

    USER_STOP
};


struct SystemStatus
{
    IrrigationState state;

    ErrorCode error;

    float litersFilled;

    uint32_t flowPulses;

    bool floatFull;

    bool floatEmpty;

    uint32_t uptime;
};