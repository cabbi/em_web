#ifndef __EM_TIME_H
#define __EM_TIME_H   

#include "em_defs.h"

#ifdef EM_TIME

#include <time.h>
#include <esp_timer.h>
#include <esp_sntp.h>

#include "em_log.h"
#include "em_epoch.h"
#include "em_timeout.h"
#include "em_threading.h"
#include "em_duration.h"
#include "em_wifi.h"

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
        return s_isStarted.load();
    }

    static bool isInitialized() {
        return s_isInitialized.load();
    }

    static void setTimeZone(const char* tz) {
        setenv("TZ", tz == nullptr ? "" : tz , 1);
        tzset();
    }

    // Get the current utc time in seconds since epoch
    template<typename T>
    static bool nowUtc(EmEpochBase<T>& currentTime) {
        if (isInitialized()) {
            currentTime.value = static_cast<T>(time(nullptr));
            return true;
        }
        return false;
    }

    // Get the current utc time in milliseconds since epoch
    static bool nowUtcMs(EmEpochAsMillisec& currentTimeMs) {
        if (isInitialized()) {
            currentTimeMs = EmEpochAsMillisec(time(nullptr) * 1000);
            return true;
        }
        return false;
    }

    // Get the current local time in seconds since epoch
    template<typename T>
    static bool nowLocal(EmEpochBase<T>& currentTime) {
        if (isInitialized()) {
            time_t now;
            struct tm tm;
            time(&now);
            localtime_r(&now, &tm);
            currentTime.value = static_cast<T>(mktime(&tm));
            return true;
        }
        return false;
    }

    // Get the current local time in milliseconds since epoch
    static bool nowLocalMs(EmEpochAsMillisec& currentTimeMs) {
        if (isInitialized()) {
            time_t now;
            struct tm tm;
            time(&now);
            localtime_r(&now, &tm);
            currentTimeMs = EmEpochAsMillisec(mktime(&tm) * 1000);
            return true;
        }
        return false;
    }
    
    // Get the current local time as a struct tm based on set timezone (if any)
    static bool getUtcTime(struct tm& timeinfo) {
        if (isInitialized()) {
            time_t now = time(nullptr);
            gmtime_r(&now, &timeinfo);
            return true;
        }
        return false;
    }

    // Get the current local time as a struct tm based on set timezone (if any)
    static bool getLocalTime(struct tm& timeinfo) {
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
        // Checking for a valid year as well since some other sw components might have set the time
        time_t now = tv->tv_sec;
        struct tm t;
        gmtime_r(&now, &t);
        if (t.tm_year > 120 && sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
            logInfo<100>("EmTime", "Time set to: %d-%02d-%02d %02d:%02d:%02d", 
                          t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                          t.tm_hour, t.tm_min, t.tm_sec);
            s_isInitialized.store(true);
        }
    }
    
    static EmWiFiEventResult wifiEventCallback_(void* userArg, EmWiFiEventType event);
    
    // Member vars
    inline static ts_bool s_isStarted = false;
    inline static ts_bool s_isInitialized = false;
    inline static EmWiFiEventHandler s_wifiEventHandler = {wifiEventCallback_, nullptr};
};

#endif
#endif // EM_TIME_H