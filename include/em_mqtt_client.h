#ifndef __EM_MQTT_CLIENT_H
#define __EM_MQTT_CLIENT_H

#include "mqtt_client.h"
#include <string>

class EmMqttClient {
public:
    bool begin(const char* endpoint, const char* root_ca, const char* client_cert, const char* client_key);
    void publish(const char* topic, const char* payload, int qos = 1);

protected:
    void onMessageReceived(const std::string& topic, const std::string& payload) {
        // Process your incoming AWS IoT messages here
    }
    static void mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data);

private:
    // Member vars
    esp_mqtt_client_handle_t client = nullptr;    
};

#endif __EM_MQTT_CLIENT_H