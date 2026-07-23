#ifndef __EM_UPDATE_H
#define __EM_UPDATE_H 

#include "em_defs.h"

#ifdef ARDUINO

#include <Update.h>
using EmFirmwareUpdate = Update;

#elif ESP_PLATFORM
#include "em_log.h"
#include "em_ota_updater.h"

#include <esp_ota_ops.h>
#include <esp_partition.h>

// The firmware update class
class EmFirmwareUpdate {
public:
    EmFirmwareUpdate();
    ~EmFirmwareUpdate();

    bool begin(size_t size = OTA_SIZE_UNKNOWN);
    size_t writeStream(EmStream& stream);
    bool end();
    void abort();

protected:
    size_t writeBytes_(const uint8_t* data, size_t len);

private:
    esp_ota_handle_t m_updateHandle;
    const esp_partition_t* m_updatePartition;
    bool m_isRunning;
    size_t m_size;    
    size_t m_bytesWritten;
};

// The ESP OTA Updater class
class Esp32OtaUpdater: public EmOtaUpdater {
public:
    virtual bool update(EmStream& client, size_t contentLength) override {
        EmFirmwareUpdate update;
        if (!update.begin(contentLength)) {
            logError("Esp32OtaUpdater", "Not enough space to begin OTA");
            return false;
        }
        size_t written = update.writeStream(client);
        if (written == 0) {
            logError("Esp32OtaUpdater", "Update failed");
            return false;
        } 
        logInfo("Esp32OtaUpdater", "Update successful!");
        update.end();
        return true;
    }

    virtual bool finalize() { 
        restart();
        return true; 
    }
};

#else
    #error "Unsupported platform!"
#endif 

#endif //__EM_UPDATE_H
