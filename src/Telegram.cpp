#include "Telegram.h"

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

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

enum class TelegramAction
{
    SendMessage,
    SendKeyboard,
    SendKeyboardTo,
    SendTo
};

struct TelegramQueueItem
{
    TelegramAction action;
    String title;
    String message;
    String chatId;
};

static QueueHandle_t telegramQueue = nullptr;
static TaskHandle_t telegramTaskHandle = nullptr;

static PendingNotification t_pending[MAX_PENDING];
static uint8_t t_pendingCount = 0;
static int32_t t_lastMessageId = 0;

static String urlEncode(const String &s);
static String htmlEscape(const String &s);
static bool t_sendNow(const String &title, const String &message);
static void telegramPollOnce();

static void telegramWorkerTask(void *arg)
{
    (void)arg;

    TelegramQueueItem item;

    for(;;)
    {
        if(xQueueReceive(telegramQueue, &item, pdMS_TO_TICKS(1000)) == pdTRUE)
        {
            if(
                WiFi.status() == WL_CONNECTED
                && config.telegramBotToken.length() > 0
            )
            {
                switch(item.action)
                {
                case TelegramAction::SendMessage:
                    t_sendNow(item.title, item.message);
                    break;

                case TelegramAction::SendKeyboard:
                    // actual keyboard send executes here, not on the
                    // irrigation loop task.
                    {
                        String kb = item.message;
                        kb.replace("\n", "");
                        kb.replace("\r", "");
                        while(kb.indexOf("  ") >= 0) kb.replace("  ", " ");
                        bool isInline = (kb.indexOf("\"inline_keyboard\"") >= 0);
                        if(isInline)
                        {
                            if(t_lastMessageId > 0)
                            {
                                WiFiClientSecure client;
                                client.setInsecure();
                                client.setTimeout(1500);
                                HTTPClient http;
                                http.setConnectTimeout(1500);
                                http.setTimeout(1500);
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
                            }
                            else
                            {
                                WiFiClientSecure client;
                                client.setInsecure();
                                client.setTimeout(1500);
                                HTTPClient http;
                                http.setConnectTimeout(1500);
                                http.setTimeout(1500);
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
                            }
                        }
                        else
                        {
                            WiFiClientSecure client;
                            client.setInsecure();
                            client.setTimeout(1500);
                            HTTPClient http;
                            http.setConnectTimeout(1500);
                            http.setTimeout(1500);
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
                            }
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
                        }
                    }
                    break;

                case TelegramAction::SendKeyboardTo:
                    {
                        String kb = item.message;
                        kb.replace("\n", "");
                        kb.replace("\r", "");
                        while(kb.indexOf("  ") >= 0) kb.replace("  ", " ");
                        bool isInline = (kb.indexOf("\"inline_keyboard\"") >= 0);
                        if(isInline)
                        {
                            WiFiClientSecure client;
                            client.setInsecure();
                            client.setTimeout(1500);
                            HTTPClient http;
                            http.setConnectTimeout(1500);
                            http.setTimeout(1500);
                            String sendUrl = "https://api.telegram.org/bot" + config.telegramBotToken + "/sendMessage";
                            http.begin(client, sendUrl);
                            http.addHeader("Content-Type", "application/x-www-form-urlencoded");
                            String body = "chat_id=" + urlEncode(item.chatId) + "&text=" + urlEncode("🦟") + "&reply_markup=" + urlEncode(kb);
                            int result = http.POST(body);
                            String resp = http.getString();
                            if(result <= 0 || (result != 200 && result != 201)) {
                                Serial.print("[Telegram] sendKeyboardTo (inline) failed: "); Serial.print(result);
                                if(resp.length() > 0) { Serial.print(" "); Serial.println(resp); }
                                else Serial.println();
                            }
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
                        }
                        else
                        {
                            WiFiClientSecure client;
                            client.setInsecure();
                            client.setTimeout(1500);
                            HTTPClient http;
                            http.setConnectTimeout(1500);
                            http.setTimeout(1500);
                            String sendUrl = "https://api.telegram.org/bot" + config.telegramBotToken + "/sendMessage";
                            http.begin(client, sendUrl);
                            http.addHeader("Content-Type", "application/x-www-form-urlencoded");
                            String body = "chat_id=" + urlEncode(item.chatId) + "&text=" + urlEncode("🦟") + "&reply_markup=" + urlEncode(kb);
                            int result = http.POST(body);
                            String resp = http.getString();
                            if(result <= 0 || (result != 200 && result != 201)) {
                                Serial.print("[Telegram] sendKeyboardTo failed: "); Serial.print(result);
                                if(resp.length() > 0) { Serial.print(" "); Serial.println(resp); }
                                else Serial.println();
                            }
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
                        }
                    }
                    break;

                case TelegramAction::SendTo:
                    {
                        WiFiClientSecure client;
                        client.setInsecure();
                        client.setTimeout(1500);
                        HTTPClient http;
                        http.setConnectTimeout(1500);
                        http.setTimeout(1500);
                        String url = "https://api.telegram.org/bot" + config.telegramBotToken + "/sendMessage";
                        http.begin(client, url);
                        http.addHeader("Content-Type", "application/x-www-form-urlencoded");
                        String titleEsc = htmlEscape(item.title);
                        String timeEsc = htmlEscape(Clock::datetime());
                        String bodyEsc = htmlEscape(item.message);
                        String textHtml = String("<b>") + titleEsc + "</b>\n" + String("<i>") + timeEsc + "</i>\n" + bodyEsc;
                        String body = "chat_id=" + urlEncode(item.chatId) + "&text=" + urlEncode(textHtml) + "&parse_mode=HTML";
                        int result = http.POST(body);
                        String resp = http.getString();
                        if(result <= 0 || (result != 200 && result != 201)) {
                            Serial.print("[Telegram] sendTo failed: "); Serial.print(result);
                            if(resp.length() > 0) { Serial.print(" "); Serial.println(resp); }
                            else Serial.println();
                        }
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
                                if(item.chatId == config.telegramChatId)
                                    t_lastMessageId = rdoc["result"]["message_id"].as<int32_t>();
                            }
                        }
                        http.end();
                    }
                    break;
                }
            }
            else
            {
                xQueueSendToFront(telegramQueue, &item, 0);
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }
        }

        if(
            NetworkManager::connected()
            && config.telegramEnabled
            && config.telegramBotToken.length() > 0
        )
        {
            telegramPollOnce();
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

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
    client.setTimeout(1500);

    HTTPClient http;
    http.setConnectTimeout(1500);
    http.setTimeout(1500);

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
        // Avoid blocking the main loop with long retry sleeps while
        // a fill/test cycle is already running. The message is still
        // queued for the next successful attempt instead of stalling
        // irrigation logic here.
        Serial.print("[Telegram] POST failed, WiFi status: "); Serial.println(WiFi.status());
        Serial.print("[Telegram] Local IP: "); Serial.println(WiFi.localIP().toString());
        Serial.print("[Telegram] send failed: "); Serial.print(result);
        if(resp.length() > 0) {
            Serial.print(" "); Serial.println(resp);
        } else Serial.println();
    }

    return result == 200 || result == 201;
}

bool Telegram::send(String title, String message)
{
    if(config.telegramBotToken.length() == 0 || config.telegramChatId.length() == 0)
        return false;

    if(telegramQueue == nullptr)
        Telegram::begin();

    TelegramQueueItem item;
    item.action = TelegramAction::SendMessage;
    item.title = title;
    item.message = message;
    item.chatId = config.telegramChatId;

    return xQueueSend(telegramQueue, &item, 0) == pdTRUE;
}

bool Telegram::sendKeyboard(const String &keyboardJson)
{
    if(config.telegramBotToken.length() == 0 || config.telegramChatId.length() == 0)
        return false;

    if(telegramQueue == nullptr)
        Telegram::begin();

    TelegramQueueItem item;
    item.action = TelegramAction::SendKeyboard;
    item.title = "🦟";
    item.message = keyboardJson;
    item.chatId = config.telegramChatId;

    return xQueueSend(telegramQueue, &item, 0) == pdTRUE;
}

bool Telegram::sendKeyboardTo(const String &chatId, const String &keyboardJson)
{
    if(config.telegramBotToken.length() == 0)
        return false;

    if(telegramQueue == nullptr)
        Telegram::begin();

    TelegramQueueItem item;
    item.action = TelegramAction::SendKeyboardTo;
    item.title = "🦟";
    item.message = keyboardJson;
    item.chatId = chatId;

    return xQueueSend(telegramQueue, &item, 0) == pdTRUE;
}

bool Telegram::sendTo(const String &chatId, String title, String message)
{
    if(config.telegramBotToken.length() == 0) return false;

    if(telegramQueue == nullptr)
        Telegram::begin();

    TelegramQueueItem item;
    item.action = TelegramAction::SendTo;
    item.title = title;
    item.message = message;
    item.chatId = chatId;

    return xQueueSend(telegramQueue, &item, 0) == pdTRUE;
}

void Telegram::begin()
{
    if(telegramQueue != nullptr)
        return;

    telegramQueue = xQueueCreate(8, sizeof(TelegramQueueItem));
    if(telegramQueue == nullptr)
        return;

    xTaskCreatePinnedToCore(
        telegramWorkerTask,
        "tg_worker",
        8192,
        nullptr,
        1,
        &telegramTaskHandle,
        1
    );
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


static void telegramPollOnce()
{
    static int64_t lastUpdateId = 0;
    static uint32_t lastPollMs = 0;

    uint32_t now = millis();
    if(now - lastPollMs < 5000) return;
    lastPollMs = now;

    if(!NetworkManager::connected()) return;
    if(!config.telegramEnabled) return;
    if(config.telegramBotToken.length() == 0) return;

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(1500);

    HTTPClient http;
    http.setConnectTimeout(1500);
    http.setTimeout(1500);

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

        if(!v["callback_query"].isNull())
        {
            JsonVariant cq = v["callback_query"];
            String data = String(cq["data"].as<const char*>());

            String chatId = cq["message"].isNull() ? String(cq["from"]["id"].as<long long>()) : String(cq["message"]["chat"]["id"].as<long long>());
            time_t nowTs = time(nullptr);
            long msgDate = 0;
            if(!cq["message"].isNull() && cq["message"]["date"].is<long long>()) msgDate = cq["message"]["date"].as<long long>();
            else if(cq["date"].is<long long>()) msgDate = cq["date"].as<long long>();
            if(msgDate > 0 && (nowTs - msgDate) > 60) { lastUpdateId = updateId; continue; }

            data.trim();
            if(config.telegramChatId.length() > 0 && chatId != config.telegramChatId)
            {
                lastUpdateId = updateId;
                continue;
            }

            if(data.startsWith("/nuke"))
            {
                IrrigationManager::requestStart();
                EventLog::add("Remote: Start requested via Telegram");
                Telegram::send("▶️ ZanzNuke", "Irrigation start requested");
            }
            else if(data.startsWith("/stop"))
            {
                IrrigationManager::requestStop();
                EventLog::add("Remote: Stop requested via Telegram");
                Telegram::send("⏹️ ZanzNuke", "Stop requested");
            }
            else if(data.startsWith("/status"))
            {
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
                SafetyManager::requestClear();
                EventLog::add("Remote: Clear requested via Telegram");
                Telegram::send("✅ ZanzNuke", "Clear requested");
            }
            else if(data.startsWith("/schedule"))
            {
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
                h2.getString();
                h2.end();
            }

            lastUpdateId = updateId;
            continue;
        }

        JsonVariant msg = v["message"].isNull() ? v["edited_message"] : v["message"];
        if(msg.isNull())
        {
            lastUpdateId = updateId;
            continue;
        }

        String chatId = String(msg["chat"]["id"].as<long long>());
        time_t nowTs = time(nullptr);
        long msgDate = 0;
        if(msg["date"].is<long long>()) msgDate = msg["date"].as<long long>();
        if(msgDate > 0 && (nowTs - msgDate) > 60) { lastUpdateId = updateId; continue; }

        String text = msg["text"].as<const char*>();
        if(text.startsWith("/start"))
        {
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

        if(text.startsWith("/chatid"))
        {
            Telegram::sendTo(chatId, "ZanzNuke ChatID", chatId);
            lastUpdateId = updateId;
            continue;
        }

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

        if(text.startsWith("/menu"))
        {
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
            IrrigationManager::requestStart();
            EventLog::add("Remote: Start requested via Telegram");
            Telegram::send("▶️ ZanzNuke", "Irrigation start requested");
        }
        else if(text.startsWith("/stop"))
        {
            IrrigationManager::requestStop();
            EventLog::add("Remote: Stop requested via Telegram");
            Telegram::send("⏹️ ZanzNuke", "Stop requested");
        }
        else if(text.startsWith("/status"))
        {
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
            SafetyManager::requestClear();
            EventLog::add("Remote: Clear requested via Telegram");
            Telegram::send("✅ ZanzNuke", "Clear requested");
        }
        else if(text.startsWith("/schedule"))
        {
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

void Telegram::update()
{
    // All network I/O is handled asynchronously by the background
    // worker task, so the main loop stays responsive while irrigation
    // and safety logic runs without delays.
}
