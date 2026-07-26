#include "Config.h"

#include <Preferences.h>
#include <cmath>


Preferences preferences;


IrrigationConfig config;



bool IrrigationConfig::load()
{

    preferences.begin(
        "irrigation",
        true
    );


    pulsesPerLiter =
        preferences.getFloat(
            "pulses",
            450.0
        );


    irrigationLiters =
        preferences.getFloat(
            "liters",
            20.0
        );


    washLiters =
        preferences.getFloat(
            "wash",
            10.0
        );


    fertilizerSecondsPerLiter =
        preferences.getFloat(
            "fertsec",
            5.0
        );


    valveTimeoutSeconds =
        preferences.getUInt(
            "valveto",
            300
        );


    irrigationTimeoutSeconds =
        preferences.getUInt(
            "irrigto",
            600
        );


    wifiSSID =
        preferences.getString(
            "ssid",
            ""
        );


    wifiPassword =
        preferences.getString(
            "pass",
            ""
        );


    pushoverToken =
        preferences.getString(
            "ptoken",
            ""
        );


    pushoverUser =
        preferences.getString(
            "puser",
            ""
        );


    preferences.end();

    /*
     * NOTE: schedule entries are NOT owned here.
     * Scheduler.cpp / SchedulerConfig owns the "schedule"
     * NVS namespace exclusively to avoid two independent
     * copies of the same data drifting apart.
     */


    /*
     * Self-heal: replace any corrupted numeric field (NaN/Inf,
     * e.g. from a flash write interrupted by power loss) with
     * its safe default and persist the repair immediately.
     * Matters because these values feed the irrigation timers
     * and the liters calculation - see valid() for why a NaN
     * is especially dangerous there.
     */

    bool repaired = false;

    if(isnan(pulsesPerLiter) || isinf(pulsesPerLiter))
    {
        pulsesPerLiter = 450.0f;
        repaired = true;
    }

    if(isnan(irrigationLiters) || isinf(irrigationLiters))
    {
        irrigationLiters = 20.0f;
        repaired = true;
    }

    if(isnan(washLiters) || isinf(washLiters))
    {
        washLiters = 10.0f;
        repaired = true;
    }

    if(isnan(fertilizerSecondsPerLiter) || isinf(fertilizerSecondsPerLiter))
    {
        fertilizerSecondsPerLiter = 5.0f;
        repaired = true;
    }

    if(repaired)
    {

        Serial.println(
            "[Config] Repaired corrupted numeric field(s) in NVS, saving fix"
        );

        save();

    }


    return true;
}



bool IrrigationConfig::save()
{

    preferences.begin(
        "irrigation",
        false
    );


    preferences.putFloat(
        "pulses",
        pulsesPerLiter
    );


    preferences.putFloat(
        "liters",
        irrigationLiters
    );


    preferences.putFloat(
        "wash",
        washLiters
    );


    preferences.putFloat(
        "fertsec",
        fertilizerSecondsPerLiter
    );


    preferences.putUInt(
        "valveto",
        valveTimeoutSeconds
    );


    preferences.putUInt(
        "irrigto",
        irrigationTimeoutSeconds
    );


    preferences.putString(
        "ssid",
        wifiSSID
    );


    preferences.putString(
        "pass",
        wifiPassword
    );


    preferences.putString(
        "ptoken",
        pushoverToken
    );


    preferences.putString(
        "puser",
        pushoverUser
    );


    preferences.end();

    return true;
}



void IrrigationConfig::reset()
{
    *this = IrrigationConfig();
    save();
}



bool IrrigationConfig::valid() const
{

    /*
     * NaN/Inf can end up in a float NVS entry if a write is
     * interrupted mid-flash (power loss, unexpected reset).
     * These MUST be checked explicitly: every comparison with
     * NaN evaluates false, so a NaN silently passes all the
     * "<= 0" / "< 0" range checks below and would reach the
     * irrigation state machine as a live value.
     */

    float values[] = {
        pulsesPerLiter,
        irrigationLiters,
        washLiters,
        fertilizerSecondsPerLiter
    };

    for(float v : values)
    {
        if(isnan(v) || isinf(v))
            return false;
    }


    if(pulsesPerLiter <= 0.0f)
        return false;

    if(irrigationLiters <= 0.0f)
        return false;

    if(washLiters < 0.0f)
        return false;

    if(fertilizerSecondsPerLiter < 0.0f)
        return false;

    if(valveTimeoutSeconds == 0)
        return false;

    if(irrigationTimeoutSeconds == 0)
        return false;

    return true;

}