#ifndef __EM_TIME_H
#define __EM_TIME_H   

#include "em_defs.h"

#ifdef EM_TIME

#include <time.h>
#include <esp_timer.h>
#include <esp_sntp.h>

#include "em_log.h"
#include "em_timeout.h"
#include "em_threading.h"
#include "em_duration.h"
#include "em_wifi.h"

using EmEpochTypeSec = uint32_t;
using EmEpochTypeMilli = uint64_t;

// EmTime class for handling time-related operations.
// Class has only static methods since it cannot have multiple instances.
class EmTime {
public:
    // Begins the time management by configuring the NTP server and time zone
    // waiting for a timeout. Call this method if you are connected to WiFi.  
    static bool begin(const EmDuration& initTimeout,
                      const char* tz = nullptr,
                      const char* ntpServer1 = "pool.ntp.org",
                      const char* ntpServer2 = "time.nist.gov",
                      const char* ntpServer3 = nullptr);

    // Begins the time management by configuring the NTP server and time zone
    // waiting for EmWiFi callback once connected to the internet.  
    static bool begin(const char* tz = nullptr,
                      const char* ntpServer1 = "pool.ntp.org",
                      const char* ntpServer2 = "time.nist.gov",
                      const char* ntpServer3 = nullptr);

    static bool isStarted() {
        return s_isStarted;
    }

    static bool isInitialized() {
        return s_isInitialized;
    }

    // Get the current time in seconds since epoch
    static bool now(EmEpochTypeSec& currentTime) {
        if (isInitialized()) {
            currentTime = static_cast<EmEpochTypeSec>(time(nullptr));
            return true;
        }
        return false;
    }

    // Get the current time in milliseconds since epoch
    static bool nowMs(EmEpochTypeMilli& currentTimeMs) {
        if (isInitialized()) {
            currentTimeMs = static_cast<EmEpochTypeMilli>(time(nullptr) * 1000);
            return true;
        }
        return false;
    }
    
    // Get the current time as a struct tm
    static bool getTime(struct tm& timeinfo) {
        if (isInitialized()) {
            time_t now = time(nullptr);
            localtime_r(&now, &timeinfo);
            return true;
        }
        return false;
    }

    // Get the up time since the device stated
    static EmLowResDuration getDeviceUpTime() {
        int64_t uptime_us = esp_timer_get_time();
        return EmLowResDuration(static_cast<uint32_t>(uptime_us/1000000));
    }    

private:
    static bool setup_(const char* tz = nullptr,
                       const char* ntpServer1 = "pool.ntp.org",
                       const char* ntpServer2 = "time.nist.gov",
                       const char* ntpServer3 = nullptr);
    static bool start_();
    static void timeSyncNotificationCallback_(struct timeval *tv) {
        s_isInitialized = true;
    }
    
    static EmWiFiEventResult wifiEventCallback_(void* userArg, EmWiFiEventType event);
    
    // Member vars
    inline static ts_bool s_isStarted = false;
    inline static ts_bool s_isInitialized = false;
    inline static EmWiFiEventHandler s_wifiEventHandler = {wifiEventCallback_, nullptr};
};

#endif
#endif // EM_TIME_H