#include "em_wifi.h"

#include "em_net.h"

#ifdef EM_XIAO_C6
    #include "em_gpio.h"
    // Define the internal XIAO antenna routing pins
    #define XIAO_WIFI_ENABLE       3
    #define XIAO_WIFI_ANT_CONFIG   14
#endif
#include <freertos/task.h>

std::atomic<uint32_t> EmWiFiEventHandler::m_nextHandlerId = 0;

bool EmWiFiApCredential::checkInternetConnection(EmNetInternetCheckType connectType) {
    if (!EmWiFi::isConnected()) {
        // Wifi not connected, no need to check internet connection!
        return false;
    }   
    return setInternetConnection(EmNet::checkInternetConnection(connectType));
}    

bool EmWiFiApCredential::setInternetConnection(bool hasInternetConnection) {
    if (hasInternetConnection) {
        // This AP has internet connection!
        noInternetFreezeTimeout.setExpired();
        return true;
    }
    // No internet connection, lets mark this AP as not connectable for  
    // a while to avoid trying to connect to it again and again
    noInternetFreezeTimeout.restart();
    return false;
}

void EmWiFiApCredential::toJson(EmJsonWriter& apsJson) const {
    // Starts the new AP dict (lets keep short dict keys!)
    apsJson.beginObject();
    apsJson.addString("ssid", ssid);
    apsJson.addString("pwd", password);
    apsJson.addInt("keep", (int)keepMeThreshold);
    apsJson.addInt("noInt", (int)noInternetFreezeTimeout.getDuration().seconds());
    apsJson.end(); // We MUST not finalize this json!
}

bool EmWiFiApCredential::fromJson(const EmJsonReader& apsJson, const EmJsonInnerObj& scope) {
    if (!apsJson.getString("ssid", ssid, scope)) {
        return false;
    }
    if (!apsJson.getString("pwd", password, scope)) {
        return false;
    }
    if (!apsJson.getInt("keep", keepMeThreshold, scope)) {
        return false;
    }
    uint32_t secs;
    if (!apsJson.getInt("noInt", secs, scope)) {
        return false;
    }
    noInternetFreezeTimeout.setTimeout(secs*1000);
    noInternetFreezeTimeout.setExpired();
    return true;
}


bool EmWiFi::switchAntenna(EmWiFiAntennaType antennaType) {
#if defined(ARDUINO_SEEED_XIAO_ESP32C6) || defined(EM_XIAO_C6)
    pinMode(XIAO_WIFI_ANT_CONFIG, OUTPUT);
    digitalWrite(XIAO_WIFI_ANT_CONFIG, antennaType == EmWiFiAntennaType::internal ? 0 : 1);
    tDelay(50, true);
    return true;
#elif defined(ARDUINO_SEEED_XIAO_ESP32S3) || defined(EM_XIAO_S3)
    return (antennaType == EmWiFiAntennaType::external);
#endif
    return false;
}

void EmWiFi::init(EmWiFiPsMode powerSaveMode, 
                  EmWiFiAntennaType antennaType,
                  const char* hostname,
                  EmNetInternetCheckType internetCheckType,
                  EmDuration internetCheckTimeout) {
    EmMutexLock lock(m_initMutex);
    m_internetCheckType = internetCheckType;
    m_internetCheckTimeout = EmTimeout(internetCheckTimeout, true);
    if (m_netif != nullptr) {
        // already initialized!
        return;
    }
    logDebug("EmWiFi", "Initializing WiFi...");

#ifdef EM_XIAO_C6
    pinMode(XIAO_WIFI_ENABLE, OUTPUT);
    digitalWrite(XIAO_WIFI_ENABLE, 0); // LOW powers ON the switch component
    tDelay(100, true); // Requested delay before antenna selection
#endif

    switchAntenna(antennaType);

    // Initialize the Idf underlying TCP/IP stack
    EmNet::init();
    
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Configure and start the Idf WiFi handling
    m_netif = esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    if (hostname) {
        esp_netif_set_hostname(m_netif, hostname); 
    }
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &EmWiFi::eventHandler_,
                                                        nullptr,
                                                        nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &EmWiFi::eventHandler_,
                                                        nullptr,
                                                        nullptr));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_ps(static_cast<wifi_ps_type_t>(powerSaveMode)));
    ESP_ERROR_CHECK(esp_wifi_start());
}

bool EmWiFi::addAP(const EmWiFiApCredential& ap) {
    EmMutexLock lock(m_networkMutex);
    if (m_networks.size() < WIFI_MAX_AP_CREDENTIALS && ap.ssid.isNotEmpty()) {
        m_networks.push_back(ap);
        return true;
    }
    return false;
}

bool EmWiFi::startConnectionLoop(uint16_t checkIntervalSec, EmWiFiLevel checkLevel) {
    // In case not initialized!
    init();

    m_checkIntervalSec = checkIntervalSec > 0 ? checkIntervalSec : 60;
    m_checkLevel = checkLevel;
    
    if (m_taskHandle == nullptr) {
        // Creates a background task running on Core 0 to leave Core 1 free for your loop()
        TaskHandle_t taskHandle;
        xTaskCreatePinnedToCore(
            EmWiFi::wifiTaskCore_, // Function to execute
            "EmWiFi_task",         // Name of task
            4096,                  // Stack size in words
            nullptr,               // Parameter passed to the task
            1,                     // Task priority
            &taskHandle,           // Task handle
            0                      // Core ID (0)
        );
        m_taskHandle = taskHandle;
        return taskHandle != nullptr;
    }
    return false;
}

void EmWiFi::stopConnectionLoop() {
    if (m_taskHandle != nullptr) {
        vTaskDelete(m_taskHandle);
        m_taskHandle = nullptr;
    }
}

void EmWiFi::connect(const char* ssid, const char* password, EmDuration waitTime) {
    // In case not initialized!
    init();

    logDebug<100>("EmWiFi", "Connecting to '%s'...", ssid);

    // Need to disconnect first?
    if (EmWiFi::isConnected()) {
        esp_wifi_disconnect();
        logDebug("EmWiFi", "Disconnecting...");
        EmTimeout disconnectionTimeout(1000);
        while (EmWiFi::isConnected() && disconnectionTimeout.isNotExpired()) {
            tDelay(20, true); 
        }
    }

    wifi_config_t wifi_config = {};
    wifi_config.sta.scan_method = WIFI_FAST_SCAN;
    wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    strncpy(reinterpret_cast<char*>(wifi_config.sta.ssid), ssid, sizeof(wifi_config.sta.ssid));
    strncpy(reinterpret_cast<char*>(wifi_config.sta.password), password, sizeof(wifi_config.sta.password));    
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_connect();
    setCurrentSsid_(ssid);
    // Lets wait extra time for this new connection
    EmTimeout conTimeout(waitTime);
    while (!isConnected() && !conTimeout.isExpired(false)) {
        tDelay(100, true);
    }
}

void EmWiFi::disconnect() {
    m_connected = false;
    esp_wifi_disconnect();
    clearCurrentSsid_();
}

void EmWiFi::eventHandler_(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t* d = (wifi_event_sta_disconnected_t*)event_data;
        logDebug<100>("EmWiFi", "Disconnected! [Reason: %d]", d->reason);
        m_connected = false;
        EmWiFi::raiseEvent_(EmWiFiEventType::disconnected);
        EmWiFi::startApConnectionLoop_();
    } else
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        logDebug("EmWiFi", "Connected!");
        // Wait for IP_EVENT_STA_GOT_IP to set m_connected = true
        m_connected = false; 
    } else
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        logDebug("EmWiFi", "Got IP!");
        // Check if current AP has internet connection
        if (!EmWiFi::checkCurrentCredentialInternetConnection_()) {
            EmWiFi::disconnect();
            // Notify the connection loop to check for another AP immediately
            xTaskNotifyGive(EmWiFi::m_taskHandle); 
            return;
        }
        m_connected = true;
        EmWiFi::raiseEvent_(EmWiFiEventType::connected);
    }
}

bool EmWiFi::getBestNetwork_(EmWiFiApCredential& bestNetwork) {
    // Any user defined network?
    if (!hasNetworks_()) {
        return false;
    }

    // Scan for available networks
    raiseEvent_(EmWiFiEventType::scanBegin);
    wifi_scan_config_t scan_config = {};
    scan_config.show_hidden = false;                // No hidden SSIDs
    scan_config.scan_type = WIFI_SCAN_TYPE_PASSIVE; // We don't want disconnections
    scan_config.scan_time.passive = 300;            // max_ms_per_channel
    esp_err_t ret = esp_wifi_scan_start(&scan_config, true);
    if (ret != ESP_OK) {
        return false;
    }
    uint16_t scanResult = 0;
    esp_wifi_scan_get_ap_num(&scanResult);
    if (scanResult == 0) {
        return false;
    }
    // Check if any network matches and take the best one
    bool networkFound = false;
    bool keepMeNetworkFound = false;
    int8_t highestRssi = -127;
    wifi_ap_record_t scannedAp;
    for (int i = 0; i < scanResult && !keepMeNetworkFound; i++) {
        if (esp_wifi_scan_get_ap_record(&scannedAp) == ESP_OK) {
            logDebug<100>("EmWiFi", "Found '%s' network [%d]", 
                          scannedAp.ssid, 
                          (int)scannedAp.rssi);
            EmMutexLock lock(m_networkMutex);
            for (auto& _network : m_networks) {
                if (_network.isConnectable() &&
                    _network.ssid.equals(reinterpret_cast<char*>(scannedAp.ssid))) {
                    // Check the credential 'keep me' threshold, if any is defined
                    keepMeNetworkFound = _network.keepMe(scannedAp.rssi);
                    if (keepMeNetworkFound || scannedAp.rssi > highestRssi) {
                        highestRssi = scannedAp.rssi;
                        bestNetwork.ssid.set(_network.ssid);
                        bestNetwork.password.set(_network.password);
                        networkFound = true;
                        if (keepMeNetworkFound) {
                            logDebug<100>("EmWiFi", "Found a 'keep me' network: '%s'", 
                                          bestNetwork.ssid.c_str());
                            break; // No need to check other networks
                        }
                    }
                }
            }
        }
    }
    if (networkFound) {
        logDebug<100>("EmWiFi", "Best network: '%s'", bestNetwork.ssid.c_str());
    }
    esp_wifi_clear_ap_list();
    raiseEvent_(EmWiFiEventType::scanEnd);
    return networkFound;
}

void EmWiFi::wifiTaskCore_(void*) {
    // Endless loop until task is killed
    while (true) {
        if (EmWiFi::hasNetworks_() &&                           // Any user defined AP
            (EmWiFi::isNotConnected() ||                        // Is WiFi disconnected
             EmWiFi::hasNoInternetConnection() ||               // No internet connection
             EmWiFi::getWiFiLevel() <= EmWiFi::m_checkLevel)) { // Is WiFi level poor
            // Clear current network ssid if disconnected
            if (EmWiFi::isNotConnected()) {
                EmWiFi::clearCurrentSsid_();
            }
            // Check the current credential "keep me" threshold, if any is defined
            if (!EmWiFi::canKeepCurrentCredential_()) {
                // Get the best user defined AP if any is found
                EmWiFiApCredential bestNetwork;
                if (EmWiFi::getBestNetwork_(bestNetwork) &&      // Any network found
                    !EmWiFi::isCurrentSsid_(bestNetwork.ssid)) { // Different than this one
                    EmWiFi::connect(bestNetwork.ssid.c_str(), 
                                    bestNetwork.password.c_str(), 
                                    EmDuration(0,0,10));  // Lets wait 10 seconds
                }
            }
        }
        // Wait timeout or event notification to check again
        xTaskNotifyWait(0x00, 
                        ULONG_MAX, 
                        nullptr, 
                        pdMS_TO_TICKS(EmWiFi::m_checkIntervalSec*1000));
    }
}
