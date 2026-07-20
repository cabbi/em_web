#ifndef __EM_OTA_UPDATER_H__
#define __EM_OTA_UPDATER_H__

#include "em_defs.h"
#include "em_stream.h"

// The interface of a generic OTA updater.
//
// The split between 'update' and 'finalize' is due to the possibility to perform
// multiple updates at once and restating the device would break the firmware 
// updating process. That said, if an update process requires multiple devices update, 
// we first call 'update' for each device 'EmOtaUpdater' and then all the 'finalize'.  
// As an example we might have two 'EmOtaUpdater' objects to update a display and the
// MPU itself. If the updates restarts the MPU we would break the whole update process.
class EmOtaUpdater {
public:
    // Updates the firmware
    virtual bool update(EmStream& client, size_t contentLength) = 0;

    // Extra step to finalize the firmware update 
    // (i.e. if applicable apply/commit/reboot the device)
    virtual bool finalize() { return true; }
};

#ifdef EM_ESP
#include "em_update.h"
#include "em_log.h"

class Esp32OtaUpdater: public EmOtaUpdater {
public:
    virtual bool update(EmStream& client, size_t contentLength) override {
        EmUpdate update;
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
#endif // EM_ESP

#endif // __EM_OTA_UPDATER_H__

