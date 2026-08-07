#include "em_log.h"
#include "em_https_client.h"

#include "mbedtls/base64.h"

#ifdef EM_USE_ESP_CRT_BUNDLE
#include "esp_crt_bundle.h"
#endif

bool EmHttpsClient::init() {
    esp_http_client_config_t config = {};
    config.url = "https://localhost/"; // Fake initial one
    config.event_handler = http_event_handler_;
    config.user_data = this;
    config.keep_alive_enable = true; 
    config.is_async = false;

    if (m_rootCA != nullptr) {
        config.cert_pem = m_rootCA;
    } else {
    #ifdef EM_USE_ESP_CRT_BUNDLE
        config.crt_bundle_attach = esp_crt_bundle_attach; 
    #endif
    }

    m_clientHandle = esp_http_client_init(&config);
    if (m_clientHandle == nullptr) {
        logError("EmHttpsClient", "Client initialization failed!");
    }

    return m_clientHandle != nullptr;
}

esp_err_t EmHttpsClient::http_event_handler_(esp_http_client_event_t *evt) {
    EmHttpsClient* instance = static_cast<EmHttpsClient*>(evt->user_data);
    if (!instance) {
        return ESP_OK;
    }        
    if (evt->event_id == HTTP_EVENT_ON_HEADER) {
        // This fires internally DURING esp_http_client_fetch_headers()
        if (strcasecmp(evt->header_key, "Content-Transfer-Encoding") == 0) {
            if (strcasecmp(evt->header_value, "base64") == 0) {
                instance->m_isBase64 = true;
            }
        }
        else if (strcasecmp(evt->header_key, "Content-Type") == 0) {
            if (strstr(evt->header_value, "base64") != nullptr) {
                instance->m_isBase64 = true;
            }
        }
    } else        
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        instance->appendResponseData_(static_cast<const char*>(evt->data), evt->data_len);
    }
    return ESP_OK;
}

void EmHttpsClient::cleanup_() {
    if (m_clientHandle != nullptr) {
        esp_http_client_cleanup(m_clientHandle);
        m_clientHandle = nullptr;
    }
}

// Note: We no longer pass the stream as a parameter. 
// We return true if headers are successfully parsed and ready for streaming.
bool EmHttpsClient::beginRequest_(const char* endpoint, 
                                  esp_http_client_method_t method, 
                                  const char* payload, 
                                  EmHttpsRequestStream& resStream) {
    if (!isInitialized() && !init()) {
        return false;
    }                

    if (m_openRequest) {
        logError("EmHttpsClient", "A request is already open. Call endRequest_() before starting a new one.");
        return false;
    }

    m_resMutex.lock(); // Lock the mutex to ensure exclusive access to the response stream
    m_openRequest = true;

    // Clear old headers so a previous POST doesn't corrupt a new GET request
    esp_http_client_delete_header(m_clientHandle, "Content-Type");
    esp_http_client_delete_header(m_clientHandle, "Content-Length");

    esp_http_client_set_url(m_clientHandle, endpoint);
    esp_http_client_set_method(m_clientHandle, method);
    
    // Reset the base64 flag for each new request. This flag will be set eventually
    // during the headers fetching (i.e 'esp_http_client_fetch_headers')
    m_isBase64 = false;
    
    int payloadLen = (payload != nullptr) ? strlen(payload) : 0;

    if (payloadLen > 0) {
        char len_str[16];
        snprintf(len_str, sizeof(len_str), "%d", payloadLen);
        esp_http_client_set_header(m_clientHandle, "Content-Length", len_str);
        esp_http_client_set_header(m_clientHandle, "Content-Type", "application/json"); 
    } else if (method == HTTP_METHOD_POST || method == HTTP_METHOD_PUT) {
        esp_http_client_set_header(m_clientHandle, "Content-Length", "0");
    }

    if (esp_http_client_open(m_clientHandle, payloadLen) != ESP_OK) {
        m_openRequest = false;
        m_resMutex.unlock();
        return false;
    }

    if (payloadLen > 0) {
        if (esp_http_client_write(m_clientHandle, payload, payloadLen) < 0) {
            esp_http_client_close(m_clientHandle);
            m_openRequest = false;
            m_resMutex.unlock();
            return false;
        }
    }

    // Fetch headers to prepare for reading the response. 
    int header_res = esp_http_client_fetch_headers(m_clientHandle); 
    if (header_res < 0) {
        esp_http_client_close(m_clientHandle);
        m_openRequest = false;
        m_resMutex.unlock();
        return false;
    }
    
    int status_code = esp_http_client_get_status_code(m_clientHandle);
    int response_size = esp_http_client_get_content_length(m_clientHandle);

    // Hand over the live, open connection pointers directly to the stream object
    resStream.attach_(m_clientHandle, status_code, response_size, m_isBase64);
    
    return (status_code >= 200 && status_code < 300);
}

// Separate cleanup routine once the caller finishes reading
void EmHttpsClient::endRequest_() {
    if (m_openRequest) {
        esp_http_client_close(m_clientHandle); 
        m_openRequest = false;
        m_resMutex.unlock(); 
    }                
}

bool EmHttpsClient::executeRequest_(const char* endpoint, 
                                    esp_http_client_method_t method, 
                                    const char* payload, 
                                    char* response, 
                                    size_t responseSize,
                                    bool isJsonRequest,
                                    bool& gotAllResponse) {
    if (!isInitialized() && !init()) {
        logError<50>("EmHttpsClient", "Client did not initialize correctly!");
        return false;
    }                

    logDebug<200>("EmHttpsClient", "Executing HTTP request: %s", payload);

    EmMutexLock lock(m_resMutex);
    m_resBuffer = response; 
    m_resBufferSize = responseSize;
    m_gotAllResponse = true;

    // Prepare new request
    esp_http_client_set_url(m_clientHandle, endpoint);
    esp_http_client_set_method(m_clientHandle, method);

    if (method == HTTP_METHOD_POST && payload != nullptr) {
        if (isJsonRequest) {
            esp_http_client_set_header(m_clientHandle, "Content-Type", "application/json");
        }
        
        char contentLengthStr[16] = {0};
        size_t payloadLen = strlen(payload);
        snprintf(contentLengthStr, sizeof(contentLengthStr), "%zu", payloadLen);
        esp_http_client_set_header(m_clientHandle, "Content-Length", contentLengthStr);

        esp_http_client_set_post_field(m_clientHandle, payload, (int)payloadLen);
    }

    // If the domain matches the last call, this will skip the TLS handshake completely!
    esp_err_t err = esp_http_client_perform(m_clientHandle);

    bool success = false;
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(m_clientHandle);
        if (status_code >= 200 && status_code < 300) {
            success = true;
            logDebug("EmHttpsClient", "HTTP request SUCCEED!");
        } else {
            logError<50>("EmHttpsClient", "HTTP error code: %d", status_code);
        }
    } else {
        logError<50>("EmHttpsClient", "Connection failed: %s", esp_err_to_name(err));
    }

    gotAllResponse = m_gotAllResponse;
    return success;
}

int EmHttpsRequestStream::available() {
    if (m_isEof) {
        return 0;
    }
    if (m_peekedByte != -1) {
        return 1;
    }    
    return m_availableBytes; 
}

int EmHttpsRequestStream::peek() {
    if (m_isEof) {
        return -1;
    }

    // If a standard single-byte peek cache already holds data, return it immediately
    if (m_peekedByte != -1) {
        return m_peekedByte;
    }

    // Handle Base64 Peek Logic
    if (m_isBase64) {
        // If there are already decoded bytes waiting in our internal block cache, peek the next one
        if (m_b64CachePos < m_b64CacheLen) {
            return m_b64Cache[m_b64CachePos];
        }

        // The block cache is empty. We must fetch and decode the next 4-char text block from the network
        char textBlock[4];
        size_t charsRead = 0;

        while (charsRead < 4) {
            char c;
            int res = esp_http_client_read(m_espClient, &c, 1);
            if (res > 0) {
                // Skip incoming HTTP stream layout symbols / whitespaces
                if (c == '\r' || c == '\n' || c == ' ') continue; 
                textBlock[charsRead++] = c;
            } else {
                m_isEof = true;
                break;
            }
        }

        if (charsRead == 4) {
            size_t outLen = 0;
            // Decode the 4 text characters into our internal cache
            int ret = mbedtls_base64_decode(m_b64Cache, sizeof(m_b64Cache), &outLen, 
                                            (const uint8_t*)textBlock, 4);
            if (ret == 0) {
                m_b64CacheLen = outLen;
                m_b64CachePos = 0;
                
                // Return the very first decoded byte of this freshly filled block cache
                if (m_b64CacheLen > 0) {
                    return m_b64Cache[m_b64CachePos];
                }
            } else {
                m_isEof = true;
            }
        }
        return -1; // Hit EOF or corrupted Base64 formatting data
    } else {
        // Handle Standard Raw Binary Peek Logic (Your original corrected architecture)
        uint8_t b;
        int res = esp_http_client_read(m_espClient, (char*)&b, 1);
        if (res <= 0) {
            m_isEof = true;
            return -1;
        }
        m_peekedByte = b; // Cache it so the next read() or peek() loop catches it
        return m_peekedByte;
    }
}

int EmHttpsRequestStream::read() {
    uint8_t buffer;
    size_t bytesRead = read(&buffer, 1);
    if (bytesRead == 1) {
        return buffer;
    }   
    return -1; // EOF or error
}

size_t EmHttpsRequestStream::read(uint8_t* buffer, size_t length) {
    if (m_isEof || length == 0) {
        return 0;
    }

    size_t totalBytesWrittenToCaller = 0;

    // Empty single-byte peek cache first
    if (m_peekedByte != -1) {
        buffer[totalBytesWrittenToCaller] = (uint8_t)m_peekedByte;
        m_peekedByte = -1;
        totalBytesWrittenToCaller++;
        length--;
    }

    if (m_isBase64) {
        while (length > 0 && !m_isEof) {
            // Drain leftovers from internal cache
            if (m_b64CachePos < m_b64CacheLen) {
                buffer[totalBytesWrittenToCaller] = m_b64Cache[m_b64CachePos++];
                totalBytesWrittenToCaller++;
                length--;
                continue; 
            }

            // Fetch a complete 4-char text block
            char textBlock[4];
            size_t charsRead = 0;
            
            while (charsRead < 4) {
                char c;
                int res = esp_http_client_read(m_espClient, &c, 1);
                if (res > 0) {
                    if (c == '\r' || c == '\n' || c == ' ') continue; 
                    textBlock[charsRead++] = c;
                } else {
                    m_isEof = true;
                    break;
                }
            }

            if (charsRead == 4) {
                size_t outLen = 0;
                int ret = mbedtls_base64_decode(m_b64Cache, sizeof(m_b64Cache), &outLen, 
                                                (const uint8_t*)textBlock, 4);
                if (ret == 0) {
                    m_b64CacheLen = outLen;
                    m_b64CachePos = 0;
                } else {
                    m_isEof = true;
                }
            }
        }
        
        // Accurate remaining string math correction
        if (m_availableBytes >= (int)totalBytesWrittenToCaller) {
            m_availableBytes -= totalBytesWrittenToCaller;
        } else {
            m_availableBytes = 0;
        }
    } 
    else {
        if (length > 0) {
            int res = esp_http_client_read(m_espClient, (char*)(buffer + totalBytesWrittenToCaller), length);
            if (res > 0) {
                totalBytesWrittenToCaller += res;
                m_availableBytes -= res;
            } else if (res <= 0) {
                m_isEof = true;
            }
        }
    }

    return totalBytesWrittenToCaller;
}

