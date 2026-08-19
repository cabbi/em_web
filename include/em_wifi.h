#ifndef __EM_WIFI_H__
#define __EM_WIFI_H__

#include <atomic>
#include <vector>
#include <algorithm>

#include "esp_wifi.h"

#include "em_log.h"
#include "em_string.h"
#include "em_duration.h"
#include "em_threading.h"


enum class EmWiFiLevel: uint8_t {
    notConnected = 0,
    veryWeak     = 1,
    weak         = 2,
    fair         = 3,
    good         = 4,
    excellent    = 5
};

inline EmWiFiLevel getWiFiLevel(int8_t rssi) {
    if (rssi <= -100 || rssi >= 0) return EmWiFiLevel::notConnected;
    if (rssi >= -50) return EmWiFiLevel::excellent;
    if (rssi >= -60) return EmWiFiLevel::good;
    if (rssi >= -70) return EmWiFiLevel::fair;
    if (rssi >= -80) return EmWiFiLevel::weak;
    return EmWiFiLevel::veryWeak;  
}

inline const char* getWiFiLevelName(EmWiFiLevel level) {
    switch(level) {
        case EmWiFiLevel::notConnected: return "Not connected";
        case EmWiFiLevel::veryWeak:     return "Very weak";
        case EmWiFiLevel::weak:         return "Weak";
        case EmWiFiLevel::fair:         return "Fair";
        case EmWiFiLevel::good:         return "Good";
        case EmWiFiLevel::excellent:    return "Excellent";
    }
    return "Unknown";
}

struct EmWiFiCredential {
    EmWiFiCredential() {}
    EmWiFiCredential(const char* ssid_val, const char* pwd_val)
     : ssid(ssid_val), password(pwd_val) {}

    EmWiFiCredential(const EmWiFiCredential& other) {
        ssid.set(other.ssid);
        password.set(other.password);
    }
    
    EmWiFiCredential& operator=(const EmWiFiCredential& other) {
        ssid.set(other.ssid); 
        password.set(other.password);
        return *this;
    }

    bool isEmpty() const {
        return ssid.isEmpty();
    }

    EmStringS ssid;
    EmStringS password;
};


// WiFi event types
enum class EmWiFiEventType: uint8_t {
    connected = 0,
    disconnected = 1,
    scanBegin = 2,
    scanEnd = 2,
};

// WiFi Power Save mode
enum class EmWiFiPsMode: uint8_t {
    none = WIFI_PS_NONE,
    min = WIFI_PS_MIN_MODEM,
    max = WIFI_PS_MAX_MODEM,
};    

// WiFi event callback result
enum class EmWiFiEventResult: uint8_t {
    none = 0,
    removeHandler = 1
};

// WiFi antenna type (if supported!)
enum class EmWiFiAntennaType: uint8_t {
    internal = 0,
    external = 1
};

// WiFi event callback prototype
typedef EmWiFiEventResult (*EmWiFiEventCallback)(void* userArg, EmWiFiEventType type);

// WiFi event data                                    
struct EmWiFiEventHandler {
    EmWiFiEventHandler(EmWiFiEventCallback callback, void* userArg)
     : callback(callback), userArg(userArg) {}

    EmWiFiEventCallback callback;
    void* userArg;       
};

// NOTE:
// Define 'EM_XIAO_C6' in case you're using a 'Seeed XIAO C6' board
// in order to use 'switchAntenna' method


// This class manages Wi-Fi connections and orchestrates scanning and  
// connecting to the best available network from a pool of credentials.
class EmWiFi {
public:
    EmWiFi() = delete;   

    static void init(EmWiFiPsMode psMode = EmWiFiPsMode::none, 
                     EmWiFiAntennaType antennaType=EmWiFiAntennaType::internal);

    static bool switchAntenna(EmWiFiAntennaType antennaType);


    // Add a new network configuration to the AP list.
    // Max 128 APs
    static bool addAP(const char* ssid, const char* passphrase);
    
    // Resets the defined APs
    static void resetApPool() {
        EmMutexLock lock(m_networkMutex);
        m_networks.clear();
        m_currentSsid.clear();
    }
    
    // Returns the number of defined APs
    static int8_t getApCount() {
        // No need to block this networks read operation
        return static_cast<int8_t>(m_networks.size());
    }

    // Add a new event handler to the WiFi object.
    // NOTE: keep the event handler callback execution fast, since it blocks other event handlers!
    static void addEventHandler(EmWiFiEventHandler handler) {
        EmMutexLock lock(m_eventsMutex);
        m_eventHandlers.push_back(handler);
    }

    // Clears the event handlers pool
    static void clearEventHandlers(EmWiFiEventHandler handler) {
        EmMutexLock lock(m_eventsMutex);
        m_eventHandlers.clear();
    }

    // Start the WiFi connection check loop.
    // The loop will check each 'checkIntervalSec' if WiFi is 
    // not connected or the level is equal or below the 'minCheckLevel'.  
    static bool startConnectionLoop(uint16_t checkIntervalSec = 60,
                                    EmWiFiLevel minCheckLevel = EmWiFiLevel::fair);
    
    // Stops the WiFi connection check loop
    static void stopConnectionLoop();

    // Connects to an Access Point
    static void connect(const char* ssid, 
                        const char* password, 
                        EmDuration waitTime = EmDuration(3000));
    
    // Disconnects from current Access Point 
    static void disconnect();

    static bool isInitialized() {
        EmMutexLock lock(m_initMutex);
        return m_netif != nullptr;
    }

    static bool isConnectionLoopRunning() {
        return m_taskHandle != nullptr;
    }

    static bool isConnected() {
        return m_connected;
    }

    static bool isNotConnected() {
        return !EmWiFi::isConnected();
    }

    static const char* getSsid(EmStringS& ssid) {
        getCurrentSsid_(ssid);
        return ssid.c_str();
    }   

    static int8_t getRssi() {
        if (!EmWiFi::isConnected()) {
            return 0;
        }
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            return ap_info.rssi;
        }
        return 0;
    }

    static EmWiFiLevel getWiFiLevel() {
        if (!EmWiFi::isConnected()) {
            return EmWiFiLevel::notConnected;
        }
        return ::getWiFiLevel(getRssi());
    }

    static const char* getWiFiLevelName() {
        if (!EmWiFi::isConnected()) {
            return "Not connected";
        }
        return ::getWiFiLevelName(getWiFiLevel());
    }
    
private:
    static bool getBestNetwork_(EmWiFiCredential& bestNetwork);
    static void clearCurrentSsid_() {
        EmMutexLock lock(m_networkMutex);
        m_currentSsid.clear();
    }
    static void setCurrentSsid_(const EmStringS& ssid) {
        EmMutexLock lock(m_networkMutex);
        m_currentSsid.set(ssid);
    }
    static void getCurrentSsid_(EmStringS& ssid) {
        EmMutexLock lock(m_networkMutex);
        ssid.set(m_currentSsid);
    }
    static bool isCurrentSsid_(const EmStringS& ssid) {
        EmMutexLock lock(m_networkMutex);
        return m_currentSsid == ssid;
    }

    static void raiseEvent_(EmWiFiEventType type) {
        EmMutexLock lock(m_eventsMutex);
        m_eventHandlers.erase(
            std::remove_if(m_eventHandlers.begin(), 
                           m_eventHandlers.end(), 
                           [type](EmWiFiEventHandler handler) {
                return handler.callback(handler.userArg, type) == EmWiFiEventResult::removeHandler;
            }), 
            m_eventHandlers.end()
        );        
    }

    static void eventHandler_(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
    static void wifiTaskCore_(void* pvParameters);

    inline static EmMutex m_initMutex;
    inline static esp_netif_t* m_netif = nullptr;
    inline static EmStringS m_currentSsid;

    inline static EmMutex m_networkMutex;
    inline static std::vector<EmWiFiCredential> m_networks;

    inline static EmMutex m_eventsMutex;
    inline static std::vector<EmWiFiEventHandler> m_eventHandlers;
    inline static std::atomic<TaskHandle_t> m_taskHandle = nullptr;

    inline static std::atomic<uint16_t> m_checkIntervalSec = 0;
    inline static std::atomic<EmWiFiLevel> m_checkLevel = EmWiFiLevel::notConnected;
    inline static std::atomic<bool> m_connected = false;
};

#endif
