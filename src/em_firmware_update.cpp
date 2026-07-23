#include "em_firmware_update.h"
#include "esp_log.h"

static const char* TAG = "EmFirmwareUpdate";
constexpr size_t INTERNAL_BUFFER_SIZE = 1024; // Chunk size used during stream reads

EmFirmwareUpdate::EmFirmwareUpdate() 
    : m_updateHandle(0), 
      m_updatePartition(nullptr), 
      m_isRunning(false), 
      m_size(0),
      m_bytesWritten(0) {}

EmFirmwareUpdate::~EmFirmwareUpdate() {
    // Safety teardown if the user forgets to call end() or abort()
    if (m_isRunning) {
        abort();
    }
}

bool EmFirmwareUpdate::begin(size_t size) {
    if (m_isRunning) {
        ESP_LOGE(TAG, "Update already running!");
        return false;
    }

    m_size = size;
    m_bytesWritten = 0;

    // Find the next available OTA partition slot
    m_updatePartition = esp_ota_get_next_update_partition(nullptr);
    if (m_updatePartition == nullptr) {
        ESP_LOGE(TAG, "Passive OTA partition not found!");
        return false;
    }

    ESP_LOGI(TAG, "Writing to partition %s at offset 0x%08X (Size: %d bytes)", 
             m_updatePartition->label, m_updatePartition->address, m_updatePartition->size);

    // Initialize the OTA operation. This erases the target partition blocks.
    esp_err_t err = esp_ota_begin(m_updatePartition, m_size, &m_updateHandle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed! Error: %s", esp_err_to_name(err));
        m_updateHandle = 0;
        m_updatePartition = nullptr;
        return false;
    }

    m_isRunning = true;
    ESP_LOGI(TAG, "OTA Update initialized successfully.");
    return true;
}

size_t EmFirmwareUpdate::writeStream(EmStream& stream) {
    if (!m_isRunning) {
        return 0;
    }

    uint8_t buffer[INTERNAL_BUFFER_SIZE];
    size_t totalStreamWritten = 0;

    // Keep pulling bytes from the stream while data is available
    while (stream.available() > 0) {
        // Calculate maximum safe chunk size to read
        size_t bytesToRead = sizeof(buffer);
        if (m_size != OTA_SIZE_UNKNOWN) {
            size_t remainingSpace = m_size - m_bytesWritten;
            if (remainingSpace < bytesToRead) {
                bytesToRead = remainingSpace;
            }
        }

        if (bytesToRead == 0) break; // Finished writing max allowed size

        // Read binary segment out of the virtualized stream interface
        size_t readBytes = stream.read(buffer, bytesToRead);
        if (readBytes == 0) {
            // Stream reported data available but read zero bytes (timeout or connection loss)
            break; 
        }

        // Pass chunk over to our local flash write system
        size_t written = writeBytes_(buffer, readBytes);
        totalStreamWritten += written;

        if (written != readBytes) {
            ESP_LOGE(TAG, "Flash write mismatch during streaming execution.");
            break; // Something went wrong inside write()
        }
    }

    return totalStreamWritten;
}

size_t EmFirmwareUpdate::writeBytes_(const uint8_t* data, size_t len) {
    if (!m_isRunning || data == nullptr || len == 0) {
        return 0;
    }

    // Safety constraint: Prevent writing more data than declared in begin()
    if (m_size != OTA_SIZE_UNKNOWN && (m_bytesWritten + len) > m_size) {
        ESP_LOGE(TAG, "Attempted to write past declared OTA size! Truncating incoming payload.");
        len = m_size - m_bytesWritten;
        if (len == 0) return 0;
    }

    esp_err_t err = esp_ota_write(m_updateHandle, data, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write failed! Error: %s", esp_err_to_name(err));
        abort(); // Auto-abort operation on critical flash failure
        return 0;
    }

    m_bytesWritten += len;
    return len;
}

bool EmFirmwareUpdate::end() {
    if (!m_isRunning) {
        return false;
    }

    // Check if the exact expected payload size was satisfied (if configured)
    if (m_size != OTA_SIZE_UNKNOWN && m_bytesWritten != m_size) {
        ESP_LOGE(TAG, "Size mismatch error! Expected: %zu, Received: %zu", m_size, m_bytesWritten);
        abort();
        return false;
    }

    // Validate the image signature and finalize the flashing procedure
    esp_err_t err = esp_ota_end(m_updateHandle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed! Error: %s", esp_err_to_name(err));
        m_isRunning = false;
        m_updateHandle = 0;
        return false;
    }

    // Direct the ESP32 to switch its primary boot target to this new application slot
    err = esp_ota_set_boot_partition(m_updatePartition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set boot partition! Error: %s", esp_err_to_name(err));
        m_isRunning = false;
        m_updateHandle = 0;
        return false;
    }

    ESP_LOGI(TAG, "OTA Upgrade Successful! Ready to reboot device.");
    m_isRunning = false;
    m_updateHandle = 0;
    return true;
}

void EmFirmwareUpdate::abort() {
    if (!m_isRunning) {
        return;
    }

    // Cancel ongoing transaction handles and drop partition targets safely
    if (m_updateHandle != 0) {
        esp_ota_abort(m_updateHandle);
    }
    
    m_updateHandle = 0;
    m_updatePartition = nullptr;
    m_isRunning = false;
    ESP_LOGW(TAG, "OTA Operation Aborted cleanly.");
}
