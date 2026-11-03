#ifndef __EM_NET_H__
#define __EM_NET_H__

#include <atomic>
#include <esp_netif.h>
#include <sys/socket.h>
#include <netdb.h>

#include "esp_http_client.h"

// The internet connection check type
enum class EmNetInternetCheckType: uint8_t {
    none = 0,
    udp  = 1, // UDP is faster and consumes less data since it does not require a handshake. 
              // However, some networks might block UDP port, in case use the TCP version.
    tcp  = 2  // TCP is slower and consumes more data since it requires a handshake. 
              // Use this TCP alternative in case UDP port is blocked by any firewall or network configuration.
};


// This class is a networking helper. 
// Using EmNet::init() ensures the 'esp_netif_init' is called ONLY ONCE.
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

    static bool checkInternetConnection(EmNetInternetCheckType checkType) {
        switch (checkType) {
        case EmNetInternetCheckType::udp:
            return checkInternetConnectionUDP();
        case EmNetInternetCheckType::tcp:
            return checkInternetConnectionTCP();
        default:
            return false;
        }
    }

    // Checks the internet connection via UDP. 
    // This method is faster than TCP and consumes less data since it does not require a handshake.
    // Some networks might block UDP port, in case use the TCP version.
    static bool checkInternetConnectionUDP() {
        struct addrinfo hints;
        struct addrinfo *res;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;   
        hints.ai_socktype = SOCK_DGRAM;  // UDP (avoid TCP handshake)    
        if (getaddrinfo("://google.com", NULL, &hints, &res) == 0) {
            freeaddrinfo(res);
            return true;
        } 
        return false;
    }

    // Checks the internet connection via TCP.
    // This method is slower than UDP and consumes more data since it requires a handshake.
    // Use this TCP alternative in case UDP port is blocked by any firewall or network configuration.  
    // The default timeout is 3 seconds, but you can specify a different value in milliseconds.
    static bool checkInternetConnectionTCP(int timeoutMs = 3000) {
        // Configure the HTTP client
        esp_http_client_config_t config = {};         
        config.url = "http://google.com"; 
        config.method = HTTP_METHOD_HEAD;          // HEAD method downloads headers only
        config.timeout_ms = timeoutMs?timeoutMs:1; // Prevent locking the thread (since 0 means 5 seconds we set 1 millisecond)
        config.disable_auto_redirect = false;      // Automatically follow standard redirects
        config.skip_cert_common_name_check = true; // Skip certificate common name check for HTTPS

        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (client == NULL) {
            return false;
        }

        bool hasInternet = false;
        // Execute the request synchronously (blocks for up to timeout_ms)
        esp_err_t err = esp_http_client_perform(client);
        if (err == ESP_OK) {
            int status_code = esp_http_client_get_status_code(client);
            // A 2xx or 3xx status code guarantees internet routing is working
            if (status_code >= 200 && status_code < 400) {
                hasInternet = true;
            }
        }
        esp_http_client_cleanup(client);
        return hasInternet;
    }

private:
    // No need to create an object
    EmNet() {};

    inline static std::atomic<bool> m_initialized = false;
};

#endif //__EM_NET_H__