#ifndef __HTTPS_CLIENT_H
#define __HTTPS_CLIENT_H

#include <esp_http_client.h>

#include "em_defs.h"
#include "em_string.h"
#include "em_stream.h"
#include "em_threading.h"
#include "em_json_writer.h"

// NOTE:
// To reduce flash memory consumption we disable by default the use of the ESP32 internal certificate bundle.
// If you want to use it, define EM_USE_ESP_CRT_BUNDLE in your project build flags.

// The stream class used to make a request and get streamed data from the server. 
// This is useful for large responses that don't fit in memory and supports Base64 encoded data.
class EmHttpsRequestStream;

// The EmHttpsClient class provides a simple interface for making HTTPS requests to a server.
class EmHttpsClient {
    friend class EmHttpsRequestStream;
public:    
    // Constructor for the Https Client.
    // 'root_ca' is an optional PEM format Root CA certificate string.
    // Defaults to nullptr, which triggers the internal hardware certificate bundle
    // if EM_USE_ESP_CRT_BUNDLE is defined.
    EmHttpsClient(const char* rootCA) 
     : m_clientHandle(nullptr), 
       m_rootCA(rootCA),
       m_resBuffer(nullptr),   // response buffer is set on request call, a mutex is used
       m_resBufferSize(0),     // to protect the response buffer from concurrent access
       m_openRequest(false),
       m_gotAllResponse(false),
       m_isBase64(false) {}
    
    virtual ~EmHttpsClient() {
        cleanup_();
    }

    bool init();

    bool isInitialized() const {
        return m_clientHandle != nullptr;
    }

    bool postJson(const char* endpoint, 
                  const char* cmd,
                  const char* params) {
        EmStringXL request;
        EmJsonDictWriter reqWriter(request);
        reqWriter.addString("cmd", cmd);
        reqWriter.addObject("params", params);
		reqWriter.end();
        bool gotAllResponse = false;
        return postJson(endpoint, request.c_str(), nullptr, 0, gotAllResponse);
    }

    bool postJson(const char* endpoint, 
                  const char* cmd,
                  const char* params, 
                  EmStringBase& response, 
                  bool& gotAllResponse) {
        return postJson(endpoint, cmd, params, response.buffer(), response.capacity(), gotAllResponse);
    }

    bool postJson(const char* endpoint, 
                  const char* cmd,
                  const char* params, 
                  char* response, 
                  size_t responseSize, 
                  bool& gotAllResponse) {
        EmStringXL request;
        EmJsonDictWriter reqWriter(request);
        reqWriter.addString("cmd", cmd);
        reqWriter.addObject("params", params);
		reqWriter.end();
        return postJson(endpoint, request.c_str(), response, responseSize, gotAllResponse);
    }

    bool postJson(const char* endpoint, const char* jsonPayload, EmStringBase& response, bool& gotAllResponse) {
        return executeRequest_(endpoint, HTTP_METHOD_POST, jsonPayload, response.buffer(), response.capacity(), true, gotAllResponse);
    }
    bool postJson(const char* endpoint, const char* jsonPayload, char* response, size_t responseSize, bool& gotAllResponse) {
        return executeRequest_(endpoint, HTTP_METHOD_POST, jsonPayload, response, responseSize, true, gotAllResponse);
    }

    bool get(const char* endpoint, EmStringBase& response, bool& gotAllResponse) {
        return executeRequest_(endpoint, HTTP_METHOD_GET, nullptr, response.buffer(), response.capacity(), false, gotAllResponse);
    }
    bool get(const char* endpoint, char* response, size_t responseSize, bool& gotAllResponse) {
        return executeRequest_(endpoint, HTTP_METHOD_GET, nullptr, response, responseSize, false, gotAllResponse);
    }

protected:
    static esp_err_t http_event_handler_(esp_http_client_event_t *evt);
    void cleanup_();
    bool executeRequest_(const char* endpoint, 
                         esp_http_client_method_t method, 
                         const char* payload, 
                         char* response, 
                         size_t responseSize,
                         bool isJsonRequest,
                         bool& gotAllResponse);

    void appendResponseData_(const char* data, size_t len) {
        if (m_resBuffer && m_resBufferSize > 0) {
            size_t currentLen = strlen(m_resBuffer);
            size_t spaceLeft = m_resBufferSize - currentLen - 1; // -1 for null terminator
            size_t toCopy = (len < spaceLeft) ? len : spaceLeft;
            m_gotAllResponse = spaceLeft >= len;
            if (toCopy > 0) {
                strncat(m_resBuffer, data, toCopy);
            }
            logInfo<100>("EmHttpsClient", "--->%s [len=%zu, spaceLeft=%zu, gotAllResponse=%s]", m_resBuffer, len, spaceLeft, m_gotAllResponse ? "true" : "false");
        }
    }

    // Streamed requests using EmHttpsRequestStream
    bool beginPostRequest_(const char* endpoint, 
                           const char* payload, 
                           EmHttpsRequestStream& resStream) {
        return beginRequest_(endpoint, HTTP_METHOD_POST, payload, resStream);
    }

    bool beginGetRequest_(const char* endpoint, 
                          const char* payload, 
                          EmHttpsRequestStream& resStream) {
        return beginRequest_(endpoint, HTTP_METHOD_GET, payload, resStream);
    }

    bool beginRequest_(const char* endpoint, 
                       esp_http_client_method_t method, 
                       const char* payload, 
                       EmHttpsRequestStream& resStream);

    void endRequest_();

private:
    esp_http_client_handle_t m_clientHandle;
    const char* m_rootCA;
    EmMutex m_resMutex;
    char* m_resBuffer;
    size_t m_resBufferSize;
    ts_bool m_openRequest;
    ts_bool m_gotAllResponse;
    ts_bool m_isBase64;
};

// A helper class to manage the lifecycle of a streamed request.
// This class is used to make a request and read the response in a streaming manner, 
// which is useful for large responses that don't fit in memory.
// This class automatically supports Base64 encoded responses and decodes them on-the-fly.
class EmHttpsRequestStream: public EmStreamRx {
    friend class EmHttpsClient;
public:
    EmHttpsRequestStream(EmHttpsClient& client,
                         const char* endpoint, 
                         esp_http_client_method_t method, 
                         const char* payload)
        : m_httpsClient(client) {
        m_isValid = m_httpsClient.beginRequest_(endpoint, method, payload, *this);
    }
        
    ~EmHttpsRequestStream() {
        end();
    }   

    void end() {
        m_httpsClient.endRequest_();
    }

    // Checks if the request was successfully initiated and the stream is valid for reading.
    bool isValid() const { return m_isValid; }

    int getStatusCode() const { return m_statusCode; }
    int getContentLength() const { return m_contentLength; }

    // Stream reading methods implementation 
    virtual int available() override;
    virtual int peek() override;
    virtual int read() override;
    virtual size_t read(uint8_t* buffer, size_t length) override;

private:
    void attach_(esp_http_client_handle_t client, int statusCode, int contentLength, bool isBase64) {
        m_espClient = client;
        m_statusCode = statusCode;
        m_isBase64 = isBase64;
        m_isEof = false;
        m_peekedByte = -1;
        m_b64CacheLen = 0;
        m_b64CachePos = 0;
        // Handle length adjustments for Base64 streams
        if (isBase64 && contentLength > 0) {
            m_contentLength = (contentLength / 4) * 3; 
        } else {
            m_contentLength = contentLength;
        }
        
        m_availableBytes = m_contentLength; 
    }

    // Member vars
    EmHttpsClient& m_httpsClient;
    esp_http_client_handle_t m_espClient = nullptr;
    int m_statusCode = 0;
    int m_contentLength = 0;
    bool m_isEof = false;
    int m_peekedByte = -1; 
    int m_availableBytes = 0; 
    bool m_isBase64 = false;
    bool m_isValid = false;
    uint8_t m_b64Cache[4];         // Stores leftover decoded binary bytes
    size_t m_b64CacheLen = 0;      // How many bytes are currently in the cache
    size_t m_b64CachePos = 0;      // Current read position in the cache
};

#endif // __HTTPS_CLIENT_H
