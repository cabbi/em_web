#include "em_wifi.h"
#include <em_timeout.h>


bool EmWiFi::addAP(const char* ssid, const char* passphrase) {
    if (m_networks.size() < 128 && ssid && strlen(ssid) > 0) {
        m_networks.push_back(EmWiFiCredential(ssid, passphrase));
        return true;
    }
    return false;
}

bool EmWiFi::start(uint16_t checkIntervalSec, EmWiFiLevel checkLevel) {
    m_checkIntervalSec = checkIntervalSec > 0 ? checkIntervalSec : 1;
    m_checkLevel = checkLevel;
    
    if (m_taskHandle == nullptr) {
        // Creates a background task running on Core 0 to leave Core 1 free for your loop()
        TaskHandle_t taskHandle;
        xTaskCreatePinnedToCore(
            this->wifiTaskCore_,   // Function to execute
            "EmWiFi_task",        // Name of task
            4096,                 // Stack size in words
            this,                 // Parameter passed to the task (pointer to this instance)
            1,                    // Task priority
            &taskHandle,          // Task handle
            0                     // Core ID (0)
        );
        m_taskHandle = taskHandle;
        return taskHandle != nullptr;
    }
    return false;
}

void EmWiFi::stop() {
    if (m_taskHandle != nullptr) {
        vTaskDelete(m_taskHandle);
        m_taskHandle = nullptr;
    }
}

bool EmWiFi::getBestNetwork_(EmWiFiCredential& bestNetwork) {
    // Any user defined network?
    if (m_networks.empty()) {
        return false;
    }

    // Scan for available networks
    int16_t scanResult = WiFi.scanNetworks(false, // async scan
                                           false, // show_hidden
                                           true,  // passive
                                           300);  // max_ms_per_channel 
    if (scanResult <= 0) {
        return false;
    }

    // Check if any network matches and take the best one
    bool networkFound = false;
    int32_t highestRssi = -1000;
    for (int i = 0; i < scanResult; i++) {
        EmStringS scannedSsid(WiFi.SSID(i).c_str());
        int currentRssi = WiFi.RSSI(i);
        EmMutexLock lock(m_networkMutex);
        for (auto& _network : m_networks) {
            if (_network.ssid == scannedSsid) {
                if (currentRssi > highestRssi) {
                    highestRssi = currentRssi;
                    bestNetwork.ssid.set(_network.ssid);
                    bestNetwork.password.set(_network.password);
                    networkFound = true;
                }
            }
        }
    }
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
                self->setCurrentSsid_(bestNetwork.ssid);
                WiFi.disconnect();
                WiFi.begin(bestNetwork.ssid.c_str(), 
                           bestNetwork.password.c_str());
                // Lest wait extra time for this new connection
                EmTimeout conTimeout(10000);
                while (!WiFi.isConnected() && !conTimeout.isExpired(false)) {
                    tDelay(100);
                }
            }
        }
        tDelay(self->m_checkIntervalSec*1000);
    }
}
