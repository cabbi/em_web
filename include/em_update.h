#ifndef __EM_UPDATE_H
#define __EM_UPDATE_H 

#include "em_defs.h"

#ifdef ARDUINO

#include <Update.h>
using EmUpdate = Update;

#elif ESP_PLATFORM
#include "em_stream.h"

#include <esp_ota_ops.h>
#include <esp_partition.h>

// The firmware update class
class EmUpdate {
public:
    EmUpdate();
    ~EmUpdate();

    bool begin(size_t size = OTA_SIZE_UNKNOWN);
    size_t write(const uint8_t* data, size_t len);
    size_t writeStream(EmStream& stream);
    bool end();
    void abort();

private:
    esp_ota_handle_t m_updateHandle;
    const esp_partition_t* m_updatePartition;
    bool m_isRunning;
    size_t m_size;    
};
#else
    #error "Unsupported platform!"
#endif 

#endif //__EM_UPDATE_H
