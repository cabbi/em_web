#ifndef __EM_BLE_SERVER_H__
#define __EM_BLE_SERVER_H__

// NimBLE Includes
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "em_string.h"
#include "em_threading.h"
#include "em_ble_service.h"

// A BLE server class that allows devices to pair and query device data
// 
// You might call in order the 'init' method, then add your application services via 'addService'
// and finally call the 'start' method to let the BLE server start advertising and handling requests.
class EmBleServer {
public:
    EmBleServer() = delete;

    // Public Lifecycle Interface Pipeline
    static void init(const EmStringBase& deviceName, uint32_t passkey = 123456);
    static bool addService(EmBleService& service); 
    static bool start();
    static bool stop();

    static void factoryReset();

    static size_t getServiceCount() { return m_services.size(); }

    static bool hasConnectedClient() { return m_connHandle != BLE_HS_CONN_HANDLE_NONE; }
    static uint16_t getConnectionHandle() { return m_connHandle; }

    // Send a "best effort" notification to a connected client if any
    static bool sendNotification(uint16_t valHandle, const uint8_t* data, uint16_t len);
    
    // Send a acknowledged indication to a connected client if any
    static bool sendIndication(uint16_t valHandle, const uint8_t* data, uint16_t len);

private:
    // Notification handling
    static void gattRegisterCallback_(struct ble_gatt_register_ctxt *ctxt, void *arg);

    // Internal Callbacks and Task Handlers (Trailing Underscore)
    static void nimbleHostTask_(void* param);
    static void onStackSync_();
    static void onStackReset_(int reason);
    static int gapEventHandler_(struct ble_gap_event* event, void* arg);
    static int characteristicAccessCallback_(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt* ctxt, void* arg);
    static int descriptorAccessCallback_(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt* ctxt, void* arg);

    // Private Component Logic Engines
    static void startAdvertising_();
    static void stopAdvertising_();
    static void updateWhitelist_();
    static void initGattServices_();
    static void factoryReset_(); 

    // Server State Management Allocations
    inline static const char* m_rawDeviceName = "ESP32_Server"; 
    inline static uint8_t m_deviceNameLen = 12;
    inline static uint32_t m_passkey = 123456;
    inline static ts_bool m_isInitialized = false;
    inline static ts_bool m_started = false;
    inline static uint16_t m_connHandle = BLE_HS_CONN_HANDLE_NONE;

private:
    inline static std::vector<EmBleService*> m_services;
    
    // Flat static storage tables mapped out explicitly for NimBLE engine registration
    inline static struct ble_gatt_svc_def m_runtimeGattTable[6]; 
    inline static struct ble_gatt_chr_def m_runtimeCharDefinitions[32];
    inline static struct ble_gatt_dsc_def m_runtimeDescDefinitions[32];

    inline static const char* TAG = "EmBleServer";
};

#endif //__EM_BLE_SERVER_H__
