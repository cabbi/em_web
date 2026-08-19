#ifndef __EM_BLE_SERVICE_H__
#define __EM_BLE_SERVICE_H__

#include <vector>
#include <string.h>
#include "em_ble_characteristic.h"

class EmBleService {
public:
    EmBleService(const ble_uuid128_t& uuid) {
        memcpy(&m_uuid128, &uuid, sizeof(ble_uuid128_t));
    }

    // Prevent copying the service itself to protect the internal pointer list
    EmBleService(const EmBleService&) = delete;
    EmBleService& operator=(const EmBleService&) = delete;
    ~EmBleService() = default;

    void addCharacteristic(EmBleCharacteristic& chr) {
        // Take the address internally using '&' to store it in the vector
        m_characteristics.push_back(&chr);
    }

    const ble_uuid_t* getUuid() const { return &m_uuid128.u; }
    size_t getCharCount() const { return m_characteristics.size(); }
    
    EmBleCharacteristic* getCharacteristic(size_t index) { 
        if (index >= m_characteristics.size()) {
            return nullptr;
        }
        return m_characteristics[index]; 
    }

private:
    ble_uuid128_t m_uuid128;
    std::vector<EmBleCharacteristic*> m_characteristics;
};

#endif // __EM_BLE_SERVICE_H__
