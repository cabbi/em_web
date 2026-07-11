#ifndef __EM_NET_H__
#define __EM_NET_H__

#include <atomic>
#include <esp_netif.h>


// This class is to call ONLY ONCE the 'esp_netif_init'.
class EmNet {
public:
    // Initialize the Idf underlying TCP/IP stack
    static bool init() {
        if (m_initialized) {
            return true;
        }
        m_initialized = esp_netif_init() == ESP_OK;
        return m_initialized;
    }

private:
    // No need to create an object
    EmNet() {};

    inline static std::atomic<bool> m_initialized = false;
};

#endif //__EM_NET_H__