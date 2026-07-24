#include "em_wifi.h"

#include "em_log.h"
#include "em_net.h"
#include "em_timeout.h"

void EmWiFi::init() {
    EmMutexLock lock(m_initMutex);
    if (m_netif != nullptr) {
        // already initialized!
        return;
    }

    // Initialize the Idf underlying TCP/IP stack
    EmNet::init();
    
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Configure and start the Idf WiFi handling
    m_netif = esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_register(WIFI_EVENT,
                                        ESP_EVENT_ANY_ID,
                                        &EmWiFi::eventHandler_,
                                        nullptr,
                                        nullptr);
    esp_event_handler_instance_register(IP_EVENT,
                                        IP_EVENT_STA_GOT_IP,
                                        &EmWiFi::eventHandler_,
                                        nullptr,
                                        nullptr);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
}

bool EmWiFi::addAP(const char* ssid, const char* passphrase) {
    if (m_networks.size() < 128 && ssid && strlen(ssid) > 0) {
        m_networks.push_back(EmWiFiCredential(ssid, passphrase));
        return true;
    }
    return false;
}

bool EmWiFi::startConnectionLoop(uint16_t checkIntervalSec, EmWiFiLevel checkLevel) {
    // In case not initialized!
    init();

    m_checkIntervalSec = checkIntervalSec > 0 ? checkIntervalSec : 1;
    m_checkLevel = checkLevel;
    
    if (m_taskHandle == nullptr) {
        // Creates a background task running on Core 0 to leave Core 1 free for your loop()
        TaskHandle_t taskHandle;
        xTaskCreatePinnedToCore(
            EmWiFi::wifiTaskCore_, // Function to execute
            "EmWiFi_task",         // Name of task
            4096,                  // Stack size in words
            nullptr,               // Parameter passed to the task (pointer to this instance)
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

    wifi_config_t wifi_config = {};
    strncpy(reinterpret_cast<char*>(wifi_config.sta.ssid), ssid, sizeof(wifi_config.sta.ssid));
    strncpy(reinterpret_cast<char*>(wifi_config.sta.password), password, sizeof(wifi_config.sta.password));    
    esp_wifi_disconnect();
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_connect();
    setCurrentSsid_(ssid);
    // Lets wait extra time for this new connection
    EmTimeout conTimeout(10000);
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
    // Cast the void pointer back to our class instance
    EmWiFi* self = static_cast<EmWiFi*>(arg);
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        m_connected = false;
        self->raiseEvent_(EmWiFiEventType::disconnected);

    } else
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        m_connected = true;
        self->raiseEvent_(EmWiFiEventType::connected);
    }
}

bool EmWiFi::getBestNetwork_(EmWiFiCredential& bestNetwork) {
    // Any user defined network?
    if (m_networks.empty()) {
        return false;
    }
    // Scan for available networks
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
    int8_t highestRssi = -127;
    wifi_ap_record_t scannedAp;
    for (int i = 0; i < scanResult; i++) {
        if (esp_wifi_scan_get_ap_record(&scannedAp) == ESP_OK) {
            EmMutexLock lock(m_networkMutex);
            for (auto& _network : m_networks) {
                if (_network.ssid.equals(reinterpret_cast<char*>(scannedAp.ssid))) {
                    if (scannedAp.rssi > highestRssi) {
                        highestRssi = scannedAp.rssi;
                        bestNetwork.ssid.set(_network.ssid);
                        bestNetwork.password.set(_network.password);
                        networkFound = true;
                    }
                }
            }
        }
    }
    esp_wifi_clear_ap_list();
    return networkFound;
}

void EmWiFi::wifiTaskCore_(void* pvParameters) {
    // Cast the void pointer back to our class instance
    EmWiFi* self = static_cast<EmWiFi*>(pvParameters);
    // Endless loop until task is killed
    while (true) {
        if (!self->m_networks.empty() &&                    // Any user defined AP
            (self->isNotConnected() ||                      // Is WiFi disconnected
             self->getWiFiLevel() <= self->m_checkLevel)) { // Is WiFi level poor
            // Clear current network ssid if disconnected
            if (self->isNotConnected()) {
                self->clearCurrentSsid_();
            }
            // Get the best user defined AP if any is found
            EmWiFiCredential bestNetwork;
            if (self->getBestNetwork_(bestNetwork) &&      // Any network found
                !self->isCurrentSsid_(bestNetwork.ssid)) { // Different than this one
                self->connect(bestNetwork.ssid.c_str(), 
                              bestNetwork.password.c_str(), 
                              EmDuration(0,0,10));  // Lets wait 10 seconds
            }
        }
        tDelay(self->m_checkIntervalSec*1000, true);
    }
}
