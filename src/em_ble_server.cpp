#include "em_ble_server.h"

#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#include "em_sbo_buffer.h"

void EmBleServer::init(const EmStringBase& deviceName, uint32_t passkey) {
    if (m_isInitialized) return;

    // Map properties using your custom EmStringBase instance APIs
    m_rawDeviceName = deviceName.c_str(); 
    m_deviceNameLen = (uint8_t)deviceName.length();
    m_passkey = passkey;

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(nimble_port_init());

    ble_hs_cfg.sync_cb = EmBleServer::onStackSync_;
    ble_hs_cfg.reset_cb = EmBleServer::onStackReset_;
    ble_hs_cfg.gatts_register_cb = EmBleServer::gattRegisterCallback_;

    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_DISP_ONLY; 
    ble_hs_cfg.sm_bonding = 1;                     
    ble_hs_cfg.sm_mitm = 1;                        
    ble_hs_cfg.sm_sc = 1;                          
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    m_isInitialized = true;
}

bool EmBleServer::addService(EmBleService& service) {
    if (!m_isInitialized || m_started) {
        return false;
    }
    m_services.push_back(&service);
    return true;
}

bool EmBleServer::start() {
    if (!m_isInitialized || m_started) {
        return false;
    }
    m_started = true;

    // Dynamic conversion mapping layout of C++ Tree objects into continuous plain C tables
    initGattServices_();

    // Spawns thread controller running background core operations
    nimble_port_freertos_init(EmBleServer::nimbleHostTask_);

    return true;
}

bool EmBleServer::stop() {
    if (!m_isInitialized || !m_started) {
        return false;
    }

    ESP_LOGW(TAG, "Executing full BLE Server shutdown sequence...");

    // Terminate any active smartphone connections if they exist
    if (m_connHandle != BLE_HS_CONN_HANDLE_NONE) {
        // Disconnect with a standard remote user termination error code
        ble_gap_terminate(m_connHandle, BLE_ERR_REM_USER_CONN_TERM);
        m_connHandle = BLE_HS_CONN_HANDLE_NONE;
    }

    // Turn off the radio transmission signals
    stopAdvertising_();

    // Stop the NimBLE host stack and de-initialize the hardware controller
    // This safely stops the blocking 'nimble_port_run()' loop inside our OS task
    int rc = nimble_port_stop();
    if (rc != 0) {
        ESP_LOGE(TAG, "Error stopping NimBLE port: %d", rc);
    }

    // Safely kill the FreeRTOS background thread task and release memory allocations
    nimble_port_deinit();

    m_started = false;
    ESP_LOGI(TAG, "BLE Server completely stopped. Hardware and stack are offline.");
    return true;
}

void EmBleServer::initGattServices_() {
    ble_svc_gap_init();
    ble_svc_gatt_init();

    size_t flatCharIdx = 0;
    size_t flatDescIdx = 0;

    for (size_t s = 0; s < getServiceCount(); ++s) {
        EmBleService* currentSvc = m_services[s];
        size_t serviceCharStartIdx = flatCharIdx;

        for (size_t c = 0; c < currentSvc->getCharCount(); ++c) {
            EmBleCharacteristic* currentChr = currentSvc->getCharacteristic(c);
            
            // 1. FIX: Statically or dynamically instantiate a stable local variable for the UUID.
            // This prevents the "taking address of rvalue" error entirely.
            // 0x2901 is the standard 16-bit UUID for a Characteristic User Description (CUD)
            ble_uuid16_t desc_uuid = {
                .u = { .type = BLE_UUID_TYPE_16 },
                .value = 0x2901
            };

            m_runtimeDescDefinitions[flatDescIdx] = {}; 
            // Assign the address of our valid, locally created struct type
            m_runtimeDescDefinitions[flatDescIdx].uuid = &desc_uuid.u;
            m_runtimeDescDefinitions[flatDescIdx].att_flags = BLE_ATT_F_READ;
            m_runtimeDescDefinitions[flatDescIdx].access_cb = EmBleServer::descriptorAccessCallback_;
            m_runtimeDescDefinitions[flatDescIdx].arg = currentChr;

            // 2. Map out the characteristic definitions securely
            m_runtimeCharDefinitions[flatCharIdx] = {};
            m_runtimeCharDefinitions[flatCharIdx].uuid = currentChr->getUuid();
            m_runtimeCharDefinitions[flatCharIdx].access_cb = EmBleServer::characteristicAccessCallback_;
            m_runtimeCharDefinitions[flatCharIdx].arg = currentChr;
            m_runtimeCharDefinitions[flatCharIdx].descriptors = &m_runtimeDescDefinitions[flatDescIdx];
            m_runtimeCharDefinitions[flatCharIdx].flags = static_cast<int>(currentChr->getProperties());

            flatCharIdx++;
            flatDescIdx++;
        }

        // Clean layout boundary terminator mapping entry
        memset(&m_runtimeCharDefinitions[flatCharIdx++], 0, sizeof(struct ble_gatt_chr_def));

        // 3. Map out the service definition row parameters
        m_runtimeGattTable[s] = {};
        m_runtimeGattTable[s].type = BLE_GATT_SVC_TYPE_PRIMARY;
        m_runtimeGattTable[s].uuid = currentSvc->getUuid();
        m_runtimeGattTable[s].characteristics = &m_runtimeCharDefinitions[serviceCharStartIdx];
    }
    
    // Set table end marker layout configuration rules
    memset(&m_runtimeGattTable[getServiceCount()], 0, sizeof(struct ble_gatt_svc_def));

    // Register our newly constructed flat tables into the NimBLE core
    ESP_ERROR_CHECK(ble_gatts_count_cfg(m_runtimeGattTable));
    ESP_ERROR_CHECK(ble_gatts_add_svcs(m_runtimeGattTable));
}

int EmBleServer::characteristicAccessCallback_(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt* ctxt, void* arg) {
    EmBleCharacteristic* targetChr = static_cast<EmBleCharacteristic*>(arg);
    if (!targetChr) return BLE_ATT_ERR_REQ_NOT_SUPPORTED;

    switch (ctxt->op) {
        case BLE_GATT_ACCESS_OP_READ_CHR: {
            // Lets try to write to a stack buffer to avoid heap allocation for most of the requests
            EmSboBuffer <uint8_t, 64> buf(targetChr->getDataLen());
            uint16_t actualDataLen = targetChr->executeRead_(buf.getBuffer(), buf.getSize());
            
            if (actualDataLen > buf.getSize()) {
                ESP_LOGE("EmBleServer", "CRITICAL OVERFLOW Gating applied on characteristic read sequence!");
                actualDataLen = buf.getSize();
            }
            
            if (actualDataLen > 0) {
                // Safely append the raw bytes from our stack buffer into NimBLE's network packet buffer (mbuf)
                return os_mbuf_append(ctxt->om, buf.getBuffer(), actualDataLen);
            }
            return 0; // Read completed with empty payload            
        }
        case BLE_GATT_ACCESS_OP_WRITE_CHR:
            targetChr->executeWrite_(ctxt->om->om_data, ctxt->om->om_len);
            return 0;
    }
    return BLE_ATT_ERR_REQ_NOT_SUPPORTED;
}

int EmBleServer::descriptorAccessCallback_(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt* ctxt, void* arg) {
    EmBleCharacteristic* targetChr = static_cast<EmBleCharacteristic*>(arg);
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_DSC && targetChr) {
        const char* textLabel = targetChr->getDescription();
        return os_mbuf_append(ctxt->om, textLabel, strlen(textLabel));
    }
    return BLE_ATT_ERR_REQ_NOT_SUPPORTED;
}

void EmBleServer::gattRegisterCallback_(struct ble_gatt_register_ctxt *ctxt, void *arg) {
    if (ctxt->op == BLE_GATT_REGISTER_OP_CHR) {
        // FIX: Change ctxt->chr.arg to ctxt->chr.chr_def->arg
        EmBleCharacteristic* chrObj = static_cast<EmBleCharacteristic*>(ctxt->chr.chr_def->arg);
        
        if (chrObj) {
            // Save the exact numeric reference key NimBLE created for this attribute!
            chrObj->setHandle_(ctxt->chr.val_handle);
            ESP_LOGI("EmBleServer", "Mapped characteristic '%s' to Handle ID: %d", 
                     chrObj->getDescription(), ctxt->chr.val_handle);
        }
    }
}

bool EmBleServer::sendNotification(uint16_t valHandle, const uint8_t* data, uint16_t len) {
    if (!hasConnectedClient() || valHandle == 0) {
        return false;
    }
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (om) {
        int rc = ble_gatts_notify_custom(m_connHandle, valHandle, om);
        if (rc != 0) {
            ESP_LOGE("EmBleServer", "Failed to stream notification packet: %d", rc);
            return false;
        }
    }
    return true;
}

bool EmBleServer::sendIndication(uint16_t valHandle, const uint8_t* data, uint16_t len) {
    if (!hasConnectedClient() || valHandle == 0) {
        return false;
    }
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (om) {
        int rc = ble_gatts_indicate_custom(m_connHandle, valHandle, om);
        if (rc != 0) {
            ESP_LOGE("EmBleServer", "Failed to stream indication packet: %d", rc);
            return false;
        }
    }
    return true;
}

void EmBleServer::factoryReset() {
    factoryReset_();
}

void EmBleServer::factoryReset_() {
    ESP_LOGW(TAG, "Factory Resetting. Flushing all secure client identities...");
    
    if (m_connHandle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(m_connHandle, BLE_ERR_REM_USER_CONN_TERM);
    }

    stopAdvertising_();

    ble_store_util_delete_all(BLE_STORE_OBJ_TYPE_OUR_SEC, nullptr);
    ble_store_util_delete_all(BLE_STORE_OBJ_TYPE_PEER_SEC, nullptr);
    ble_store_util_delete_all(BLE_STORE_OBJ_TYPE_CCCD, nullptr);

    ble_gap_wl_set(NULL, 0);
    startAdvertising_();
}

void EmBleServer::nimbleHostTask_(void* param) {
    nimble_port_run(); 
    nimble_port_freertos_deinit();
}

void EmBleServer::onStackSync_() {
    ble_svc_gap_device_name_set(m_rawDeviceName);
    startAdvertising_();
}

void EmBleServer::onStackReset_(int reason) {
    ESP_LOGE(TAG, "Critical Stack Reset Incident! Reason: %d", reason);
}

void EmBleServer::updateWhitelist_() {
    ble_addr_t peers[MYNEWT_VAL(BLE_STORE_MAX_BONDS)];
    int num_peers = 0;
    int rc = ble_store_util_bonded_peers(peers, &num_peers, MYNEWT_VAL(BLE_STORE_MAX_BONDS));
    
    if (rc == 0 && num_peers > 0) {
        ESP_LOGI(TAG, "Hybrid Shield Activated. Loading %d Whitelist entries.", num_peers);
        ble_gap_wl_set(peers, num_peers);
    } else {
        ESP_LOGI(TAG, "Whitelist trace clear. Open to general connections.");
        ble_gap_wl_set(NULL, 0);
    }
}

void EmBleServer::startAdvertising_() {
    updateWhitelist_();

    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND; 
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN; 
    adv_params.filter_policy = BLE_HCI_ADV_FILT_CONN; // Hybrid Gate policy

    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t*)m_rawDeviceName;
    fields.name_len = m_deviceNameLen;
    fields.name_is_complete = 1;

    ble_gap_adv_set_fields(&fields);
    ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &adv_params, EmBleServer::gapEventHandler_, NULL);
    ESP_LOGI(TAG, "Hybrid active loop live.");
}

void EmBleServer::stopAdvertising_() {
    ble_gap_adv_stop();
}

int EmBleServer::gapEventHandler_(struct ble_gap_event* event, void* arg) {
    struct ble_gap_conn_desc desc;
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                m_connHandle = event->connect.conn_handle;
                ble_gap_conn_find(m_connHandle, &desc);
                if (!desc.sec_state.bonded) {
                    ESP_LOGW(TAG, "Challenging untrusted client link connection request...");
                    ble_gap_security_initiate(m_connHandle);
                }
            } else {
                startAdvertising_();
            }
            return 0;

        case BLE_GAP_EVENT_DISCONNECT:
            m_connHandle = BLE_HS_CONN_HANDLE_NONE;
            startAdvertising_();
            return 0;

        case BLE_GAP_EVENT_PASSKEY_ACTION:
            if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
                struct ble_sm_io pk;
                pk.action = event->passkey.params.action;
                pk.passkey = m_passkey; 
                return ble_sm_inject_io(event->passkey.conn_handle, &pk);
            }
            return 0;

        case BLE_GAP_EVENT_ENC_CHANGE:
            if (event->enc_change.status != 0) {
                ESP_LOGE(TAG, "Encryption handshake failed. Securing server drop.");
                ble_gap_terminate(event->enc_change.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            }
            return 0;
    }
    return 0;
}
