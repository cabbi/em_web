#ifndef __EM_WIFI_H__
#define __EM_WIFI_H__

#include <atomic>
#include <vector>
#include <algorithm>
#include <type_traits>

#include "esp_wifi.h"

#include "em_log.h"
#include "em_net.h"
#include "em_string.h"
#include "em_timeout.h"
#include "em_duration.h"
#include "em_threading.h"
#include "em_json_reader.h"
#include "em_json_writer.h"


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

// Limit of user defined APs 
#define WIFI_MAX_AP_CREDENTIALS 20

// WiFi Access Point Credential structure.
// The optional 'keepMeThreshold' is used to determine if the current connection 
// should be kept or if the system should try to connect to another APs.
// The use-case (which is mine real one) is when some APs might not have weak/intermittent 
// signal but others are connection with few data/moth plan (e.g. IoT SIMs).
// Setting it to 'EmWiFiLevel::notConnected' will always try to connect to another AP.
// The 'noInternetFreezeTimeout' is used to avoid trying to connect to an AP that has no internet connection.
struct EmWiFiApCredential {
    EmWiFiApCredential() {}
    EmWiFiApCredential(const char* ssid, 
                       const char* pwd)
     : ssid(ssid), password(pwd) {}
    EmWiFiApCredential(const char* ssid, 
                       const char* pwd,
                       EmWiFiLevel keepMeThreshold)
     : ssid(ssid), password(pwd), 
       keepMeThreshold(keepMeThreshold) {}
    EmWiFiApCredential(const char* ssid, 
                       const char* pwd,
                       EmWiFiLevel keepMeThreshold,
                       EmDuration noInternetFreeze)
     : ssid(ssid), password(pwd), 
       keepMeThreshold(keepMeThreshold), 
       noInternetFreezeTimeout(noInternetFreeze) {}

    EmWiFiApCredential(const EmWiFiApCredential& other) {
        ssid.set(other.ssid);
        password.set(other.password);
        keepMeThreshold = other.keepMeThreshold;
        noInternetFreezeTimeout = other.noInternetFreezeTimeout;
    }
    
    EmWiFiApCredential& operator=(const EmWiFiApCredential& other) {
        ssid.set(other.ssid); 
        password.set(other.password);
        keepMeThreshold = other.keepMeThreshold;
        noInternetFreezeTimeout = other.noInternetFreezeTimeout;
        return *this;
    }

    bool isEmpty() const {
        return ssid.isEmpty();
    }

    // True if the WiFi credential is connectable 
    // (i.e. if it doesn't have internet connection issues already detected in previous connection) 
    bool isConnectable() const {
        return noInternetFreezeTimeout.isExpired();
    }

    bool keepMe() const;
    bool keepMe(int rssi) const {
        if (keepMeThreshold == EmWiFiLevel::notConnected) {
            return false;
        }
        return getWiFiLevel(rssi) >= keepMeThreshold;
    }

    bool checkInternetConnection(EmNetInternetCheckType checkType);
    bool setInternetConnection(bool hasInternetConnection);

    void toJson(EmJsonWriter& apsJson) const;
    bool fromJson(const EmJsonDictReader& apsJson);

    EmStringS ssid;
    EmStringS password;
    // If the current connection level is equal or above this threshold, 
    // keep the connection and don't try to connect to another APs
    EmWiFiLevel keepMeThreshold = EmWiFiLevel::notConnected;
    // If this timeout is not expired, the connection loop will not try to connect to another APs
    EmTimeout noInternetFreezeTimeout = EmTimeout(EmDuration(0, 5, 0), true); // 5 minutes, elapsed
};

              
// WiFi event types
enum class EmWiFiEventType: uint8_t {
    connected = 0,
    disconnected = 1,
    scanBegin = 2,
    scanEnd = 3,
};

// WiFi Power Save mode
enum class EmWiFiPsMode: uint8_t {
    none = WIFI_PS_NONE,
    min = WIFI_PS_MIN_MODEM,
    max = WIFI_PS_MAX_MODEM,
};    

// WiFi antenna type (if supported!)
enum class EmWiFiAntennaType: uint8_t {
    internal = 0,
    external = 1
};

// WiFi event callback prototype
// NOTE:
// Call 'setMarkedForRemoval' within the call back if you want to remove the handler.
typedef void (*EmWiFiEventCallback)(void* userArg, EmWiFiEventType type);

// WiFi event handler
struct EmWiFiEventHandler {
    EmWiFiEventHandler(EmWiFiEventCallback callback, void* userArg)
     : callback(callback), userArg(userArg), 
       m_id(++m_nextHandlerId), m_markedForRemoval(false) {}

    EmWiFiEventCallback callback;
    void* userArg;

    uint32_t id() const { return m_id; }

    void setMarkedForRemoval() {
        m_markedForRemoval = true;
    }

    bool markedForRemoval() const {
        return m_markedForRemoval;
    }

protected:
    uint32_t m_id;
    bool m_markedForRemoval;
    static std::atomic<uint32_t> m_nextHandlerId;
};


// NOTE:
// -----
// Define 'EM_XIAO_C6' in case you're using a 'Seeed XIAO C6' board
// in order to use 'switchAntenna' method


// This class manages Wi-Fi connections and orchestrates scanning and  
// connecting to the best available network from a pool of credentials.
class EmWiFi {
public:
    EmWiFi() = delete;   

    static void init(EmWiFiPsMode powerSaveMode=EmWiFiPsMode::none, 
                     EmWiFiAntennaType antennaType=EmWiFiAntennaType::internal,
                     const char* hostname=nullptr,
                     EmNetInternetCheckType internetCheckType=EmNetInternetCheckType::udp,
                     EmDuration internetCheckTimeout=EmDuration(0, 5, 0)); // 5 minutes

    static bool switchAntenna(EmWiFiAntennaType antennaType);


    // Add a new network configuration to the AP list.
    static bool addAP(const EmWiFiApCredential& ap);
    static bool addAP(const char* ssid, 
                      const char* pwd,
                      EmWiFiLevel keepMeThreshold = EmWiFiLevel::notConnected, // No threshold
                      EmDuration noInternetFreeze = EmDuration(0, 5, 0)) {     // 5 minutes timeout
        return addAP(EmWiFiApCredential(ssid, pwd, keepMeThreshold, noInternetFreeze));
    }

    
    // Resets the defined APs
    static void resetApPool() {
        EmMutexLock lock(m_networkMutex);
        m_networks.clear();
        m_currentSsid.clear();
    }
    
    // Returns the number of defined APs
    static int8_t getApCount() {
        EmMutexLock lock(m_networkMutex);
        return static_cast<int8_t>(m_networks.size());
    }

    // Add a new event handler to the WiFi object.
    // NOTES:
    //  - Call 'setMarkedForRemoval' within the call back if you want to remove
    //    the handler within the event callback.
    //  - Keep the event handler callback execution fast, since it blocks other event handlers!
    static void addEventHandler(EmWiFiEventHandler handler) {
        EmMutexLock lock(m_eventsMutex);
        m_eventHandlers.push_back(handler);
    }

    // Removes an existing event handler from the WiFi object.
    static void removeEventHandler(const EmWiFiEventHandler& handler) {
        removeEventHandler(handler.id());
    }
    static void removeEventHandler(uint32_t id) {
        EmMutexLock lock(m_eventsMutex);
        m_eventHandlers.erase(
            std::remove_if(
                m_eventHandlers.begin(),
                m_eventHandlers.end(),
                [id](const EmWiFiEventHandler& h)
                {
                    return h.id() == id;
                }),
            m_eventHandlers.end());
    }

    // Clears the event handlers pool
    static void clearEventHandlers() {
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
                        EmDuration waitTime = EmDuration(1000));
    
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

    static bool hasInternetConnection() {
        if (m_internetCheckTimeout.isExpired(true)) {
            m_hasInternetConnection = EmNet::checkInternetConnection(m_internetCheckType);
        }
EmStringS ssid; // CABBI 
getCurrentSsid_(ssid);
logInfo<100>("EmWiFi", "AP '%s' has%s internet connection!", 
ssid.c_str(),
m_hasInternetConnection ? "" : " NO"); 
        return m_hasInternetConnection;
    }

    static bool hasNoInternetConnection() {
        return !hasInternetConnection();
    }

    static const char* getSsid(EmStringS& ssid) {
        getCurrentSsid_(ssid);
        return ssid.c_str();
    }   

    // Returns 0 if not conected of the negative RSSI db value of the current connection.
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
    
protected:
    static bool getBestNetwork_(EmWiFiApCredential& bestNetwork);
    static void startApConnectionLoop_() {
        // Notify the connection loop to check for another AP immediately
        xTaskNotifyGive(EmWiFi::m_taskHandle); 
    }
    static void clearCurrentSsid_() {
        EmMutexLock lock(m_networkMutex);
        m_currentSsid.clear();
    }
    static void setCurrentSsid_(const EmStringBase& ssid) {
        setCurrentSsid_(ssid.c_str());
    }
    static void setCurrentSsid_(const char* ssid) {
        EmMutexLock lock(m_networkMutex);
        m_currentSsid.set(ssid);
    }
    static bool getCurrentSsid_(EmStringBase& ssid) {
        EmMutexLock lock(m_networkMutex);
        ssid.set(m_currentSsid);
        return ssid.isNotEmpty();
    }
    static bool isCurrentSsid_(const EmStringBase& ssid) {
        return isCurrentSsid_(ssid.c_str());
    }
    static bool isCurrentSsid_(const char* ssid) {
        EmMutexLock lock(m_networkMutex);
        return m_currentSsid == ssid;
    }
    static bool hasCurrentSsid_() {
        EmMutexLock lock(m_networkMutex);
        return m_currentSsid.isNotEmpty();
    }


    static bool canKeepCurrentCredential_() {
        if (!hasCurrentSsid_()) {
            return false;
        }
        int8_t rssi = getRssi();
        EmMutexLock lock(m_networkMutex);
        for (const auto& credential : m_networks) {
            if (credential.ssid == m_currentSsid) {
                return credential.keepMe(rssi);
            }
        }
        return false;
    }

    static bool checkCurrentCredentialInternetConnection_() {
        if (!hasCurrentSsid_()) {
            return false;
        }
        
        // We still not mark as connected so we AVOID checking; EmWiFi::isConnected()!
        bool hasInternetConnection = EmNet::checkInternetConnection(m_internetCheckType);

        EmMutexLock lock(m_networkMutex);
        for (auto& credential : m_networks) {
            if (credential.ssid == m_currentSsid) {
                return credential.setInternetConnection(hasInternetConnection);
            }
        }
        return false;
    }

    static bool hasNetworks_() {
        EmMutexLock lock(m_networkMutex);
        return !m_networks.empty();
    }

    static void raiseEvent_(EmWiFiEventType type) {
        // To avoid locking event callback we go by indexes which might not be 
        // a 100% user events safe method but it avoid deadlock in case user
        // calls 'addEventHandler' or 'removeEventHandler' within the event handler.
        size_t index=0;
        for (;;)
        {
            EmWiFiEventCallback cb;
            void* arg;
            {
                EmMutexLock lock(m_eventsMutex);
                if (index >= m_eventHandlers.size())
                    break;
                cb = m_eventHandlers[index].callback;
                arg = m_eventHandlers[index].userArg;
            }
            cb(arg, type);
            ++index;
        }
        // Remove user marked event handler
        EmMutexLock lock(m_eventsMutex);
        m_eventHandlers.erase(
            std::remove_if(
                m_eventHandlers.begin(),
                m_eventHandlers.end(),
                [](const EmWiFiEventHandler& h)
                {
                    return h.markedForRemoval();
                }),
            m_eventHandlers.end());        
    }

    static void eventHandler_(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
    static void wifiTaskCore_(void* pvParameters);

    inline static EmMutex m_initMutex;
    inline static esp_netif_t* m_netif = nullptr;
    inline static EmStringS m_currentSsid; // Needed in case of direct user connect(...) call!

    inline static EmMutex m_networkMutex;
    inline static std::vector<EmWiFiApCredential> m_networks;

    inline static EmMutex m_eventsMutex;
    inline static std::vector<EmWiFiEventHandler> m_eventHandlers;
    inline static std::atomic<TaskHandle_t> m_taskHandle = nullptr;

    inline static std::atomic<uint16_t> m_checkIntervalSec = 0;
    inline static std::atomic<EmWiFiLevel> m_checkLevel = EmWiFiLevel::notConnected;
    inline static std::atomic<bool> m_connected = false;
    inline static std::atomic<bool> m_hasInternetConnection = false;
    inline static EmTimeout m_internetCheckTimeout = EmTimeout(EmDuration(0, 5, 0), true);  // % minutes to check for internet connection
    inline static EmNetInternetCheckType m_internetCheckType = EmNetInternetCheckType::udp;
};

inline bool EmWiFiApCredential::keepMe() const { return keepMe(EmWiFi::getRssi()); }


#endif
