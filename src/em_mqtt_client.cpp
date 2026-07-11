#include "em_log.h"
#include "em_mqtt_client.h"

void EmMqttClient::mqttEventHandler_(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
    auto* instance = static_cast<EmMqttClient*>(handler_args);
    auto* event = static_cast<esp_mqtt_event_handle_t>(event_data);
    
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            logInfo<100>("MQTT", "Connected successfully. Session status: %d", event->session_present);
            instance->onConnect_();
            break;
            
        case MQTT_EVENT_DATA: {
            EmMqttStringTopic topic(event->topic, event->topic_len);
            EmMqttStringPayload payload(event->data, event->data_len);
            instance->onMessage_(event->msg_id, topic, payload);
            break;
        }
        case MQTT_EVENT_DISCONNECTED:
            logWarning("MQTT", "Disconnected. Underlying task will auto-retry.");
            break;
        default:
            break;
    }
}

bool EmMqttClient::connect(const char* endpoint,
                           EmMqttOnMsgCallback msgCallback,
                           EmMqttOnConnectCallback connectCallback,
                           const char* root_ca, 
                           const char* client_cert, 
                           const char* client_key,
                           int keepaliveSec) {

    if (client_cert != nullptr) {
        // TODO: need to initialize the time                            
    }

    esp_mqtt_client_config_t mqtt_cfg = {};
    m_msgCallback = msgCallback;
    m_connectCallback = connectCallback;
    
    // Network Endpoint and Server Verification
    mqtt_cfg.broker.address.uri = endpoint; 
    mqtt_cfg.broker.verification.certificate = root_ca;
    
    // Enable SNI (Server Name Indication) automatically for virtual host cloud endpoints
    mqtt_cfg.broker.verification.skip_cert_common_name_check = false;

    // Client X.509 Cryptographic Authentication (Required by AWS)
    mqtt_cfg.credentials.authentication.certificate  = client_cert;
    mqtt_cfg.credentials.authentication.key = client_key;

    // Native MQTT v5.0 Protocol Selection
    // This unlocks features like Message Expiry and User Properties on supporting brokers
    mqtt_cfg.session.protocol_ver = MQTT_PROTOCOL_V_5; 
    
    // v5.x Network Tuning: Keep-Alive & Automatic Reconnection
    mqtt_cfg.session.keepalive = keepaliveSec;
    mqtt_cfg.network.disable_auto_reconnect = false; // Background task auto-heals dropped connections

    // Driver initialization
    m_client = esp_mqtt_client_init(&mqtt_cfg);
    if (m_client == nullptr) {
        return false;
    }
    esp_mqtt_client_register_event(m_client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, mqttEventHandler_, this);
    return esp_mqtt_client_start(m_client) == ESP_OK;
}

bool EmMqttClient::disconnect() {
    if (m_client != nullptr) {
        esp_mqtt_client_disconnect(m_client);
        esp_mqtt_client_stop(m_client);
        esp_mqtt_client_destroy(m_client);
        m_client = nullptr; 
        return true;
    }
    return false;
}

bool EmMqttClient::publish(const char* topic, const char* payload, int qos) const {
    if (m_client != nullptr) {
        return esp_mqtt_client_publish(m_client, topic, payload, 0, qos, 0) == ESP_OK;
    }
    return false;
}

int EmMqttClient::subscribe(const char* topic, int qos) const {
    if (m_client == nullptr) {
        return -1;
    }

    int msg_id = esp_mqtt_client_subscribe(m_client, topic, qos);
    if (msg_id >= 0) {
        logInfo<100>("MQTT", "Sent sub request for %s (Expected Msg ID: %d)", topic, msg_id);
    }
    return msg_id;
}
