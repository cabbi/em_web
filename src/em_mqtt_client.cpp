#include "em_log.h"
#include "em_mqtt_client.h"

bool EmMqttClient::connect(const char* endpoint,
                           EmMqttOnConnectCallback connectCallback, 
                           uint16_t port, 
                           const char* root_ca, 
                           const char* client_cert, 
                           const char* client_key,
                           int keepaliveSec) {
    m_connectCallback = connectCallback;

    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.broker.address.uri = endpoint;
    mqtt_cfg.broker.address.port = port;
    mqtt_cfg.session.keepalive = keepaliveSec;

    if (root_ca != nullptr) {
        mqtt_cfg.broker.verification.certificate = root_ca;
    }
    if (client_cert != nullptr && client_key != nullptr) {
        mqtt_cfg.credentials.authentication.certificate = client_cert;
        mqtt_cfg.credentials.authentication.key = client_key;
    }

    m_client.store(esp_mqtt_client_init(&mqtt_cfg));
    if (m_client.load() == nullptr) {
        logError("EmMqttClient", "Failed to init MQTT client");
        return false;
    }

    if (esp_mqtt_client_register_event(m_client.load(), 
                                       MQTT_EVENT_ANY, 
                                       &EmMqttClient::mqttEventHandler_, 
                                       this) != ESP_OK) {
        esp_mqtt_client_destroy(m_client.load());
        m_client.store(nullptr);
        logError("EmMqttClient", "Failed to register MQTT event handler");
        return false;
    }

    if (esp_mqtt_client_start(m_client.load()) != ESP_OK) {
        esp_mqtt_client_destroy(m_client.load());
        m_client.store(nullptr);
        logError("EmMqttClient", "Failed to start MQTT client");
        return false;
    }
    logInfo("EmMqttClient", "MQTT client started successfully");
    return true;
}

bool EmMqttClient::disconnect(bool removeAllSubscriptions) {
    if (m_client.load() == nullptr) {
        return false;
    }

    if (removeAllSubscriptions) {
        unsubscribeAll();
    }

    esp_mqtt_client_stop(m_client.load());
    esp_mqtt_client_destroy(m_client.load());
    m_client.store(nullptr);
    m_connected.store(false);
    return true;
}

bool EmMqttClient::publish(const char* topic, const char* payload, int qos) const {
    if (!m_connected.load() || m_client.load() == nullptr) {
        return false;
    }
    return (esp_mqtt_client_publish(m_client.load(), topic, payload, 0, qos, 0) >= 0);
}

bool EmMqttClient::isConnected() const {
    return m_connected.load();
}

void EmMqttClient::subscribe(const char* topic, EmMqttOnMsgCallback msgCallback, int qos) {
    EmMutexLock lock(m_subscriptionMutex);
    
    // Alloca l'oggetto direttamente nell'heap. L'assegnazione iniziale da const char* 
    // chiama il costruttore base di EmString (Candidate 1 nel tuo log di errore), bypassando la copia.
    Subscription_* sub = new Subscription_();
    sub->topicFilter.set(topic); 
    sub->callback = msgCallback;
    sub->qos = qos;
    
    m_subscriptions.push_back(sub); // Il vettore memorizza il puntatore triviale senza problemi

    if (m_connected.load() && m_client.load() != nullptr) {
        esp_mqtt_client_subscribe(m_client.load(), topic, qos);
    }
}

bool EmMqttClient::unsubscribe(const char* topic) {
    EmMutexLock lock(m_subscriptionMutex);
    bool found = false;

    for (int i = static_cast<int>(m_subscriptions.size()) - 1; i >= 0; --i) {
        if (m_subscriptions[i]->topicFilter.equals(topic)) {
            Subscription_* subToDelete = m_subscriptions[i];
            m_subscriptions.erase(m_subscriptions.begin() + i);
            delete subToDelete;
            found = true;
        }
    }

    if (m_connected.load() && m_client.load() != nullptr) {
        int msg_id = esp_mqtt_client_unsubscribe(m_client.load(), topic);
        return (msg_id >= 0);
    }

    return found;
}

void EmMqttClient::unsubscribeAll() {
    EmMutexLock lock(m_subscriptionMutex);
    
    for (const auto& sub : m_subscriptions) {
        if (m_connected.load() && m_client.load() != nullptr) {
            esp_mqtt_client_unsubscribe(m_client.load(), sub->topicFilter.c_str());
        }
        delete sub;
    }
    m_subscriptions.clear();
}

void EmMqttClient::onConnect_() {
    EmMutexLock lock(m_subscriptionMutex);
    m_connected.store(true);
    logInfo("EmMqttClient", "Connected to Broker. Auto-resubscribing...");

    for (const auto& sub : m_subscriptions) {
        esp_mqtt_client_subscribe(m_client.load(), sub->topicFilter.c_str(), sub->qos);
    }

    if (m_connectCallback != nullptr) {
        m_connectCallback(*this);
    }
}

void EmMqttClient::onDisconnect_() {
    m_connected.store(false);
    logWarning("EmMqttClient", "Disconnected from broker.");
}

bool EmMqttClient::matchTopic_(const char* filter, const char* topic) const {
    while (*filter && *topic) {
        if (*filter == '+') {
            while (*topic && *topic != '/') topic++;
            filter++;
        } else if (*filter == '#') {
            return true;
        } else if (*filter == *topic) {
            filter++;
            topic++;
        } else {
            return false;
        }
    }
    return (*filter == '\0' && *topic == '\0') || (*filter == '#' && *(filter - 1) == '/');
}

void EmMqttClient::onMessage_(const EmStringBase& topic, const EmStringBase& payload) {
    std::vector<Subscription_*> tempSubs;
    
    {
        EmMutexLock lock(m_subscriptionMutex);
        tempSubs = m_subscriptions; 
    }

    for (const auto& sub : tempSubs) {
        if (matchTopic_(sub->topicFilter.c_str(), topic.c_str())) {
            if (sub->callback != nullptr) {
                sub->callback(topic, payload);
            }
        }
    }
}

void EmMqttClient::mqttEventHandler_(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
    auto* self = static_cast<EmMqttClient*>(handler_args);
    auto* event = static_cast<esp_mqtt_event_handle_t>(event_data);

    switch (static_cast<esp_mqtt_event_id_t>(event_id)) {
        case MQTT_EVENT_CONNECTED:
            self->onConnect_();
            break;
        case MQTT_EVENT_DISCONNECTED:
            self->onDisconnect_();
            break;
        case MQTT_EVENT_DATA:
            if (event->topic_len > 0) {
                self->m_currentTopic.set(event->topic);
                self->m_currentPayload.clear();
            }
            self->m_currentPayload.append(event->data, event->data_len);
            if (event->current_data_offset + event->data_len >= event->total_data_len) {
                self->onMessage_(self->m_currentTopic, self->m_currentPayload);
            }
            break;
        default:
            break;
    }
}
