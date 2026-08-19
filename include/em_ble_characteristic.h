#ifndef __EM_BLE_CHARACTERISTIC_H__
#define __EM_BLE_CHARACTERISTIC_H__

#include <stdint.h>

#include "host/ble_hs.h"
#include "psa/crypto.h"

// Forward declaration
class EmBleServer;

// Strongly typed enum to mask complex native BLE GATT bitmask macros
enum class EmBleGattProperty : uint16_t {
    None         = 0,
    Read         = BLE_GATT_CHR_F_READ,          // Simple open read (Insecure)
    Write        = BLE_GATT_CHR_F_WRITE,         // Simple open write (Insecure)
    WriteNoRsp   = BLE_GATT_CHR_F_WRITE_NO_RSP,  // Fast write without acknowledgment
    ReadSecure   = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC,   // PIN-protected secure reading
    WriteSecure  = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC, // PIN-protected secure writing
    Notify       = BLE_GATT_CHR_F_NOTIFY,        // Enables server-to-phone data streaming
    Indicate     = BLE_GATT_CHR_F_INDICATE       // Enables streaming with client delivery confirmations
};

// Overload the | operator to allow combining properties seamlessly
inline EmBleGattProperty operator|(EmBleGattProperty lhs, EmBleGattProperty rhs) {
    return static_cast<EmBleGattProperty>(
        static_cast<uint16_t>(lhs) | static_cast<uint16_t>(rhs)
    );
}

// Overload the & operator for internal flag checking logic
inline bool operator&(EmBleGattProperty lhs, EmBleGattProperty rhs) {
    return (static_cast<uint16_t>(lhs) & static_cast<uint16_t>(rhs)) != 0;
}

// The user defined BLE characteristic.
//
// Add your characteristic to the EmBleService object and add it to the EmBleServer.
class EmBleCharacteristic {
    friend class EmBleServer;
public:
    // Define advanced C-style function pointer signatures with user context arguments
    typedef uint16_t (*ReadCallback)(void* arg, uint8_t* dest, uint16_t maxLen);
    typedef bool (*WriteCallback)(void* arg, const uint8_t* source, uint16_t len);

    EmBleCharacteristic(const char* name, // Used to compute the characteristic's UUID
                        const char* userDescription,
                        uint16_t dataLen,
                        EmBleGattProperty properties, 
                        ReadCallback readCallback,
                        WriteCallback writeCallback,
                        void* userArg = nullptr)
     : m_properties(properties), 
       m_description(userDescription),
       m_dataLen(dataLen),
       m_readCb(readCallback),
       m_writeCb(writeCallback),
       m_userCbArg(userArg) {
        ble_uuid128_t uuid;
        uuidFromName(name, uuid);
        memcpy(&m_uuid128, &uuid, sizeof(ble_uuid128_t));
    }

    EmBleCharacteristic(const ble_uuid128_t& uuid, 
                        const char* userDescription,
                        EmBleGattProperty properties, 
                        ReadCallback readCallback,
                        WriteCallback writeCallback,
                        void* userArg = nullptr)
     : m_properties(properties), 
       m_description(userDescription),
       m_readCb(readCallback),
       m_writeCb(writeCallback),
       m_userCbArg(userArg) {
        memcpy(&m_uuid128, &uuid, sizeof(ble_uuid128_t));
    }

    // Prevent copying the characteristic itself to protect the service internal pointer list
    EmBleCharacteristic(const EmBleCharacteristic&) = delete;
    EmBleCharacteristic& operator=(const EmBleCharacteristic&) = delete;
    ~EmBleCharacteristic() = default;

    bool updateAndNotify(const uint8_t* source, uint16_t len);

    const ble_uuid_t* getUuid() const { return &m_uuid128.u; }
    EmBleGattProperty getProperties() const { return m_properties; }
    const char* getDescription() const { return m_description; }
    uint16_t getDataLen() const { return m_dataLen; }
    uint16_t getHandle() const { return m_valHandle; }

    static void uuidFromName(const char* name, ble_uuid128_t& uuid);

protected:
    void setHandle_(uint16_t handle) { 
        m_valHandle = handle; 
    }

    uint16_t executeRead_(uint8_t* dest, uint16_t maxLen) const {
        if (m_readCb) {
             return m_readCb(m_userCbArg, dest, maxLen);
        }
        return 0;
    }

    bool executeWrite_(const uint8_t* source, uint16_t len) const {
        if (m_writeCb) {
            return m_writeCb(m_userCbArg, source, len);
        }
        return false;
    }

private:
    ble_uuid128_t m_uuid128;
    EmBleGattProperty m_properties;
    const char* m_description;
    uint16_t m_dataLen;
    uint16_t m_valHandle = 0; // BLE Server will assign this once started!

    ReadCallback m_readCb;
    WriteCallback m_writeCb;
    void* m_userCbArg; // The stored user execution context pointer
};

#endif //__EM_BLE_CHARACTERISTIC_H__
