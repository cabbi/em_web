#include "em_mqtt_client.h"

void EmMqttClient::mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
    auto* instance = static_cast<EspMqttAwsClient*>(handler_args);
    auto* event = static_cast<esp_mqtt_event_handle_t>(event_data);
    
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            // Auto-subscribe to an AWS topic on successful connection
            esp_mqtt_client_subscribe(event->client, "esp32/sub_topic", 1);
            break;
            
        case MQTT_EVENT_DATA: {
            // Safely extract the string payload (ESP-IDF payloads are NOT null-terminated)
            std::string topic(event->topic, event->topic_len);
            std::string payload(event->data, event->data_len);
            
            // Route the data to your application
            instance->onMessageReceived(topic, payload);
            break;
        }
        case MQTT_EVENT_DISCONNECTED:
            // Background automatically retries, but you can flag this in your UI/Logs
            break;
        default:
            break;
    }
}

bool EmMqttClient::begin(const char* endpoint, const char* root_ca, const char* client_cert, const char* client_key) {
    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.broker.address.uri = endpoint; // e.g., "mqtts://://amazonaws.com"
    mqtt_cfg.broker.verification.cert_pem = root_ca;
    mqtt_cfg.credentials.authentication.certificate_pem = client_cert;
    mqtt_cfg.credentials.authentication.private_key_pem = client_key;

    client = esp_mqtt_client_init(&mqtt_cfg);
    if (client == nullptr) return false;

    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, this);
    return (esp_mqtt_client_start(client) == ESP_OK);
}

void EmMqttClient::publish(const char* topic, const char* payload, int qos) {
    if (client) {
        esp_mqtt_client_publish(client, topic, payload, 0, qos, 0);
    }
}

