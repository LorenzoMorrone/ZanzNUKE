#include "Notification.h"
#include "Pushover.h"
#include "Telegram.h"
#include "Config.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

/*
 * All internet I/O (Telegram/Pushover sends, the Telegram getUpdates
 * poll) is blocking: a fresh TLS handshake + request takes 1-3 s, and
 * far longer on a flaky link. Doing that on the Arduino loop() task
 * delayed relay switching and flow-meter checks by seconds (a 1 L fill
 * overshooting to 1.3 L, timed relay tests running long). So every
 * network call now runs on this dedicated worker task and loop() only
 * ever touches the small mutex-protected queue below.
 *
 * Telegram.cpp / Pushover.cpp keep their own unsynchronised pending
 * lists; that is safe because after begin() they are only ever touched
 * from the worker task.
 */

struct QueuedNotification
{
    String title;
    String message;
};

constexpr uint8_t MAX_QUEUED = 10;

static QueuedNotification queued[MAX_QUEUED];
static uint8_t queuedCount = 0;

static SemaphoreHandle_t queueMutex = nullptr;
static TaskHandle_t workerTask = nullptr;
static volatile bool flushRequested = false;


static bool deliver(String title, String message)
{
    if(config.telegramEnabled) {
        // If title already seems emoji-prefixed, don't add another
        const char* knownPrefixes[] = {"▶️","⏹️","ℹ️","✅","📅","❗","⚠️","💧","🌊","🔔"};
        bool hasPrefix = false;
        for(auto &p : knownPrefixes) {
            if(title.startsWith(p)) { hasPrefix = true; break; }
        }

        if(!hasPrefix) {
            String tl = title;
            String low = tl;
            low.toLowerCase();
            if(low.indexOf("error") >= 0 || low.indexOf("err") >= 0 || low.indexOf("fail") >= 0)
                title = String("❗ ") + title;
            else if(low.indexOf("warn") >= 0)
                title = String("⚠️ ") + title;
            else if(low.indexOf("flow") >= 0 || low.indexOf("irrig") >= 0)
                title = String("💧 ") + title;
            else if(low.indexOf("status") >= 0 || low.indexOf("info") >= 0)
                title = String("ℹ️ ") + title;
            else
                title = String("🔔 ") + title;
        }

        return Telegram::send(title, message);
    } else {
        return Pushover::send(title, message);
    }
}


static void workerLoop(void *)
{
    for(;;)
    {
        bool have = false;
        QueuedNotification item;

        xSemaphoreTake(queueMutex, portMAX_DELAY);
        if(queuedCount > 0)
        {
            item = queued[0];
            for(uint8_t i = 1; i < queuedCount; i++)
                queued[i - 1] = queued[i];
            queuedCount--;
            have = true;
        }
        xSemaphoreGive(queueMutex);

        if(have)
        {
            deliver(item.title, item.message);
            continue;
        }

        if(flushRequested)
        {
            flushRequested = false;
            Pushover::flushPending();
            Telegram::flushPending();
        }

        // Self-throttled to one poll every 2 s; checks connectivity itself.
        Telegram::update();

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}


void Notification::begin()
{
    Pushover::begin();
    Telegram::begin();

    if(workerTask != nullptr) return;

    queueMutex = xSemaphoreCreateMutex();

    // Core 0 (next to the WiFi stack) keeps core 1 free for loop().
    // 16 KB stack: mbedTLS handshakes are stack hungry.
    xTaskCreatePinnedToCore(
        workerLoop,
        "notify",
        16384,
        nullptr,
        1,
        &workerTask,
        0
    );
}

bool Notification::send(String title, String message)
{
    // Before the worker exists (boot-time messages) there is no loop to
    // protect yet, so send directly.
    if(workerTask == nullptr)
        return deliver(title, message);

    xSemaphoreTake(queueMutex, portMAX_DELAY);

    if(queuedCount >= MAX_QUEUED)
    {
        for(uint8_t i = 1; i < MAX_QUEUED; i++)
            queued[i - 1] = queued[i];
        queuedCount = MAX_QUEUED - 1;
    }

    queued[queuedCount].title = title;
    queued[queuedCount].message = message;
    queuedCount++;

    xSemaphoreGive(queueMutex);

    return true;
}

void Notification::flushPending()
{
    if(workerTask == nullptr)
    {
        Pushover::flushPending();
        Telegram::flushPending();
        return;
    }

    flushRequested = true;
}

uint8_t Notification::pendingCount()
{
    return Pushover::pendingCount() + Telegram::pendingCount() + queuedCount;
}
