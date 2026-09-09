///
/// @file
/// @brief ESP32 Async Web Server
/// 
///====================================================================
#ifndef _XSERVER_H
#define _XSERVER_H

#include <map>
#include <string_view>
#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "xque.h"

// Fixed size configurations to eliminate dynamic heap allocations
#define SES_BUF_SZ 512

struct SessionBuf {
    size_t   head      = 0;
    size_t   tail      = 0;
    bool     is_done   = false;
    uint32_t timestamp = 0;               /// millis()
    uint8_t  data[SES_BUF_SZ];
    AsyncWebServerRequest* req = nullptr; /// <--- Add this to track the network handle

    // Helper functions to manage the circular buffer state
    size_t available() const {
        if (head >= tail) return head - tail;
        return (SES_BUF_SZ - tail) + head;
    }
    size_t free_space() const {
        // Leave one slot open to distinguish between full and empty
        return SES_BUF_SZ - available() - 1;
    }
    void write(const char* src, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            if (free_space() == 0) break; // Drop bytes if buffer overflows
            data[head] = src[i];
            head = (head + 1) % SES_BUF_SZ;
        }
    }
    size_t read(uint8_t* dest, size_t max_len) {
        size_t bytes_read = 0;
        while (bytes_read < max_len && tail != head) {
            dest[bytes_read] = data[tail];
            tail = (tail + 1) % SES_BUF_SZ;
            bytes_read++;
        }
        return bytes_read;
    }
};

class XServer {
private:
    uint16_t       _port;
    const char     *_ssid;
    const char     *_password;
    xQueWeb        *_web;
    TaskHandle_t   _task;
    AsyncWebServer _server;          // Direct compilation inclusion

    std::map<uint32_t, SessionBuf> _active;
    SemaphoreHandle_t              _mutex;
    uint32_t                       _tx_id;
    
    void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, 
                   AwsEventType type, void *arg, uint8_t *data, size_t len);
    
    // internal worker functions handles the actual execution logic
    void   setup();
    bool   parse_req(uint32_t id, char *txt);
    void   process(AsyncWebServerRequest *req);
    size_t feed_web_rsp(uint32_t id, uint8_t *buf, size_t max);
    void   handle_rsp();
    void   check_timeout();
    
    void run();

public:
    XServer(const char* ssid, const char* password, uint16_t port = 80) :
        _ssid(ssid),
        _password(password),
        _port(port),
        _web(NULL),
        _task(NULL),
        _server(port),
        _tx_id(0) {
        _mutex = xSemaphoreCreateMutex();
    }
    ~XServer() {
        vSemaphoreDelete(_mutex);
    }

    // Establishes WiFi parameters and spins up the FreeRTOS background worker
    bool begin(xQueWeb *web, int priority);
};

#endif // _XSERVER_H
