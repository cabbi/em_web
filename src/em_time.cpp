#include "em_time.h"

bool EmTime::begin(const EmDuration& initTimeout,
                   const char* tz,
                   const char* ntpServer1,
                   const char* ntpServer2,
                   const char* ntpServer3) {
    if (!setup_(tz, ntpServer1, ntpServer2, ntpServer3)) {
        return false;
    }

    // Starts the connection without checking WiFi status.
    start_();

    // Waiting loop... note that s_isInitialized is set in the callback 
    EmTimeout timeout(initTimeout);
    while (!s_isInitialized && !timeout.isExpired()) {
        tDelay(100, true);
    }    
    return s_isInitialized;
}

bool EmTime::begin(const char* tz,
                   const char* ntpServer1,
                   const char* ntpServer2,
                   const char* ntpServer3) {
    if (!setup_(tz, ntpServer1, ntpServer2, ntpServer3)) {
        return false;
    }
    
    if (EmWiFi::isConnected()) {
        start_();
    } else {
        EmWiFi::addEventHandler(s_wifiEventHandler);
    }
    return true;
}

EmWiFiEventResult EmTime::wifiEventCallback_(void* userArg, EmWiFiEventType event) {
    if (event == EmWiFiEventType::connected) {
        start_();
        // No notifications needed anymore
        return EmWiFiEventResult::removeHandler;
    }
    // Waiting the WiFi connection callback
    return EmWiFiEventResult::none;
}

bool EmTime::setup_(const char* tz,
                    const char* ntpServer1,
                    const char* ntpServer2,
                    const char* ntpServer3) {
    // already stared?
    if (s_isInitialized) {
        logWarning("EmTime", "EmTime already initialized!");
        return false;
    }
    s_isInitialized = true;
    // Initialize time handling
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    if (ntpServer1 != nullptr) {
        esp_sntp_setservername(0, ntpServer1);
    }
    if (ntpServer2 != nullptr) {
        esp_sntp_setservername(1, ntpServer2);
    }
    if (ntpServer3 != nullptr) {
        esp_sntp_setservername(2, ntpServer3);
    }    
    sntp_set_time_sync_notification_cb(EmTime::timeSyncNotificationCallback_);
    return true;
}

bool EmTime::start_() {
    if (s_isStarted) {
        logWarning("EmTime", "EmTime already started!");
        return false;
    }
    esp_sntp_init();
    s_isStarted = true;
    logInfo("EmTime", "Time handler started. Waiting for NTP sync...");
    return true;
}