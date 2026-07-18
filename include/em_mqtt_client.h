#ifndef __EM_MQTT_CLIENT_H
#define __EM_MQTT_CLIENT_H

//extern "C" {
    #include <mqtt_client.h>
//}

#include "em_string.h"

using EmMqttStringTopic = EmStringL;
using EmMqttStringPayload = EmStringXL;

// A ESP IDF base Mqtt client implementation
class EmMqttClient {
public:
    typedef void (*EmMqttOnMsgCallback)(int msgId, const EmStringBase& topic, const EmStringBase& payload);
    typedef void (*EmMqttOnConnectCallback)(const EmMqttClient& self);
    
    // Connects to MQTT server.
    // User can set a message callback used to receive subscribed topics,
    // and a connection callback generally used to subscribe to desired topics.
    // If the connection drops, the client must again subscribe its topics!
    bool connect(const char* endpoint, 
                 EmMqttOnMsgCallback msgCallback,
                 EmMqttOnConnectCallback connectCallback, 
                 const char* root_ca = nullptr, 
                 const char* client_cert = nullptr, 
                 const char* client_key = nullptr,
                 int keepaliveSec = 120);

    // Disconnect and close the current client MQTT connection.
    bool disconnect();

    // Publish a payload to a topic
    bool publish(const char* topic,
                 const char* payload, 
                 int qos = 1) const;

    // Subscribe a topic. 
    // Returns the tracking Message ID for that topic or -1 on failure.
    int subscribe(const char* topic, int qos = 1) const;

    bool isConnected() {
        return m_client != nullptr;
    }

protected:
    void onMessage_(int msgId, const EmStringBase& topic, const EmStringBase& payload) {
        if (m_msgCallback) {
            m_msgCallback(msgId, topic, payload);
        }
    }

    void onConnect_() {
        if (m_connectCallback != nullptr) {
            m_connectCallback(*this);
        }
    }

    static void mqttEventHandler_(void* handler_args, 
                                  esp_event_base_t base, 
                                  int32_t event_id, 
                                  void* event_data);

private:
    // Member vars
    esp_mqtt_client_handle_t m_client = nullptr;    
    EmMqttOnMsgCallback m_msgCallback = nullptr;
    EmMqttOnConnectCallback m_connectCallback = nullptr;
};

#endif //__EM_MQTT_CLIENT_H