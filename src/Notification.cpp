#include "Notification.h"
#include "Pushover.h"
#include "Telegram.h"
#include "Config.h"

void Notification::begin()
{
    Pushover::begin();
    Telegram::begin();
}

bool Notification::send(String title, String message)
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

void Notification::flushPending()
{
    Pushover::flushPending();
    Telegram::flushPending();
}

uint8_t Notification::pendingCount()
{
    return Pushover::pendingCount() + Telegram::pendingCount();
}
