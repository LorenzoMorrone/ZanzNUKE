#include "WebServer.h"

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <WiFi.h>

#include <esp_system.h>

#include "IrrigationManager.h"
#include "SafetyManager.h"
#include "Outputs.h"
#include "Sensors.h"
#include "FlowMeter.h"
#include "Config.h"
#include "Scheduler.h"
#include "Clock.h"
#include "NetworkManager.h"
#include "ErrorStrings.h"
#include "WebStyle.h"
#include "EventLog.h"


AsyncWebServer server(80);


/*
 * ---------------------------------------------------------
 * Manual diagnostic relay tests ("/test" page).
 *
 * Web handlers (async task) only set a request flag; the actual
 * relay control happens exclusively in WebServerManager::update()
 * on the main loop task, same pattern as IrrigationManager's
 * command queue. This keeps every digitalWrite() to the relays
 * on one task, and lets SafetyManager keep watching for real
 * faults (overflow, impossible float state) even during a test.
 * ---------------------------------------------------------
 */

enum class TestRelay : uint8_t
{
    NONE,
    PUMP,
    VALVE,
    FERTILIZER
};

static volatile bool requestTestPump = false;
static volatile bool requestTestValve = false;
static volatile bool requestTestFertilizer = false;

/*
 * Set by the request handler (async task) right before the
 * corresponding requestTestX flag, read by tryStartTest() on
 * the main loop task once that flag is consumed. Not itself
 * used to synchronize anything (the requestTestX bool is what
 * does that) - it just needs to still hold the right value at
 * the moment the flag is read, which holds here since a second
 * test request can't overwrite it before the first is consumed
 * (WebServerManager::update() runs every loop() iteration, far
 * more often than a human can submit a second request).
 */
static volatile uint32_t requestedTestDurationMs = 3000;

static TestRelay activeTestRelay = TestRelay::NONE;
static uint32_t testRelayOffAt = 0;

constexpr uint32_t TEST_RELAY_MIN_DURATION_MS = 1000;
constexpr uint32_t TEST_RELAY_MAX_DURATION_MS = 30000;
constexpr uint32_t TEST_RELAY_DEFAULT_DURATION_MS = 3000;


static uint32_t clampTestDurationMs(uint32_t ms)
{
    if(ms < TEST_RELAY_MIN_DURATION_MS) return TEST_RELAY_MIN_DURATION_MS;
    if(ms > TEST_RELAY_MAX_DURATION_MS) return TEST_RELAY_MAX_DURATION_MS;
    return ms;
}


/*
 * WiFi setup/save lives entirely in NetworkManager now, on its
 * own dedicated server that only runs during AP mode - see
 * NetworkManager::beginSetupServer(). Nothing web-UI-related
 * here needs to know about it.
 */


static void tryStartTest(TestRelay which, uint32_t durationMs)
{

    if(IrrigationManager::state() != IrrigationState::IDLE)
        return;

    if(SafetyManager::hasError())
        return;


    switch(which)
    {

    case TestRelay::PUMP:

        if(Sensors::tankEmpty())
            return;

        Outputs::pumpOn();

        break;


    case TestRelay::VALVE:

        if(Sensors::tankFull())
            return;

        SafetyManager::setManualFlowAllowed(true);

        Outputs::valveOpen();

        break;


    case TestRelay::FERTILIZER:

        Outputs::peristalticOn();

        break;


    default:

        return;

    }


    activeTestRelay = which;

    testRelayOffAt =
        millis() + durationMs;

}



void WebServerManager::update()
{

    if(activeTestRelay != TestRelay::NONE)
    {

        bool timeElapsed =
            (int32_t)(millis() - testRelayOffAt)
            >=
            0;

        /*
         * Safety stop: a pump test must never keep running once
         * the tank reports empty, exactly like the real
         * IRRIGATING state - the fixed test duration is a upper
         * bound, not a guarantee that water will still be
         * present for the whole thing.
         */

        bool emptyFloatTripped =
            activeTestRelay == TestRelay::PUMP
            &&
            Sensors::tankEmpty();


        if(timeElapsed || emptyFloatTripped)
        {

            if(emptyFloatTripped && !timeElapsed)
            {

                Serial.println(
                    "[Test] Pump test stopped early: tank empty float active"
                );

            }


            Outputs::stopAll();

            if(activeTestRelay == TestRelay::VALVE)
                SafetyManager::setManualFlowAllowed(false);

            activeTestRelay = TestRelay::NONE;

        }
        else
        {

            /*
             * A test is already running: ignore any other
             * request that arrived while it was in progress.
             */

            requestTestPump = false;
            requestTestValve = false;
            requestTestFertilizer = false;

            return;

        }

    }


    if(requestTestPump)
    {
        requestTestPump = false;
        tryStartTest(TestRelay::PUMP, requestedTestDurationMs);
    }
    else if(requestTestValve)
    {
        requestTestValve = false;
        tryStartTest(TestRelay::VALVE, requestedTestDurationMs);
    }
    else if(requestTestFertilizer)
    {
        requestTestFertilizer = false;
        tryStartTest(TestRelay::FERTILIZER, requestedTestDurationMs);
    }

}



/*
 * ---------------------------------------------------------
 * All pages here are generated from live sensor state and
 * stored config, so they must never be cached - otherwise a
 * browser can show stale readings or stale config values after
 * they've changed.
 * ---------------------------------------------------------
 */

static void sendNoCacheHtml(
    AsyncWebServerRequest *request,
    int code,
    const String &html
)
{

    AsyncWebServerResponse *response =
        request->beginResponse(code, "text/html", html);

    response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");

    response->addHeader("Pragma", "no-cache");

    response->addHeader("Expires", "0");

    request->send(response);

}



/*
 * ---------------------------------------------------------
 * Dashboard
 * ---------------------------------------------------------
 */

static void handleRoot(AsyncWebServerRequest *request)
{

    /*
     * WebServerManager::begin() is never called while running as
     * the setup access point (see main.cpp) - by the time any
     * route here can be reached, we're always connected to a
     * real network, so no AP-mode branch is needed here.
     */

    String html =
        htmlHead("Irrigation");

    html += "<h1>Irrigation</h1>";

    html += R"rawliteral(
<div class="card center">
<span class="badge badge-idle" id="stateBadge">Loading&hellip;</span>
<div class="stat-grid">
<div><span class="stat-label">Tank</span><span class="stat-value" id="tankText">&ndash;</span></div>
<div><span class="stat-label">Progress</span><span class="stat-value" id="progText">&ndash;</span></div>
<div><span class="stat-label">Next run</span><span class="stat-value" id="nextText">&ndash;</span></div>
<div><span class="stat-label">Flow pulses</span><span class="stat-value" id="pulseText">&ndash;</span></div>
</div>
<div class="hint" id="timeText"></div>
</div>

<button id="btnStart" onclick="cmd('/api/start','btnStart')">Start Irrigation</button>
<button id="btnWash" class="btn-secondary" onclick="cmd('/api/wash','btnWash')">Wash System</button>
<div class="btn-row">
<button id="btnStop" class="btn-danger" onclick="cmd('/api/stop','btnStop')">Stop</button>
<button id="btnClear" class="btn-warning" onclick="cmd('/api/clear','btnClear')">Clear Error</button>
</div>
<button class="btn-secondary" onclick="refresh()">Refresh</button>

<script>
function refresh()
{
    fetch('/api/status')
    .then(r=>r.json())
    .then(d=>{

        var badge = document.getElementById('stateBadge');
        var hasError = d.error !== 0;

        badge.className = 'badge ' + (hasError ? 'badge-error' : (d.state === 0 ? 'badge-idle' : 'badge-active'));
        badge.textContent = hasError ? ('ERROR: ' + d.errorText) : d.stateText;

        document.getElementById('tankText').textContent =
            d.full ? 'FULL' : (d.empty ? 'EMPTY' : 'OK');

        document.getElementById('progText').textContent =
            d.liters.toFixed(1) + ' / ' + d.target.toFixed(1) + ' L';

        document.getElementById('nextText').textContent = d.nextRun;
        document.getElementById('pulseText').textContent = d.pulses;
        document.getElementById('timeText').textContent = d.time + ' · ' + d.ip;

    });
}

/* Fetched once at page load only - not polled continuously. */
refresh();

function cmd(url, btnId)
{
    var btn = document.getElementById(btnId);
    if(btn) btn.disabled = true;

    fetch(url)
    .then(()=>{ refresh(); })
    .finally(()=>{ if(btn) btn.disabled = false; });
}
</script>
)rawliteral";

    html += htmlFoot("home");


    sendNoCacheHtml(request, 200, html);

}



static void handleApiStatus(AsyncWebServerRequest *request)
{

    JsonDocument doc;

    IrrigationState state =
        IrrigationManager::state();

    ErrorCode err =
        SafetyManager::error();


    doc["state"] = (int)state;
    doc["stateText"] = stateToString(state, IrrigationManager::isWashCycle());
    doc["isWash"] = IrrigationManager::isWashCycle();

    doc["error"] = (int)err;
    doc["errorText"] = errorToString(err);

    doc["full"] = Sensors::tankFull();
    doc["empty"] = Sensors::tankEmpty();

    doc["pulses"] = FlowMeter::pulses();

    doc["liters"] =
        (config.pulsesPerLiter > 0.0f)
        ? (FlowMeter::pulses() / config.pulsesPerLiter)
        : 0.0f;

    doc["target"] = IrrigationManager::targetLitersValue();

    doc["time"] = Clock::datetime();

    doc["ip"] = NetworkManager::ip();

    doc["apMode"] = NetworkManager::isAPMode();

    doc["nextRun"] = Scheduler::nextRunDescription();

    doc["irrigationLiters"] = config.irrigationLiters;

    doc["washLiters"] = config.washLiters;


    String response;

    serializeJson(doc, response);

    request->send(200, "application/json", response);

}



/*
 * ---------------------------------------------------------
 * Commands (thread-safe: only set flags consumed by the main
 * loop task, never touch relays/state directly here)
 * ---------------------------------------------------------
 */

static void handleApiStart(AsyncWebServerRequest *request)
{
    IrrigationManager::requestStart();
    request->send(200, "text/plain", "Queued");
}

static void handleApiWash(AsyncWebServerRequest *request)
{
    IrrigationManager::requestWash();
    request->send(200, "text/plain", "Queued");
}

static void handleApiStop(AsyncWebServerRequest *request)
{
    IrrigationManager::requestStop();
    request->send(200, "text/plain", "Queued");
}

static void handleApiClear(AsyncWebServerRequest *request)
{
    SafetyManager::requestClear();
    request->send(200, "text/plain", "Queued");
}



/*
 * ---------------------------------------------------------
 * Configuration page
 * ---------------------------------------------------------
 */

static void handleConfigPage(AsyncWebServerRequest *request)
{

    String html = htmlHead("Configuration");

    html += "<h1>Configuration</h1>";

    if(!config.valid())
    {
        html += "<div class='card'><p class='err' style='margin:0'>Current configuration "
                "is invalid - irrigation is blocked until this is fixed.</p></div>";
    }

    /*
     * All float fields below use step='0.1' AND display with 1
     * decimal (String(x, 1)) - these must always match. Showing
     * 2 decimals while only allowing 0.1 increments is confusing
     * (the second digit is never actually reachable by the
     * stepper and just looks like stale precision).
     */

    html += "<form action='/config/save' method='get'>";

    html += "<div class='card'><h3>Watering</h3>";

    html += "<label>Water per irrigation (liters)</label>";
    html += "<input name='liters' type='number' inputmode='decimal' step='0.1' min='0.1' value='";
    html += String(config.irrigationLiters, 1);
    html += "'>";

    html += "<label>Wash liters</label>";
    html += "<input name='wash' type='number' inputmode='decimal' step='0.1' min='0' value='";
    html += String(config.washLiters, 1);
    html += "'>";

    html += "<label>Flow meter pulses per liter</label>";
    html += "<input name='pulses' type='number' inputmode='decimal' step='0.1' min='1' value='";
    html += String(config.pulsesPerLiter, 1);
    html += "'>";

    html += "</div><div class='card'><h3>Fertilizer</h3>";

    html += "<label>Pump seconds per liter of water</label>";
    html += "<input name='fert' type='number' inputmode='decimal' step='0.1' min='0' value='";
    html += String(config.fertilizerSecondsPerLiter, 1);
    html += "'>";

    html += "</div><div class='card'><h3>Safety timeouts</h3>";

    html += "<label>Valve timeout (seconds)</label>";
    html += "<input name='valve_timeout' type='number' inputmode='numeric' step='1' min='10' value='";
    html += String(config.valveTimeoutSeconds);
    html += "'>";

    html += "<label>Irrigation timeout (seconds)</label>";
    html += "<input name='irrigation_timeout' type='number' inputmode='numeric' step='1' min='10' value='";
    html += String(config.irrigationTimeoutSeconds);
    html += "'>";

    html += "</div><div class='card'><h3>WiFi</h3>";

    html += "<label>Network name (SSID)</label>";
    html += "<input name='ssid' type='text' value='";
    html += config.wifiSSID;
    html += "'>";

    html += "<label>Password</label>";
    html += "<input name='wifi_pass' type='password' value='' placeholder='Leave blank to keep current'>";

    html += "<p class='hint'>Changing either of these restarts the device to reconnect. "
            "If it can't reach the new network, it falls back to broadcasting "
            "<b>Irrigation-Setup</b> again.</p>";

    html += "</div><div class='card'><h3>Pushover notifications</h3>";

    html += "<label>App token</label>";
    html += "<input name='pushover_token' type='text' value='";
    html += config.pushoverToken;
    html += "'>";

    html += "<label>User key</label>";
    html += "<input name='pushover_user' type='text' value='";
    html += config.pushoverUser;
    html += "'>";

    html += "</div>";


    /*
     * Advanced: previously-hardcoded tuning constants, now
     * adjustable without a firmware reflash. Tucked behind a
     * <details> disclosure at the bottom so the common settings
     * above stay the focus for day-to-day use.
     */

    html += "<details><summary>Advanced settings</summary>";

    html += "<div class='card'><h3>Leak &amp; Flow Detection</h3>";

    html += "<label>No-flow stall timeout while filling (seconds)</label>";
    html += "<input name='flow_stall' type='number' inputmode='numeric' step='1' min='1' value='";
    html += String(config.flowStallTimeoutSeconds);
    html += "'>";

    html += "<label>Leak detection pulse tolerance</label>";
    html += "<input name='leak_tol' type='number' inputmode='numeric' step='1' min='0' value='";
    html += String(config.flowLeakTolerancePulses);
    html += "'>";

    html += "<label>Leak detection grace period after valve closes (seconds)</label>";
    html += "<input name='leak_grace' type='number' inputmode='numeric' step='1' min='0' value='";
    html += String(config.flowLeakGraceSeconds);
    html += "'>";

    html += "<label>Leak detection stray-pulse window (seconds)</label>";
    html += "<input name='leak_rebase' type='number' inputmode='numeric' step='1' min='1' value='";
    html += String(config.flowLeakRebaselineSeconds);
    html += "'>";

    html += "<p class='hint'>Controls how sensitive leak/stuck-valve detection is. Lower "
            "tolerance/grace = stricter (more false alarms from residual flow after the "
            "valve closes); higher = more lenient (slower to catch a genuine leak).</p>";

    html += "</div>";


    html += "<div class='card'><h3>Network</h3>";

    html += "<label>WiFi reconnect retry interval (seconds)</label>";
    html += "<input name='wifi_reconnect' type='number' inputmode='numeric' step='1' min='5' value='";
    html += String(config.wifiReconnectIntervalSeconds);
    html += "'>";

    html += "<label>Restart device after WiFi down for (minutes)</label>";
    html += "<input name='wifi_giveup' type='number' inputmode='numeric' step='1' min='1' value='";
    html += String(config.wifiGiveUpRestartMinutes);
    html += "'>";

    html += "</div>";


    html += "<div class='card'><h3>System</h3>";

    html += "<label>Watchdog timeout (seconds)</label>";
    html += "<input name='wdt_timeout' type='number' inputmode='numeric' step='1' min='5' max='120' value='";
    html += String(config.watchdogTimeoutSeconds);
    html += "'>";

    html += "<p class='hint'>Reboots the device if the main loop ever hangs for longer "
            "than this. Takes effect after the device restarts, not immediately.</p>";

    html += "</div>";

    html += "</details>";


    html += "<button>Save</button></form>";

    html += htmlFoot("config");


    sendNoCacheHtml(request, 200, html);

}



static float clampf(float value, float lo, float hi)
{
    if(value < lo) return lo;
    if(value > hi) return hi;
    return value;
}


static void handleConfigSave(AsyncWebServerRequest *request)
{

    if(request->hasParam("liters"))
        config.irrigationLiters =
            clampf(request->getParam("liters")->value().toFloat(), 0.1f, 100000.0f);

    if(request->hasParam("wash"))
        config.washLiters =
            clampf(request->getParam("wash")->value().toFloat(), 0.0f, 100000.0f);

    if(request->hasParam("pulses"))
        config.pulsesPerLiter =
            clampf(request->getParam("pulses")->value().toFloat(), 1.0f, 1000000.0f);

    if(request->hasParam("fert"))
        config.fertilizerSecondsPerLiter =
            clampf(request->getParam("fert")->value().toFloat(), 0.0f, 3600.0f);

    if(request->hasParam("valve_timeout"))
        config.valveTimeoutSeconds =
            (uint32_t)clampf(request->getParam("valve_timeout")->value().toFloat(), 10.0f, 86400.0f);

    if(request->hasParam("irrigation_timeout"))
        config.irrigationTimeoutSeconds =
            (uint32_t)clampf(request->getParam("irrigation_timeout")->value().toFloat(), 10.0f, 86400.0f);


    /*
     * Advanced section - see handleConfigPage() for the field
     * descriptions.
     */

    if(request->hasParam("flow_stall"))
        config.flowStallTimeoutSeconds =
            (uint32_t)clampf(request->getParam("flow_stall")->value().toFloat(), 1.0f, 300.0f);

    if(request->hasParam("leak_tol"))
        config.flowLeakTolerancePulses =
            (uint32_t)clampf(request->getParam("leak_tol")->value().toFloat(), 0.0f, 10000.0f);

    if(request->hasParam("leak_grace"))
        config.flowLeakGraceSeconds =
            (uint32_t)clampf(request->getParam("leak_grace")->value().toFloat(), 0.0f, 60.0f);

    if(request->hasParam("leak_rebase"))
        config.flowLeakRebaselineSeconds =
            (uint32_t)clampf(request->getParam("leak_rebase")->value().toFloat(), 1.0f, 3600.0f);

    if(request->hasParam("wifi_reconnect"))
        config.wifiReconnectIntervalSeconds =
            (uint32_t)clampf(request->getParam("wifi_reconnect")->value().toFloat(), 5.0f, 300.0f);

    if(request->hasParam("wifi_giveup"))
        config.wifiGiveUpRestartMinutes =
            (uint32_t)clampf(request->getParam("wifi_giveup")->value().toFloat(), 1.0f, 60.0f);

    if(request->hasParam("wdt_timeout"))
        config.watchdogTimeoutSeconds =
            (uint32_t)clampf(request->getParam("wdt_timeout")->value().toFloat(), 5.0f, 120.0f);


    if(request->hasParam("pushover_token"))
        config.pushoverToken = request->getParam("pushover_token")->value();

    if(request->hasParam("pushover_user"))
        config.pushoverUser = request->getParam("pushover_user")->value();


    /*
     * WiFi is handled separately from the rest of the fields
     * above: an empty submission must NEVER overwrite the
     * stored value (the password field is deliberately never
     * pre-filled, so it arrives empty on every save that isn't
     * specifically changing it - overwriting unconditionally
     * would wipe the stored password on the very next unrelated
     * config save), and only an actual change should trigger a
     * reconnect/restart.
     */

    bool wifiChanged = false;

    if(request->hasParam("ssid"))
    {

        String newSsid =
            request->getParam("ssid")->value();

        if(
            newSsid.length() > 0
            &&
            newSsid != config.wifiSSID
        )
        {
            config.wifiSSID = newSsid;
            wifiChanged = true;
        }

    }

    if(request->hasParam("wifi_pass"))
    {

        String newPass =
            request->getParam("wifi_pass")->value();

        if(
            newPass.length() > 0
            &&
            newPass != config.wifiPassword
        )
        {
            config.wifiPassword = newPass;
            wifiChanged = true;
        }

    }


    config.save();


    if(wifiChanged)
    {

        Serial.print(
            "[Config] WiFi settings changed (SSID: "
        );
        Serial.print(config.wifiSSID);
        Serial.println("), restarting to reconnect");


        String html = htmlHead("Configuration");

        html += "<h1>Configuration</h1>"
                "<div class='card'>"
                "<p class='ok' style='margin-top:0'>WiFi settings saved. Restarting to reconnect&hellip;</p>"
                "<p class='hint'>Reconnect to your network in about a minute, or rejoin "
                "<b>Irrigation-Setup</b> if it can't reach the new network.</p>"
                "</div>";

        html += htmlFoot();

        sendNoCacheHtml(request, 200, html);


        /*
         * Short delay so the response above actually flushes to
         * the client before the reboot - same pattern already
         * proven reliable in NetworkManager's setup server.
         */

        delay(1000);

        ESP.restart();

        return;

    }


    Serial.println(
        "[Config] Configuration saved"
    );


    request->redirect("/config");

}



/*
 * ---------------------------------------------------------
 * Weekly schedule page
 * ---------------------------------------------------------
 */

static void handleSchedulePage(AsyncWebServerRequest *request)
{

    String html = htmlHead("Schedule");

    html += "<h1>Weekly Schedule</h1>";

    html += "<p class='hint' style='margin-top:-8px;margin-bottom:16px'>Up to 3 watering "
            "times per day. Tap a time to open the picker.</p>";

    html += "<form action='/schedule/save' method='get'>";


    const char* weekdays[] = {
        "Sunday", "Monday", "Tuesday", "Wednesday",
        "Thursday", "Friday", "Saturday"
    };


    for(int day = 0; day < 7; day++)
    {

        html += "<div class='card daycard'><h3>";
        html += weekdays[day];
        html += "</h3>";


        for(int slot = 0; slot < 3; slot++)
        {

            int index = day * 3 + slot;

            ScheduleEntry e =
                Scheduler::getEntry(index);


            char timeValue[6];

            snprintf(
                timeValue,
                sizeof(timeValue),
                "%02u:%02u",
                e.hour,
                e.minute
            );


            html += "<div class='row'><input type='checkbox' name='en_";
            html += String(index);
            html += "'";
            if(e.enabled) html += " checked";
            html += "><input type='time' name='t_";
            html += String(index);
            html += "' value='";
            html += timeValue;
            html += "'></div>";

        }

        html += "</div>";

    }


    html += "<button>Save Schedule</button></form>";

    html += htmlFoot("schedule");


    sendNoCacheHtml(request, 200, html);

}



static uint8_t clampu(long value, uint8_t lo, uint8_t hi)
{
    if(value < lo) return lo;
    if(value > hi) return hi;
    return (uint8_t)value;
}



/*
 * Parses an <input type="time"> value ("HH:MM", per the HTML
 * spec always zero-padded 24h) out of a request param. Falls
 * back to 00:00 if the param is missing or malformed rather
 * than rejecting the whole save - a bad single slot shouldn't
 * block saving the other 20.
 */
static void parseTimeParam(
    AsyncWebServerRequest *request,
    const String &name,
    uint8_t &hour,
    uint8_t &minute
)
{

    hour = 0;
    minute = 0;

    if(!request->hasParam(name.c_str()))
        return;


    String value =
        request->getParam(name.c_str())->value();

    int colon =
        value.indexOf(':');

    if(colon < 0)
        return;


    hour =
        clampu(value.substring(0, colon).toInt(), 0, 23);

    minute =
        clampu(value.substring(colon + 1).toInt(), 0, 59);

}


static void handleScheduleSave(AsyncWebServerRequest *request)
{

    for(int day = 0; day < 7; day++)
    {

        for(int slot = 0; slot < 3; slot++)
        {

            int index = day * 3 + slot;

            String enName = "en_" + String(index);
            String tName = "t_" + String(index);

            ScheduleEntry e{};

            e.enabled = request->hasParam(enName.c_str());

            e.weekday = (uint8_t)day;

            parseTimeParam(
                request,
                tName,
                e.hour,
                e.minute
            );

            Scheduler::setEntry(index, e);

        }

    }


    Scheduler::saveEntries();

    request->redirect("/schedule");

}



/*
 * ---------------------------------------------------------
 * Diagnostics / manual relay test / backup
 * ---------------------------------------------------------
 */

static const char* resetReasonToString(esp_reset_reason_t reason)
{
    switch(reason)
    {
    case ESP_RST_POWERON:   return "Power-on";
    case ESP_RST_EXT:       return "External pin reset";
    case ESP_RST_SW:        return "Software (ESP.restart())";
    case ESP_RST_PANIC:     return "PANIC / exception";
    case ESP_RST_INT_WDT:   return "Interrupt watchdog";
    case ESP_RST_TASK_WDT:  return "Task watchdog (loop() hung)";
    case ESP_RST_WDT:       return "Other watchdog";
    case ESP_RST_DEEPSLEEP: return "Woke from deep sleep";
    case ESP_RST_BROWNOUT:  return "Brownout (power supply sag)";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "Unknown";
    }
}


static void handleDiagnostics(AsyncWebServerRequest *request)
{

    String html = htmlHead("Diagnostics");

    html += "<h1>Diagnostics</h1>";

    html += "<div class='card'><h3>System</h3><div class='stat-grid'>";

    html += "<div><span class='stat-label'>Uptime</span><span class='stat-value'>";
    html += String(millis() / 1000);
    html += " s</span></div>";

    html += "<div><span class='stat-label'>Free heap</span><span class='stat-value'>";
    html += String(ESP.getFreeHeap());
    html += "</span></div>";

    html += "<div><span class='stat-label'>CPU</span><span class='stat-value'>";
    html += String(getCpuFrequencyMhz());
    html += " MHz</span></div>";

    html += "<div><span class='stat-label'>Flow pulses</span><span class='stat-value'>";
    html += String(FlowMeter::pulses());
    html += "</span></div>";

    html += "</div></div>";


    html += "<div class='card'><h3>Network</h3><div class='stat-grid'>";

    html += "<div><span class='stat-label'>Mode</span><span class='stat-value'>";
    html += (NetworkManager::isAPMode() ? "Access Point" : "Station");
    html += "</span></div>";

    html += "<div><span class='stat-label'>Signal</span><span class='stat-value'>";
    html += String(WiFi.RSSI());
    html += " dBm</span></div>";

    html += "<div style='grid-column:1/-1'><span class='stat-label'>IP address</span><span class='stat-value'>";
    html += NetworkManager::ip();
    html += "</span></div>";

    html += "</div></div>";


    bool cfgValid = config.valid();

    html += "<div class='card'><h3>Irrigation</h3><div class='stat-grid'>";

    html += "<div><span class='stat-label'>State</span><span class='stat-value'>";
    html += stateToString(IrrigationManager::state(), IrrigationManager::isWashCycle());
    html += "</span></div>";

    html += "<div><span class='stat-label'>Error</span><span class='stat-value'>";
    html += errorToString(SafetyManager::error());
    html += "</span></div>";

    html += "<div style='grid-column:1/-1'><span class='stat-label'>Config</span><span class='stat-value ";
    html += (cfgValid ? "ok" : "err");
    html += "'>";
    html += (cfgValid ? "Valid" : "INVALID");
    html += "</span></div>";

    html += "</div></div>";


    /*
     * Advanced / debug: raw sensor states and internal safety-
     * loop timing, tucked behind a disclosure since it's rarely
     * needed but is exactly what's useful to have on hand after
     * an unexpected trigger or a report of "it crashed" - so the
     * next investigation starts from real numbers instead of
     * guessing what the device was seeing at the time.
     */

    html += "<details><summary>Advanced / Debug</summary>";


    html += "<div class='card'><h3>Raw Sensors</h3><div class='stat-grid'>";

    html += "<div><span class='stat-label'>Top float (full)</span><span class='stat-value ";
    html += (Sensors::tankFull() ? "err" : "ok");
    html += "'>";
    html += (Sensors::tankFull() ? "CLOSED" : "open");
    html += "</span></div>";

    html += "<div><span class='stat-label'>Bottom float (empty)</span><span class='stat-value ";
    html += (Sensors::tankEmpty() ? "err" : "ok");
    html += "'>";
    html += (Sensors::tankEmpty() ? "OPEN (empty)" : "closed (water present)");
    html += "</span></div>";

    html += "<div><span class='stat-label'>Flow pulses (raw)</span><span class='stat-value'>";
    html += String(FlowMeter::pulses());
    html += "</span></div>";

    html += "<div><span class='stat-label'>Computed liters</span><span class='stat-value'>";
    html += String(
        (config.pulsesPerLiter > 0.0f)
        ? (FlowMeter::pulses() / config.pulsesPerLiter)
        : 0.0f,
        2
    );
    html += "</span></div>";

    html += "</div></div>";


    html += "<div class='card'><h3>Safety Loop Internals</h3><div class='stat-grid'>";

    html += "<div><span class='stat-label'>Manual flow override</span><span class='stat-value'>";
    html += (SafetyManager::isManualFlowAllowed() ? "ACTIVE" : "off");
    html += "</span></div>";

    html += "<div><span class='stat-label'>Flow baseline age</span><span class='stat-value'>";
    html += String(SafetyManager::flowClosedBaselineAgeMs() / 1000);
    html += " s</span></div>";

    html += "<div><span class='stat-label'>Last flow change</span><span class='stat-value'>";
    html += String(SafetyManager::lastFlowChangeAgeMs() / 1000);
    html += " s ago</span></div>";

    html += "<div><span class='stat-label'>Last flow count seen</span><span class='stat-value'>";
    html += String(SafetyManager::lastFlowPulseCount());
    html += "</span></div>";

    html += "</div></div>";


    html += "<div class='card'><h3>Irrigation Internals</h3><div class='stat-grid'>";

    html += "<div><span class='stat-label'>State elapsed</span><span class='stat-value'>";
    html += String(IrrigationManager::stateElapsedMs() / 1000);
    html += " s</span></div>";

    html += "<div><span class='stat-label'>Target liters</span><span class='stat-value'>";
    html += String(IrrigationManager::targetLitersValue(), 2);
    html += "</span></div>";

    html += "<div><span class='stat-label'>Wash cycle</span><span class='stat-value'>";
    html += (IrrigationManager::isWashCycle() ? "yes" : "no");
    html += "</span></div>";

    html += "<div><span class='stat-label'>Clock synced</span><span class='stat-value'>";
    html += (Clock::valid() ? "yes" : "no");
    html += "</span></div>";

    html += "<div style='grid-column:1/-1'><span class='stat-label'>Next scheduled run</span><span class='stat-value'>";
    html += Scheduler::nextRunDescription();
    html += "</span></div>";

    html += "</div></div>";


    html += "<div class='card'><h3>System / Crash Info</h3><div class='stat-grid'>";

    html += "<div style='grid-column:1/-1'><span class='stat-label'>Last reset reason</span><span class='stat-value'>";
    html += resetReasonToString(esp_reset_reason());
    html += "</span></div>";

    html += "<div><span class='stat-label'>Chip</span><span class='stat-value'>";
    html += ESP.getChipModel();
    html += " rev ";
    html += String(ESP.getChipRevision());
    html += "</span></div>";

    html += "<div><span class='stat-label'>Flash size</span><span class='stat-value'>";
    html += String(ESP.getFlashChipSize() / 1024 / 1024);
    html += " MB</span></div>";

    html += "<div><span class='stat-label'>Min free heap ever</span><span class='stat-value'>";
    html += String(ESP.getMinFreeHeap());
    html += "</span></div>";

    html += "<div><span class='stat-label'>Max alloc block</span><span class='stat-value'>";
    html += String(ESP.getMaxAllocHeap());
    html += "</span></div>";

    html += "<div><span class='stat-label'>Sketch size</span><span class='stat-value'>";
    html += String(ESP.getSketchSize() / 1024);
    html += " KB</span></div>";

    html += "<div><span class='stat-label'>Free sketch space</span><span class='stat-value'>";
    html += String(ESP.getFreeSketchSpace() / 1024);
    html += " KB</span></div>";

    html += "<div style='grid-column:1/-1'><span class='stat-label'>MAC address</span><span class='stat-value'>";
    html += WiFi.macAddress();
    html += "</span></div>";

    html += "</div></div>";


    html += "<div class='card'><h3>Event Log</h3>";
    html += "<pre>";

    String eventLog = EventLog::get();

    html += (eventLog.length() > 0) ? eventLog : "(empty)";

    html += "</pre></div>";


    html += "</details>";


    html += htmlFoot("diagnostics");


    sendNoCacheHtml(request, 200, html);

}



static void handleTestPage(AsyncWebServerRequest *request)
{

    String html = htmlHead("Manual relay test");

    html += "<h1>Manual Test</h1>";

    html += "<div class='card'><p class='hint' style='margin-top:0'>Runs the selected "
            "relay for the chosen duration, only while the system is idle and with no "
            "active error. Blocked automatically if the relevant tank sensor makes it "
            "unsafe to start (e.g. pump test while the tank is empty) - and the pump "
            "test also stops immediately, before the timer runs out, if the tank empty "
            "float activates mid-test.</p></div>";

    html += "<div class='card'>";
    html += "<label>Test duration: <span id='durVal'>";
    html += String(TEST_RELAY_DEFAULT_DURATION_MS / 1000);
    html += "</span>s</label>";
    html += "<input type='range' id='durSlider' min='";
    html += String(TEST_RELAY_MIN_DURATION_MS / 1000);
    html += "' max='";
    html += String(TEST_RELAY_MAX_DURATION_MS / 1000);
    html += "' value='";
    html += String(TEST_RELAY_DEFAULT_DURATION_MS / 1000);
    html += "' oninput=\"document.getElementById('durVal').textContent=this.value\">";
    html += "</div>";

    html += "<button id='btnPump' onclick=\"test('/relay/pump','btnPump')\">Test Pump</button>";

    html += "<button id='btnValve' class='btn-secondary' onclick=\"test('/relay/valve','btnValve')\">Test Valve</button>";

    html += "<button id='btnFert' class='btn-secondary' onclick=\"test('/relay/fertilizer','btnFert')\">Test Fertilizer Pump</button>";

    html += "<script>";
    html += "function test(url, id) {";
    html += "  var seconds = document.getElementById('durSlider').value;";
    html += "  var btn = document.getElementById(id);";
    html += "  btn.disabled = true;";
    html += "  fetch(url + '?seconds=' + seconds).finally(function() {";
    html += "    setTimeout(function(){ btn.disabled = false; }, seconds * 1000);";
    html += "  });";
    html += "}";
    html += "</script>";

    html += htmlFoot("test");


    sendNoCacheHtml(request, 200, html);

}



static void handleBackup(AsyncWebServerRequest *request)
{

    JsonDocument doc;

    doc["irrigationLiters"] = config.irrigationLiters;
    doc["washLiters"] = config.washLiters;
    doc["pulsesPerLiter"] = config.pulsesPerLiter;
    doc["fertilizerSecondsPerLiter"] = config.fertilizerSecondsPerLiter;
    doc["valveTimeoutSeconds"] = config.valveTimeoutSeconds;
    doc["irrigationTimeoutSeconds"] = config.irrigationTimeoutSeconds;


    String out;

    serializeJson(doc, out);

    request->send(200, "application/json", out);

}



/*
 * ---------------------------------------------------------
 * Setup
 * ---------------------------------------------------------
 */

void WebServerManager::begin()
{

    setupRoutes();

    server.begin();

}



void WebServerManager::setupRoutes()
{

    /*
     * ==========================================================
     * IMPORTANT: every route below uses AsyncURIMatcher::exact().
     *
     * A plain server.on("/config", ...) does NOT mean "the URL
     * /config". ESPAsyncWebServer defaults a bare string to its
     * Type::BackwardCompatible matcher, which is:
     *
     *     (uri == path) || path.startsWith(uri + "/")
     *
     * (see AsyncURIMatcher in the library's WebServer.cpp).
     *
     * So "/config" ALSO matches "/config/save", "/config/foo",
     * and so on. Handlers are dispatched in registration order,
     * first match wins (AsyncWebServer::_attachHandler), so
     * registering "/config" before "/config/save" means every
     * POST/GET to /config/save was silently answered by the
     * config PAGE handler - handleConfigSave() was never called,
     * which is why absolutely nothing appeared in the serial log.
     *
     * The same shadowing hit "/schedule" -> "/schedule/save" and,
     * earlier, "/wifi" -> "/wifi/save" + "/wifi/scan".
     *
     * Routes like /api/start and /relay/pump were unaffected only
     * because no parent "/api" or "/relay" handler exists.
     *
     * exact() pins each route to its own URL, so this class of
     * bug cannot come back as routes are added.
     * ==========================================================
     */

    using M = AsyncURIMatcher;

    server.on(M::exact("/"), HTTP_GET, handleRoot);

    server.on(M::exact("/api/status"), HTTP_GET, handleApiStatus);
    server.on(M::exact("/api/start"), HTTP_GET, handleApiStart);
    server.on(M::exact("/api/wash"), HTTP_GET, handleApiWash);
    server.on(M::exact("/api/stop"), HTTP_GET, handleApiStop);
    server.on(M::exact("/api/clear"), HTTP_GET, handleApiClear);

    server.on(M::exact("/config"), HTTP_GET, handleConfigPage);
    server.on(M::exact("/config/save"), HTTP_GET, handleConfigSave);

    server.on(M::exact("/schedule"), HTTP_GET, handleSchedulePage);
    server.on(M::exact("/schedule/save"), HTTP_GET, handleScheduleSave);

    /*
     * WiFi setup lives entirely in NetworkManager's own isolated
     * server, only running during AP mode - nothing to register
     * here (see NetworkManager::beginSetupServer()).
     */

    server.on(M::exact("/diagnostics"), HTTP_GET, handleDiagnostics);

    server.on(M::exact("/test"), HTTP_GET, handleTestPage);

    server.on(
        M::exact("/relay/pump"), HTTP_GET,
        [](AsyncWebServerRequest *request)
        {
            if(request->hasParam("seconds"))
                requestedTestDurationMs = clampTestDurationMs(
                    (uint32_t)(request->getParam("seconds")->value().toFloat() * 1000.0f)
                );
            requestTestPump = true;
            request->send(200, "text/plain", "OK");
        }
    );

    server.on(
        M::exact("/relay/valve"), HTTP_GET,
        [](AsyncWebServerRequest *request)
        {
            if(request->hasParam("seconds"))
                requestedTestDurationMs = clampTestDurationMs(
                    (uint32_t)(request->getParam("seconds")->value().toFloat() * 1000.0f)
                );
            requestTestValve = true;
            request->send(200, "text/plain", "OK");
        }
    );

    server.on(
        M::exact("/relay/fertilizer"), HTTP_GET,
        [](AsyncWebServerRequest *request)
        {
            if(request->hasParam("seconds"))
                requestedTestDurationMs = clampTestDurationMs(
                    (uint32_t)(request->getParam("seconds")->value().toFloat() * 1000.0f)
                );
            requestTestFertilizer = true;
            request->send(200, "text/plain", "OK");
        }
    );

    server.on(M::exact("/backup"), HTTP_GET, handleBackup);

}
