#include "em_ble_characteristic.h"
#include "em_ble_server.h"


bool EmBleCharacteristic::updateAndNotify(const uint8_t* source, uint16_t len) {
    // Update the user object by calling its callback
    if (!executeWrite_(source, len)) {
        return false;
    }

    // Inspect active properties using bitwise checks
    bool hasNotify   = (m_properties & EmBleGattProperty::Notify);
    bool hasIndicate = (m_properties & EmBleGattProperty::Indicate);

    // Smart Router Strategy
    if (hasIndicate) {
        // If they asked for Guaranteed Delivery, give priority to Indication
        return EmBleServer::sendIndication(m_valHandle, source, len);
    } 
    else if (hasNotify) {
        // Fallback to fast, lightweight Notification
        return EmBleServer::sendNotification(m_valHandle, source, len);
    } 
    ESP_LOGE("EmBleCharacteristic", "Error pushing '%s': Neither Notify nor Indicate are set!", m_description);
    return false;
}

void EmBleCharacteristic::uuidFromName(const char* name, ble_uuid128_t& uuid) {
    // Define your stable custom Namespace UUID
    static const uint8_t baseNamespace[16] = {
        0xAA, 0xBB, 0xCC, 0xDD, 0x11, 0x22, 0x33, 0x44,
        0x55, 0x66, 0x77, 0x88, 0x99, 0x00, 0x11, 0x22
    };

    // Initialize the PSA Crypto subsystem (Safe to call multiple times)
    psa_crypto_init();

    // Setup the multi-part SHA-1 operation structure 
    psa_hash_operation_t hash_op = PSA_HASH_OPERATION_INIT;
    size_t out_len = 0;
    uint8_t sha1Result[64] = {0}; // SHA-1 returns a 20-byte digest

    // Setup execution parameters for the SHA-1 algorithm block
    psa_status_t status = psa_hash_setup(&hash_op, PSA_ALG_SHA_1);
    if (status != PSA_SUCCESS) {
        return; // Failed to prepare hashing engine
    }

    // Feed the constant Namespace signature bytes 
    psa_hash_update(&hash_op, baseNamespace, 16);

    // Feed your string name characters directly into the pipeline
    psa_hash_update(&hash_op, (const uint8_t*)name, strlen(name));

    // Finalize the cryptographic sequence extraction
    status = psa_hash_finish(&hash_op, sha1Result, sizeof(sha1Result), &out_len);
    if (status != PSA_SUCCESS) {
        return; // Extraction calculation fault
    }

    // Truncate the 20-byte result into a 16-byte UUID buffer space
    uint8_t finalUuidBytes[16];
    memcpy(finalUuidBytes, sha1Result, 16);

    // Set standard RFC 4122 bits to declare a legitimate Name-Based Version 5 UUID
    finalUuidBytes[6] = (finalUuidBytes[6] & 0x0F) | 0x50; // Force Version 5 signature
    finalUuidBytes[8] = (finalUuidBytes[8] & 0x3F) | 0x80; // Force Variant 1 signature

    // Directly map the bytes into your native pre-cached NimBLE layout member variable
    memcpy(uuid.value, finalUuidBytes, sizeof(uuid.value));
    uuid.u.type = BLE_UUID_TYPE_128;
}
