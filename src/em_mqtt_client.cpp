#include "em_log.h"
#include "em_mqtt_client.h"

bool EmMqttClient::connect(const char* endpoint,
                           uint16_t port,
                           EmMqttOnStatusChangedCallback statusChangedCallback, 
                           const char* root_ca, 
                           const char* client_cert, 
                           const char* client_key,
                           int keepaliveSec) {
    m_statusChangedCallback = statusChangedCallback;

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
    // First lets remove subscriptions if requested
    if (removeAllSubscriptions) {
        unsubscribeAll();
    }

    // Client initialized?
    if (m_client.load() == nullptr) {
        return false;
    }

    // Stop and free the current client handle
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

void EmMqttClient::subscribe(void* userData, 
                             const char* topic, 
                             EmMqttOnMsgCallback msgCallback, 
                             int qos) {
    EmMutexLock lock(m_subscriptionMutex);
    
    Subscription_* sub = new Subscription_();
    sub->userData = userData;
    sub->topicFilter.set(topic); 
    sub->callback = msgCallback;
    sub->qos = qos;
    
    m_subscriptions.push_back(sub);

    // Subscribe the topic if connected, if not it will do once connection is established
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
    
    // Unsubscribe the topic if connected
    if (m_connected.load() && m_client.load() != nullptr) {
        int msg_id = esp_mqtt_client_unsubscribe(m_client.load(), topic);
        return (msg_id >= 0);
    }

    return found;
}

void EmMqttClient::unsubscribeAll() {
    EmMutexLock lock(m_subscriptionMutex);
    
    for (const auto& sub : m_subscriptions) {
        // Unsubscribe the topic if connected
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

    if (m_statusChangedCallback != nullptr) {
        m_statusChangedCallback(*this, true);
    }
}

void EmMqttClient::onDisconnect_() {
    m_connected.store(false);
    logWarning("EmMqttClient", "Disconnected from broker.");
    if (m_statusChangedCallback != nullptr) {
        m_statusChangedCallback(*this, false);
    }
}

bool EmMqttClient::matchTopic_(const char* filter, const char* topic) const {
    // Valid input?
    if (filter == nullptr || topic == nullptr) {
        return false;
    }

    while (*filter) {
        // Case 1: Handle multi-level wildcard '#'
        if (*filter == '#') {
            // According to MQTT spec, '#' must be the absolute last character in the filter
            return (*(filter + 1) == '\0');
        }

        // Case 2: Handle single-level wildcard '+'
        if (*filter == '+') {
            // Consume characters from the topic until we hit a level separator '/' or end of string
            while (*topic && *topic != '/') {
                topic++;
            }
            filter++;
            // Continue the main loop to validate the character right after '+' (usually '/')
            continue; 
        }

        // Case 3: Regular character mismatch handling
        if (*filter != *topic) {
            // Special MQTT Edge Case: Filter "sport/#" must match the topic "sport"
            if (*filter == '/' && *(filter + 1) == '#' && *(filter + 2) == '\0' && *topic == '\0') {
                return true;
            }
            return false;
        }
        // Next character in both filter and topic must match
        filter++;
        topic++;
    }

    // Both filter and topic strings must reach their end simultaneously for a perfect match
    return (*filter == '\0' && *topic == '\0');
}



void EmMqttClient::onMessage_(const EmStringBase& topic, 
                              const char* payload, 
                              size_t payloadLen,
                              EmMqttPayloadBufferStatus payloadBufferStatus) {
    std::vector<Subscription_*> tempSubs;
    
    {
        EmMutexLock lock(m_subscriptionMutex);
        tempSubs = m_subscriptions; 
    }

    for (const auto& sub : tempSubs) {
        if (matchTopic_(sub->topicFilter.c_str(), topic.c_str())) {
            if (sub->callback != nullptr) {
                sub->callback(sub->userData, topic, payload, payloadLen, payloadBufferStatus);
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
        case MQTT_EVENT_DATA: {
            // First message data for a topic, initialize the current topic and clear the payload
            if (event->topic_len > 0) {
                self->m_currentTopic.set(event->topic, event->topic_len);
                self->m_currentPayloadLen = 0;
            }
            // Merge payload data with the topic message (i.e. multi-part handling)
            size_t dataToCopy = event->data_len;
            size_t newPayloadLen = self->m_currentPayloadLen + event->data_len;
            if (newPayloadLen > self->m_payloadBufferCapacity) {
                // Log once buffer overflow
                if (self->m_currentPayloadLen <= self->m_payloadBufferCapacity) {
                    logError<100>("EmMqttClient", "Payload buffer overflow for topic: %s", self->m_currentTopic.c_str());
                }
                // Truncate payload to fit buffer
				if (self->m_payloadBufferCapacity > self->m_currentPayloadLen) {
					dataToCopy = self->m_payloadBufferCapacity - self->m_currentPayloadLen;
				} else {
					dataToCopy = 0;
				}
            }
            // Copy the new payload block if there is space in the buffer
            if (dataToCopy > 0) {
                memcpy(self->m_payloadBuffer + self->m_currentPayloadLen, event->data, dataToCopy);
                // Set a null termination at the end of the payload buffer
                self->m_payloadBuffer[self->m_currentPayloadLen + dataToCopy] = '\0';
            }
            self->m_currentPayloadLen = newPayloadLen;
            // All message received?
            if (event->current_data_offset + event->data_len >= event->total_data_len) {
                // Compute the buffer status for the callback
                EmMqttPayloadBufferStatus payloadBufferStatus = EmMqttPayloadBufferStatus::notFull;
                if (self->m_currentPayloadLen == self->m_payloadBufferCapacity) {
                    payloadBufferStatus = EmMqttPayloadBufferStatus::full;
                } else if (self->m_currentPayloadLen > self->m_payloadBufferCapacity) {
                    payloadBufferStatus = EmMqttPayloadBufferStatus::overflow;
                }
                // Call the user callback with the complete message
                self->onMessage_(self->m_currentTopic, 
                                 self->m_payloadBuffer, 
                                 self->m_currentPayloadLen,
                                 payloadBufferStatus);
            }
        } break;
        default:
            break;
    }
}
