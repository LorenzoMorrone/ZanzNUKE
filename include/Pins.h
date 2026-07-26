#pragma once

#include <Arduino.h>

/*
 * Hardware pin mapping
 * XIAO ESP32-S3
 */

// Outputs (relay inputs)
constexpr uint8_t PIN_PERISTALTIC = 1;
constexpr uint8_t PIN_VALVE       = 2;
constexpr uint8_t PIN_PUMP        = 3;

// Inputs
constexpr uint8_t PIN_FLOAT_FULL  = 4;
constexpr uint8_t PIN_FLOAT_EMPTY = 5;

constexpr uint8_t PIN_FLOW        = 6;

// User-facing fault reset button
constexpr uint8_t PIN_FAULT_RESET = 7;

// Start button (user-facing)
constexpr uint8_t PIN_BUTTON      = 8;

// RGB LED
constexpr uint8_t PIN_RGB         = 9;


/*
 * Relay configuration
 *
 * Change this to false if your relay board
 * is active LOW.
 */
constexpr bool RELAY_ACTIVE_HIGH = true;


/*
 * Float switches
 *
 * Both floats are wired the same way:
 *
 * GPIO ---- switch ---- GND
 *
 * therefore, at the wire level:
 *
 * LOW  = switch closed
 * HIGH = switch open
 *
 * This describes the WIRING only. What "closed" MEANS is
 * different for each float - see Sensors.cpp:
 *   - top (full) float:    closed = tank full
 *   - bottom (empty) float: closed = water present (not empty),
 *                            open = below minimum (empty)
 */
constexpr bool FLOAT_ACTIVE_LOW = true;