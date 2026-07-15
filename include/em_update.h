#ifndef __EM_UPDATE_H
#define __EM_UPDATE_H 

#include "em_defs.h"

#ifdef ESP_PLATFORM

#include "esp_ota_ops.h"
#include "esp_partition.h"

class EmUpdate {
private:
    esp_ota_handle_t _update_handle;
    const esp_partition_t* _update_partition;
    bool _is_running;
    size_t _size;

public:
    EmUpdate();
    ~EmUpdate();

    bool begin(size_t size = OTA_SIZE_UNKNOWN);
    size_t write(const uint8_t* data, size_t len);
    bool end();
    void abort();
};
#endif //ESP_PLATFORM

#endif //__EM_UPDATE_H
