#include "em_time.h"

bool EmTime::begin(const EmDuration& initTimeout,
                   const char* tz,
                   const char* ntpServer1,
                   const char* ntpServer2,
                   const char* ntpServer3) {
    // already stared?
    if (s_isStarted) {
        return s_isInitialized;
    }

    // Initialize time handling
    s_isStarted = true;
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
    esp_sntp_init();

    // Waiting loop... note that s_isInitialized is set in the callback 
    EmTimeout timeout(initTimeout);
    while (!s_isInitialized && !timeout.isExpired()) {
        tDelay(100);
    }    
    return s_isInitialized;
}