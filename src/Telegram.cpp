#include "Telegram.h"

#include <WiFiClientSecure.h>
#include <HTTPClient.h>

#include "Config.h"
#include "Clock.h"
#include "IrrigationManager.h"
#include "SafetyManager.h"
#include "Scheduler.h"
#include "FlowMeter.h"
#include "Sensors.h"
#include "NetworkManager.h"
#include "EventLog.h"
#include "ErrorStrings.h"
#include <ArduinoJson.h>

struct PendingNotification
{
    String title;
    String message;
};

constexpr uint8_t MAX_PENDING = 10;

static PendingNotification t_pending[MAX_PENDING];
static uint8_t t_pendingCount = 0;
static int32_t t_lastMessageId = 0;

static void t_enqueue(const String &title, const String &message)
{
    if(t_pendingCount >= MAX_PENDING)
    {
        for(uint8_t i = 1; i < MAX_PENDING; i++)
            t_pending[i - 1] = t_pending[i];
        t_pendingCount = MAX_PENDING - 1;
    }

    t_pending[t_pendingCount].title = title;
    t_pending[t_pendingCount].message = message;
    t_pendingCount++;
}

static String urlEncode(const String &s)
{
    String out;
    out.reserve(s.length() * 3);
    for(size_t i = 0; i < s.length(); i++)
    {
        char c = s[i];
        if((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c=='-' || c=='_' || c=='.' || c=='~')
        {
            out += c;
        }
        else if(c == ' ') out += "%20";
        else if(c == '\n') out += "%0A";
        else
        {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
            out += buf;
        }
    }
    return out;
}

static String htmlEscape(const String &s)
{
    String out;
    out.reserve(s.length() * 2);
    for(size_t i = 0; i < s.length(); i++)
    {
        char c = s[i];
        switch(c)
        {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += c; break;
        }
    }
    return out;
}

static bool t_sendNow(const String &title, const String &message)
{
    if(config.telegramBotToken.length() == 0 || config.telegramChatId.length() == 0)
        return false;

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(5000);

    HTTPClient http;
    http.setConnectTimeout(3000);
    http.setTimeout(5000);

    String url = "https://api.telegram.org/bot" + config.telegramBotToken + "/sendMessage";

    http.begin(client, url);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    // Build HTML-formatted message: bold title, italic timestamp, newline, body
    String titleEsc = htmlEscape(title);
    String timeEsc = htmlEscape(Clock::datetime());
    String bodyEsc = htmlEscape(message);

    String textHtml = String("<b>") + titleEsc + "</b>\n" + String("<i>") + timeEsc + "</i>\n" + bodyEsc;

    String body = "chat_id=" + urlEncode(config.telegramChatId) + "&text=" + urlEncode(textHtml) + "&parse_mode=HTML";

    int result = http.POST(body);
    String resp = http.getString();

    // Capture message_id of the sent message so we can edit its reply_markup later
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    DynamicJsonDocument rdoc(1024);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    if(!deserializeJson(rdoc, resp))
    {
        if(rdoc["ok"].as<bool>())
        {
            t_lastMessageId = rdoc["result"]["message_id"].as<int32_t>();
        }
    }

    http.end();

    if(result <= 0 || (result != 200 && result != 201)) {
        // extra diagnostics and one retry
        Serial.print("[Telegram] POST failed, WiFi status: "); Serial.println(WiFi.status());
        Serial.print("[Telegram] Local IP: "); Serial.println(WiFi.localIP().toString());
        Serial.println("[Telegram] Retrying POST in 500ms...");
        delay(500);
        result = http.POST(body);
        resp = http.getString();
        if(result <= 0 || (result != 200 && result != 201)) {
            Serial.print("[Telegram] send failed: "); Serial.print(result);
            if(resp.length() > 0) {
                Serial.print(" "); Serial.println(resp);
            } else Serial.println();
        }
    }

    return result == 200 || result == 201;
}

bool Telegram::send(String title, String message)
{
    if(config.telegramBotToken.length() == 0 || config.telegramChatId.length() == 0)
        return false;

    // try to flush pending first
    Telegram::flushPending();

    if(WiFi.status() != WL_CONNECTED)
    {
        t_enqueue(title, message);
        return false;
    }

    bool ok = t_sendNow(title, message);
    if(!ok) t_enqueue(title, message);
    return ok;
}

bool Telegram::sendKeyboard(const String &keyboardJson)
{
    if(config.telegramBotToken.length() == 0 || config.telegramChatId.length() == 0)
        return false;

    if(WiFi.status() != WL_CONNECTED)
        return false;

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(5000);

    HTTPClient http;
    http.setConnectTimeout(3000);
    http.setTimeout(5000);

    // Minify keyboard JSON
    String kb = keyboardJson;
    kb.replace("\n", "");
    kb.replace("\r", "");
    while(kb.indexOf("  ") >= 0) kb.replace("  ", " ");


    // Decide behavior based on keyboard type: inline vs reply keyboard
    bool isInline = (kb.indexOf("\"inline_keyboard\"") >= 0);

    if(isInline)
    {
        // For inline keyboards: try editing the last message if we have its id,
        // otherwise send a new message with inline keyboard attached.
        if(t_lastMessageId > 0)
        {
            // Editing reply_markup only works for inline keyboards
            String editUrl = "https://api.telegram.org/bot" + config.telegramBotToken + "/editMessageReplyMarkup";
            http.begin(client, editUrl);
            http.addHeader("Content-Type", "application/x-www-form-urlencoded");

            String body = "chat_id=" + urlEncode(config.telegramChatId) + "&message_id=" + String(t_lastMessageId) + "&reply_markup=" + urlEncode(kb);

            int result = http.POST(body);
            String resp = http.getString();
            if(result <= 0 || (result != 200 && result != 201)) {
                Serial.print("[Telegram] editMessageReplyMarkup failed: "); Serial.print(result);
                if(resp.length() > 0) { Serial.print(" "); Serial.println(resp); }
                else Serial.println();
            }

            http.end();
            return result == 200 || result == 201;
        }
        else
        {
            // Send a fresh message with inline keyboard
            String sendUrl = "https://api.telegram.org/bot" + config.telegramBotToken + "/sendMessage";
            http.begin(client, sendUrl);
            http.addHeader("Content-Type", "application/x-www-form-urlencoded");

            String body = "chat_id=" + urlEncode(config.telegramChatId) + "&text=" + urlEncode("🦟") + "&reply_markup=" + urlEncode(kb);

            int result = http.POST(body);
            String resp = http.getString();
            if(result <= 0 || (result != 200 && result != 201)) {
                Serial.print("[Telegram] send inline keyboard failed: "); Serial.print(result);
                if(resp.length() > 0) { Serial.print(" "); Serial.println(resp); }
                else Serial.println();
            }

            // try parsing message_id
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
            DynamicJsonDocument rdoc(1024);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
            if(!deserializeJson(rdoc, resp))
            {
                if(rdoc["ok"].as<bool>())
                {
                    t_lastMessageId = rdoc["result"]["message_id"].as<int32_t>();
                }
            }

            http.end();
            return result == 200 || result == 201;
        }
    }

    // For ReplyKeyboardMarkup (regular persistent keyboard) we must send a new message
    String sendUrl = "https://api.telegram.org/bot" + config.telegramBotToken + "/sendMessage";
    http.begin(client, sendUrl);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    String body = "chat_id=" + urlEncode(config.telegramChatId) + "&text=" + urlEncode("🦟") + "&reply_markup=" + urlEncode(kb);

    int result = http.POST(body);
    String resp = http.getString();
    if(result <= 0 || (result != 200 && result != 201)) {
        Serial.print("[Telegram] sendKeyboard failed: "); Serial.print(result);
        if(resp.length() > 0) { Serial.print(" "); Serial.println(resp); }
        else Serial.println();

        // extra diagnostics and one retry
        Serial.print("[Telegram] POST failed, WiFi status: "); Serial.println(WiFi.status());
        Serial.print("[Telegram] Local IP: "); Serial.println(WiFi.localIP().toString());
        Serial.println("[Telegram] Retrying POST in 500ms...");
        delay(500);
        result = http.POST(body);
        resp = http.getString();
        if(result <= 0 || (result != 200 && result != 201)) {
            Serial.print("[Telegram] sendKeyboard retry failed: "); Serial.print(result);
            if(resp.length() > 0) { Serial.print(" "); Serial.println(resp); }
            else Serial.println();
        }
    }

    // try parsing message_id even for fallback send
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    DynamicJsonDocument rdoc(1024);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    if(!deserializeJson(rdoc, resp))
    {
        if(rdoc["ok"].as<bool>())
        {
            t_lastMessageId = rdoc["result"]["message_id"].as<int32_t>();
        }
    }

    http.end();
    return result == 200 || result == 201;
}

bool Telegram::sendKeyboardTo(const String &chatId, const String &keyboardJson)
{
    if(config.telegramBotToken.length() == 0)
        return false;

    if(WiFi.status() != WL_CONNECTED)
        return false;

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(5000);

    HTTPClient http;
    http.setConnectTimeout(3000);
    http.setTimeout(5000);

    // Minify keyboard JSON
    String kb = keyboardJson;
    kb.replace("\n", "");
    kb.replace("\r", "");
    while(kb.indexOf("  ") >= 0) kb.replace("  ", " ");

    bool isInline = (kb.indexOf("\"inline_keyboard\"") >= 0);

    if(isInline)
    {
        // send a new message with inline keyboard
        String sendUrl = "https://api.telegram.org/bot" + config.telegramBotToken + "/sendMessage";
        http.begin(client, sendUrl);
        http.addHeader("Content-Type", "application/x-www-form-urlencoded");

        String body = "chat_id=" + urlEncode(chatId) + "&text=" + urlEncode("🦟") + "&reply_markup=" + urlEncode(kb);

        int result = http.POST(body);
        String resp = http.getString();
        if(result <= 0 || (result != 200 && result != 201)) {
            Serial.print("[Telegram] sendKeyboardTo (inline) failed: "); Serial.print(result);
            if(resp.length() > 0) { Serial.print(" "); Serial.println(resp); }
            else Serial.println();
        }

        // try parsing message_id
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
        DynamicJsonDocument rdoc(1024);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
        if(!deserializeJson(rdoc, resp))
        {
            if(rdoc["ok"].as<bool>())
            {
                t_lastMessageId = rdoc["result"]["message_id"].as<int32_t>();
            }
        }

        http.end();
        return result == 200 || result == 201;
    }

    // ReplyKeyboardMarkup send path
    String sendUrl = "https://api.telegram.org/bot" + config.telegramBotToken + "/sendMessage";
    http.begin(client, sendUrl);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    String body = "chat_id=" + urlEncode(chatId) + "&text=" + urlEncode("🦟") + "&reply_markup=" + urlEncode(kb);

    int result = http.POST(body);
    String resp = http.getString();
    if(result <= 0 || (result != 200 && result != 201)) {
        Serial.print("[Telegram] sendKeyboardTo failed: "); Serial.print(result);
        if(resp.length() > 0) { Serial.print(" "); Serial.println(resp); }
        else Serial.println();
    }

    // try parsing message_id
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    DynamicJsonDocument rdoc2(1024);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    if(!deserializeJson(rdoc2, resp))
    {
        if(rdoc2["ok"].as<bool>())
        {
            t_lastMessageId = rdoc2["result"]["message_id"].as<int32_t>();
        }
    }

    http.end();
    return result == 200 || result == 201;
}

bool Telegram::sendTo(const String &chatId, String title, String message)
{
    if(config.telegramBotToken.length() == 0) return false;
    if(WiFi.status() != WL_CONNECTED) return false;

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(5000);

    HTTPClient http;
    http.setConnectTimeout(3000);
    http.setTimeout(5000);

    String url = "https://api.telegram.org/bot" + config.telegramBotToken + "/sendMessage";
    http.begin(client, url);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    String titleEsc = htmlEscape(title);
    String timeEsc = htmlEscape(Clock::datetime());
    String bodyEsc = htmlEscape(message);
    String textHtml = String("<b>") + titleEsc + "</b>\n" + String("<i>") + timeEsc + "</i>\n" + bodyEsc;

    String body = "chat_id=" + urlEncode(chatId) + "&text=" + urlEncode(textHtml) + "&parse_mode=HTML";

    int result = http.POST(body);
    String resp = http.getString();
    if(result <= 0 || (result != 200 && result != 201)) {
        Serial.print("[Telegram] sendTo failed: "); Serial.print(result);
        if(resp.length() > 0) { Serial.print(" "); Serial.println(resp); }
        else Serial.println();
    }

    // parse message_id but only store if it's the configured chat
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    DynamicJsonDocument rdoc(1024);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    if(!deserializeJson(rdoc, resp))
    {
        if(rdoc["ok"].as<bool>())
        {
            if(chatId == config.telegramChatId)
                t_lastMessageId = rdoc["result"]["message_id"].as<int32_t>();
        }
    }

    http.end();
    return result == 200 || result == 201;
}

void Telegram::begin()
{
    // No initialization required currently. Placeholder to satisfy linker.
}

void Telegram::flushPending()
{
    if(t_pendingCount == 0) return;

    if(config.telegramBotToken.length() == 0 || config.telegramChatId.length() == 0)
    {
        t_pendingCount = 0;
        return;
    }

    if(WiFi.status() != WL_CONNECTED) return;

    uint8_t count = t_pendingCount;
    t_pendingCount = 0;

    for(uint8_t i = 0; i < count; i++)
    {
        t_sendNow(t_pending[i].title, t_pending[i].message);
    }
}

uint8_t Telegram::pendingCount()
{
    return t_pendingCount;
}


void Telegram::update()
{
    static int64_t lastUpdateId = 0;
    static uint32_t lastPollMs = 0;

    uint32_t now = millis();
    if(now - lastPollMs < 2000) return; // poll at most every 2s
    lastPollMs = now;

    if(!NetworkManager::connected()) return;
    if(!config.telegramEnabled) return;
    if(config.telegramBotToken.length() == 0) return;

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(5000);

    HTTPClient http;
    http.setConnectTimeout(3000);
    http.setTimeout(5000);

    String url = "https://api.telegram.org/bot" + config.telegramBotToken + "/getUpdates?offset=" + String(lastUpdateId + 1);

    http.begin(client, url);

    int code = http.GET();
    if(code <= 0)
    {
        http.end();
        return;
    }

    String body = http.getString();
    http.end();

    // DynamicJsonDocument is deprecated in this ArduinoJson version; suppress
    // the warning for this allocation so we can specify capacity as before.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    DynamicJsonDocument doc(8192);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    auto err = deserializeJson(doc, body);
    if(err) return;

    if(!doc["result"].is<JsonArray>()) return;

    for(JsonVariant v : doc["result"].as<JsonArray>())
    {
        int64_t updateId = v["update_id"].as<int64_t>();
        if(updateId <= lastUpdateId) continue;

        // handle callback_query (inline keyboard presses) first
        if(!v["callback_query"].isNull())
        {
            JsonVariant cq = v["callback_query"];
            String cbId = String(cq["id"].as<const char*>());
            String data = String(cq["data"].as<const char*>());

            // chat id may be in cq["message"]["chat"]["id"] or from.id
            String chatId = cq["message"].isNull() ? String(cq["from"]["id"].as<long long>()) : String(cq["message"]["chat"]["id"].as<long long>());

            Serial.print("[Telegram] Callback from "); Serial.print(chatId); Serial.print(": "); Serial.println(data);

            // only accept callbacks from configured chat id
            if(config.telegramChatId.length() > 0 && chatId != config.telegramChatId)
            {
                lastUpdateId = updateId;
                continue;
            }

            data.trim();

            // process callback data as commands (same as text handlers)
            if(data.startsWith("/nuke"))
            {
                Serial.println("[Telegram] Callback /nuke received - starting irrigation");
                IrrigationManager::requestStart();
                EventLog::add("Remote: Start requested via Telegram");
                Telegram::send("▶️ ZanzNuke", "Irrigation start requested");
            }
            else if(data.startsWith("/stop"))
            {
                Serial.println("[Telegram] Callback /stop received - stopping irrigation");
                IrrigationManager::requestStop();
                EventLog::add("Remote: Stop requested via Telegram");
                Telegram::send("⏹️ ZanzNuke", "Stop requested");
            }
            else if(data.startsWith("/status"))
            {
                Serial.println("[Telegram] Callback /status received - sending status");
                IrrigationState state = IrrigationManager::state();
                ErrorCode errCode = SafetyManager::error();

                String s;
                s += "State: ";
                s += stateToString(state, IrrigationManager::isWashCycle());
                s += "\n";
                s += "Error: ";
                s += errorToString(errCode);
                s += "\n";
                s += "Tank: ";
                s += (Sensors::tankFull() ? "FULL" : (Sensors::tankEmpty() ? "EMPTY" : "OK"));
                s += "\n";
                s += "Pulses: ";
                s += String(FlowMeter::pulses());
                float liters = (config.pulsesPerLiter > 0.0f) ? (FlowMeter::pulses() / config.pulsesPerLiter) : 0.0f;
                s += "\nLiters: ";
                s += String(liters, 2);
                s += " / Target: ";
                s += String(IrrigationManager::targetLitersValue(), 2);
                s += " L\n";
                s += "Time: ";
                s += Clock::datetime();
                s += "\n";
                s += "IP: ";
                s += NetworkManager::ip();
                s += "\n";
                s += "Next: ";
                s += Scheduler::nextRunDescription();

                Telegram::send("ℹ️ ZanzNuke Status", s);
            }
            else if(data.startsWith("/clear"))
            {
                Serial.println("[Telegram] Callback /clear received - clearing faults");
                SafetyManager::requestClear();
                EventLog::add("Remote: Clear requested via Telegram");
                Telegram::send("✅ ZanzNuke", "Clear requested");
            }
            else if(data.startsWith("/schedule"))
            {
                Serial.println("[Telegram] Callback /schedule received - sending schedule");
                String out;
                const char* weekdays[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
                bool any = false;
                for(int i=0;i<MAX_SCHEDULES;i++)
                {
                    ScheduleEntry e = Scheduler::getEntry(i);
                    if(!e.enabled) continue;
                    any = true;
                    char buf[64];
                    snprintf(buf, sizeof(buf), "%s %02u:%02u\n", weekdays[e.weekday], e.hour, e.minute);
                    out += buf;
                }
                if(!any) out = "No scheduled runs";
                Telegram::send("📅 ZanzNuke Schedule", out);
            }

            // Answer the callback to remove client loading indicators
            if(cq["id"].is<const char*>())
            {
                String cbQId = String(cq["id"].as<const char*>());
                WiFiClientSecure c2;
                c2.setInsecure();
                HTTPClient h2;
                String ansUrl = "https://api.telegram.org/bot" + config.telegramBotToken + "/answerCallbackQuery";
                h2.begin(c2, ansUrl);
                h2.addHeader("Content-Type", "application/x-www-form-urlencoded");
                String abody = "callback_query_id=" + urlEncode(cbQId) + "&text=" + urlEncode("OK") + "&show_alert=false";
                int acode = h2.POST(abody);
                String aresp = h2.getString();
                if(acode <= 0 || (acode != 200 && acode != 201)) {
                    Serial.print("[Telegram] answerCallbackQuery failed: "); Serial.print(acode);
                    if(aresp.length() > 0) { Serial.print(" "); Serial.println(aresp); }
                    else Serial.println();
                }
                h2.end();
            }

            lastUpdateId = updateId;
            continue;
        }

        // message may be in v["message"] or v["edited_message"]; prefer message
        JsonVariant msg = v["message"].isNull() ? v["edited_message"] : v["message"];
        if(msg.isNull())
        {
            lastUpdateId = updateId;
            continue;
        }

        String chatId = String(msg["chat"]["id"].as<long long>());

        String text = msg["text"].as<const char*>();

        // If a user sends /start, send the keyboard to that chat but do NOT change the configured chat id
        if(text.startsWith("/start"))
        {
            Serial.println(" [Telegram] Text: /start");

            // send the persistent reply keyboard to the requesting chat without adopting it
            const String kb = R"KB({
    "keyboard": [
        [{"text":"/status 📊"},{"text":"/schedule 📅"}],
        [{"text":"/stop ⛔"},{"text":"/clear 🗑"}],
        [{"text":"/nuke ☢️"}]
    ],
    "resize_keyboard": true,
    "is_persistent": true
})KB";
            Telegram::sendKeyboardTo(chatId, kb);
            lastUpdateId = updateId;
            continue;
        }

        // /chatid: anyone can request their chat id
        if(text.startsWith("/chatid"))
        {
            Serial.println("[Telegram] Command /chatid received - replying with chat id");
            Telegram::sendTo(chatId, "ZanzNuke ChatID", chatId);
            lastUpdateId = updateId;
            continue;
        }

        // only accept commands from configured chat id
        if(config.telegramChatId.length() > 0 && chatId != config.telegramChatId)
        {
            lastUpdateId = updateId;
            continue;
        }
        if(text.length() == 0)
        {
            lastUpdateId = updateId;
            continue;
        }

        text.trim();

        // minimal: command-specific logs below

        if(text.startsWith("/menu"))
        {
            Serial.println("[Telegram] Command /menu received - sending keyboard");
            const String kb = R"KB({
    "keyboard": [
        [{"text":"/status 📊"},{"text":"/schedule 📅"}],
        [{"text":"/stop ⛔"},{"text":"/clear 🗑"}],
        [{"text":"/nuke ☢️"}]
    ],
    "resize_keyboard": true,
    "is_persistent": true
})KB";
            Telegram::sendKeyboard(kb);
            lastUpdateId = updateId;
            continue;
        }

            if(text.startsWith("/nuke"))
            {
                Serial.println("[Telegram] Command /nuke received - starting irrigation");
                IrrigationManager::requestStart();
                EventLog::add("Remote: Start requested via Telegram");
                Telegram::send("▶️ ZanzNuke", "Irrigation start requested");
            }
        else if(text.startsWith("/stop"))
        {
                Serial.println("[Telegram] Command /stop received - stopping irrigation");
                IrrigationManager::requestStop();
                EventLog::add("Remote: Stop requested via Telegram");
                Telegram::send("⏹️ ZanzNuke", "Stop requested");
        }
        else if(text.startsWith("/status"))
        {
            // build status text similar to /api/status
            IrrigationState state = IrrigationManager::state();
            ErrorCode errCode = SafetyManager::error();

            String s;
            s += "State: ";
            s += stateToString(state, IrrigationManager::isWashCycle());
            s += "\n";
            s += "Error: ";
            s += errorToString(errCode);
            s += "\n";
            s += "Tank: ";
            s += (Sensors::tankFull() ? "FULL" : (Sensors::tankEmpty() ? "EMPTY" : "OK"));
            s += "\n";
            s += "Pulses: ";
            s += String(FlowMeter::pulses());
            s += "\n";
            float liters = (config.pulsesPerLiter > 0.0f) ? (FlowMeter::pulses() / config.pulsesPerLiter) : 0.0f;
            s += "Liters: ";
            s += String(liters, 2);
            s += " / Target: ";
            s += String(IrrigationManager::targetLitersValue(), 2);
            s += " L\n";
            s += "Time: ";
            s += Clock::datetime();
            s += "\n";
            s += "IP: ";
            s += NetworkManager::ip();
            s += "\n";
            s += "Next: ";
            s += Scheduler::nextRunDescription();

            Telegram::send("ℹ️ ZanzNuke Status", s);
        }
            else if(text.startsWith("/clear"))
            {
                Serial.println("[Telegram] Command /clear received - clearing faults");
                SafetyManager::requestClear();
                EventLog::add("Remote: Clear requested via Telegram");
                Telegram::send("✅ ZanzNuke", "Clear requested");
            }
            else if(text.startsWith("/schedule"))
            {
                Serial.println("[Telegram] Command /schedule received - sending schedule");
                String out;
                const char* weekdays[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
                bool any = false;
                for(int i=0;i<MAX_SCHEDULES;i++)
                {
                    ScheduleEntry e = Scheduler::getEntry(i);
                    if(!e.enabled) continue;
                    any = true;
                    char buf[64];
                    snprintf(buf, sizeof(buf), "%s %02u:%02u\n", weekdays[e.weekday], e.hour, e.minute);
                    out += buf;
                }
                if(!any) out = "No scheduled runs";
                Telegram::send("📅 ZanzNuke Schedule", out);
            }

        lastUpdateId = updateId;
    }

}
