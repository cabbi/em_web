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
        // We set initialize as soon as possible to avoid someone else colling this!
        m_initialized = true; 
        ESP_ERROR_CHECK(esp_netif_init()); // Pass or abort!
        return m_initialized;
    }

private:
    // No need to create an object
    EmNet() {};

    inline static std::atomic<bool> m_initialized = false;
};

#endif //__EM_NET_H__