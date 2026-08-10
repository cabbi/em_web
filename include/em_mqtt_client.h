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
using EmMqttStringPayload = EmStringXL;

// An MQTT client class that wraps the ESP-IDF MQTT client functionality.
// The client automatically handles re-connections and allows for multiple topic subscriptions 
// with individual callbacks. All user subscriptions are automatically restored on re-connection. 
// NOTE: 
// if you are using TLS, you must ensure that time is synchronized. You can use 'EmTime' class for this purpose.
class EmMqttClient {
public:
    typedef void (*EmMqttOnMsgCallback)(const EmStringBase& topic, const EmStringBase& payload);
    typedef void (*EmMqttOnConnectCallback)(const EmMqttClient& self);

    EmMqttClient() = default;    
    ~EmMqttClient() {
        disconnect(true);
    }

    // Connects to the MQTT broker with optional TLS parameters.
    // If 'root_ca' is provided, it will be used for server certificate verification.
    bool connect(const char* endpoint, 
                 EmMqttOnConnectCallback connectCallback = nullptr, 
                 uint16_t port = 8883,
                 const char* root_ca = nullptr, 
                 const char* client_cert = nullptr, 
                 const char* client_key = nullptr,
                 int keepaliveSec = 120);

    bool disconnect(bool removeAllSubscriptions);

    bool publish(const char* topic, const char* payload, int qos = 1) const;
    
    void subscribe(const char* topic, EmMqttOnMsgCallback msgCallback, int qos = 1);
    bool unsubscribe(const char* topic);
    void unsubscribeAll();

    bool isConnected() const;

protected:
    struct Subscription_ {
        EmMqttStringTopic topicFilter;
        EmMqttOnMsgCallback callback;
        int qos;
    };

    void onMessage_(const EmStringBase& topic, const EmStringBase& payload);
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
    EmMqttOnConnectCallback m_connectCallback = nullptr;
    
    std::vector<Subscription_*> m_subscriptions;
    ts_bool m_connected = false;

    EmMqttStringTopic m_currentTopic;
    EmMqttStringPayload m_currentPayload;
};

#endif
