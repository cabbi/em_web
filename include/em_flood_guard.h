#ifndef __EM_FLOOD_GUARD_H__
#define __EM_FLOOD_GUARD_H__

#include <map>

#include "em_string_map.h"
#include "em_timeout.h"

// TODO: implement clock based flood guard that stores previous clock time in NVS so that flood 
//       guard prevention is also available on device restarts (i.e. prevent sending on each reboot)

// The abstract "flood guard" protection interface definition.
// This set of classes are used to prevent endpoint "flooding" operations 
// (i.e. hammering an endpoint with a lot of requests).
class EmFloodGuard {
public:
    virtual bool canSend(const char* endpoint) = 0;
};

// The "flood guard" protection applies globally to any endpoint (e.g. if you send a request 
// to endpoint A, 'sendTimeout' must expire before sending a request to different endpoint B)
class EmGlobalEndpointGuard: public EmFloodGuard {
public:
    EmGlobalEndpointGuard(EmDuration sendTimeout)
     : m_sendTimeout(sendTimeout) {}

    virtual bool canSend(const char* /*endpoint*/) override {
        return m_sendTimeout.isExpired(true);
    }

protected:
    EmTimeout m_sendTimeout;
};

// The "flood guard" protection applies per each endpoint (e.g. if you send a request to endpoint A,
// 'sendTimeout' must NOT expire before sending a request to endpoint B).
class EmPerEndpointGuard: public EmFloodGuard {
public:
    EmPerEndpointGuard(EmDuration sendTimeout)
     : m_sendTimeout(sendTimeout) {}

    virtual bool canSend(const char* endpoint) override {
        EmTimeout* timeout = nullptr;
        if (m_endpointTimeouts.get(endpoint, timeout)) {
            // Endpoint was called before, lets check its timeout
            return timeout->isExpired(true);
        }
        // First call for this endpoint, create a new timeout entry.
        m_endpointTimeouts.set(endpoint, new EmTimeout(m_sendTimeout));
        return true;
    }

protected:
    EmStringMap<EmTimeout*> m_endpointTimeouts;
    EmDuration m_sendTimeout;
};

// A fake "no guard" implementation to be used as default: no "flood" protection!
class EmNoFloodGuard: public EmFloodGuard {
public:
    virtual bool canSend(const char* /*endpoint*/) override { return true; }
};

// The default "no flood guard" protection object (used as default parameter)
inline EmNoFloodGuard noFloodGuard;

#endif // __EM_FLOOD_GUARD_H__