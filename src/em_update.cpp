#include "em_update.h"

#include <esp_log.h>

static const char* TAG = "EmUpdate";

EmUpdate::EmUpdate() 
    : m_updateHandle(0), m_updatePartition(nullptr), m_isRunning(false), m_size(0) {}

EmUpdate::~EmUpdate() {
    if (m_isRunning) {
        abort();
    }
}

/**
 * Initializes the OTA update process and finds the next boot partition.
 */
bool EmUpdate::begin(size_t size) {
    if (m_isRunning) {
        return false;
    }

    m_size = size;
    
    // Find the next available OTA partition to write into
    m_updatePartition = esp_ota_get_next_update_partition(NULL);
    if (m_updatePartition == NULL) {
        ESP_LOGE(TAG, "Passive OTA partition not found");
        return false;
    }

    ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%lx",
             m_updatePartition->subtype, m_updatePartition->address);

    // Initialize the OTA process
    esp_err_t err = esp_ota_begin(m_updatePartition, m_size, &m_updateHandle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
        return false;
    }

    m_isRunning = true;
    return true;
}

size_t EmUpdate::write(const uint8_t* data, size_t len) {
    if (!m_isRunning) {
        return 0;
    }

    esp_err_t err = esp_ota_write(m_updateHandle, data, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write failed (%s)", esp_err_to_name(err));
        abort();
        return 0;
    }

    return len;
}

size_t EmUpdate::writeStream(EmStream& stream) {
    // TODO CABBI
    return 0;
}

bool EmUpdate::end() {
    if (!m_isRunning) {
        return false;
    }

    m_isRunning = false;

    // End the OTA handle session
    esp_err_t err = esp_ota_end(m_updateHandle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "Image validation failed, image is corrupted");
        } else {
            ESP_LOGE(TAG, "esp_ota_end failed (%s)", esp_err_to_name(err));
        }
        return false;
    }

    // Set the freshly written partition as the boot target for next restart
    err = esp_ota_set_boot_partition(m_updatePartition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "OTA update successful! Ready to reboot.");
    return true;
}

void EmUpdate::abort() {
    if (m_isRunning) {
        esp_ota_abort(m_updateHandle);
        m_isRunning = false;
        ESP_LOGW(TAG, "OTA operation aborted.");
    }
}
