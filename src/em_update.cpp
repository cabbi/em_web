#include "EmUpdate.hpp"
#include "esp_log.h"

static const char* TAG = "EmUpdate";

EmUpdate::EmUpdate() 
    : _update_handle(0), _update_partition(nullptr), _is_running(false), _size(0) {}

EmUpdate::~EmUpdate() {
    if (_is_running) {
        abort();
    }
}

/**
 * Initializes the OTA update process and finds the next boot partition.
 */
bool EmUpdate::begin(size_t size) {
    if (_is_running) {
        ESP_LOGW(TAG, "OTA update already running");
        return false;
    }

    _size = size;
    
    // Find the next available OTA partition to write into
    _update_partition = esp_ota_get_next_update_partition(NULL);
    if (_update_partition == NULL) {
        ESP_LOGE(TAG, "Passive OTA partition not found");
        return false;
    }

    ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%lx",
             _update_partition->subtype, _update_partition->address);

    // Initialize the OTA process
    esp_err_t err = esp_ota_begin(_update_partition, _size, &_update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
        return false;
    }

    _is_running = true;
    return true;
}

/**
 * Writes a chunk of the binary firmware array to the flash memory.
 */
size_t EmUpdate::write(const uint8_t* data, size_t len) {
    if (!_is_running) {
        return 0;
    }

    esp_err_t err = esp_ota_write(_update_handle, data, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write failed (%s)", esp_err_to_name(err));
        abort();
        return 0;
    }

    return len;
}

/**
 * Validates the written image and sets it as the next boot target.
 */
bool EmUpdate::end() {
    if (!_is_running) {
        return false;
    }

    _is_running = false;

    // End the OTA handle session
    esp_err_t err = esp_ota_end(_update_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "Image validation failed, image is corrupted");
        } else {
            ESP_LOGE(TAG, "esp_ota_end failed (%s)", esp_err_to_name(err));
        }
        return false;
    }

    // Set the freshly written partition as the boot target for next restart
    err = esp_ota_set_boot_partition(_update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "OTA update successful! Ready to reboot.");
    return true;
}

/**
 * Terminate the update if a network drops or a packet fails mid-air.
 */
void EmUpdate::abort() {
    if (_is_running) {
        esp_ota_abort(_update_handle);
        _is_running = false;
        ESP_LOGW(TAG, "OTA operation aborted.");
    }
}
