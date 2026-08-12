#ifndef __EM_MQTT_CLIENT_H
#define __EM_MQTT_CLIENT_H

#include <mqtt_client.h>
#include <vector>

// FreeRTOS includes for Mutex handling
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "em_string.h"
#include "em_threading.h"


using EmMqttStringTopic = EmStringL;

enum class EmMqttPayloadBufferStatus : uint8_t {
    notFull  = 0, // Payload fits in the provided buffer
    full     = 1, // Payload fits in the provided buffer, but the buffer is now full 
                  // (no null termination in case of string payload!)
    overflow = 2  // Payload was truncated due to insufficient buffer size
};

// An MQTT client class that wraps the ESP-IDF MQTT client functionality.
// The client automatically handles re-connections and allows for multiple topic subscriptions 
// with individual callbacks. All user subscriptions are automatically restored on re-connection. 
// NOTES:
//  - The message callback will always set a "\0" (i.e. string null-termination) at the end of the payload
//    buffer provided by the user. This is done to facilitate the most of the payloads being strings. 
//    However, the user must check the 'payloadBufferStatus' in the callback to determine if
//    the payload was truncated.
//  - If you are constructing 'EmMqttClient' by providing a buffer and its size, consider that the
//    maximum payload size will be 1 byte less than the buffer size to allow for the null termination 
//    (see note above).  
//  - if 'payloadBufferStatus' is 'overflow' the 'payloadLen' will exceed the 'payloadBufferSize' 
//    provided by the user, but data is not copied in the payload buffer.
//  - If you are using TLS, you must ensure that time is synchronized. You can use 'EmTime' class for this purpose.
class EmMqttClient {
public:
    typedef void (*EmMqttOnStatusChangedCallback)(EmMqttClient& self, bool connected);
    typedef void (*EmMqttOnMsgCallback)(void* userData, 
                                        const EmStringBase& topic, 
                                        const char* payload, 
                                        size_t payloadLen,
                                        EmMqttPayloadBufferStatus payloadBufferStatus);

    EmMqttClient(EmStringBase& payloadBuffer)
     : m_payloadBuffer(payloadBuffer.buffer()), m_payloadBufferCapacity(payloadBuffer.capacity()) {
    }
    EmMqttClient(char* payloadBuffer, size_t payloadBufferSize)
     : m_payloadBuffer(payloadBuffer), m_payloadBufferCapacity(payloadBufferSize-1) {
    }
    ~EmMqttClient() {
        disconnect(true);
    }

    // Connects to the MQTT broker with optional TLS parameters.
    // User can provide callbacks for status detection (i.e. connection and disconnection).
    // If 'root_ca' is provided, it will be used for server certificate verification.
    bool connect(const char* endpoint, 
                 uint16_t port = 8883,
                 EmMqttOnStatusChangedCallback statusChangedCallback = nullptr, 
                 const char* root_ca = nullptr, 
                 const char* client_cert = nullptr, 
                 const char* client_key = nullptr,
                 int keepaliveSec = 120);

    bool disconnect(bool removeAllSubscriptions);

    bool publish(const char* topic, const char* payload, int qos = 1) const;
    
    void subscribe(void* userData, 
                   const char* topic, 
                   EmMqttOnMsgCallback msgCallback, 
                   int qos = 1);
    bool unsubscribe(const char* topic);
    void unsubscribeAll();

    bool isConnected() const;

protected:
    struct Subscription_ {
        void* userData;
        EmMqttStringTopic topicFilter;
        EmMqttOnMsgCallback callback;
        int qos;
    };

    void onMessage_(const EmStringBase& topic, 
                    const char* payload, 
                    size_t payloadLen,
                    EmMqttPayloadBufferStatus payloadBufferStatus);
    void onConnect_();
    void onDisconnect_();

    bool matchTopic_(const char* filter, const char* topic) const;

    static void mqttEventHandler_(void* handler_args, 
                                  esp_event_base_t base, 
                                  int32_t event_id, 
                                  void* event_data);

private:
    mutable EmMutex m_subscriptionMutex;

    std::atomic<esp_mqtt_client_handle_t> m_client = nullptr;    
    EmMqttOnStatusChangedCallback m_statusChangedCallback = nullptr;
    
    std::vector<Subscription_*> m_subscriptions;
    ts_bool m_connected = false;

    EmMqttStringTopic m_currentTopic;
    char* m_payloadBuffer = nullptr;
    size_t m_payloadBufferCapacity = 0;
    size_t m_currentPayloadLen = 0;
};

#endif
