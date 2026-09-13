///
/// @file
/// @brief ESP32 Web Server (esp_http_server backend - Downgraded for Core 2.0.16 / ESP-IDF v4.4)
///
#ifndef _XSERVER_H
#define _XSERVER_H

#include <map>
#include <string>
#include <string_view>
#include <Arduino.h>
#include <WiFi.h>
#include <esp_http_server.h>
#include "xque.h"

#define SES_BUF_SZ     512
#define FORM_BUF_SZ    2048   
#define REQ_TIMEOUT_MS 5000   
#define WAIT_POLL_MS   50     

#define ASYNC_WORKER_COUNT 3
#define ASYNC_QUEUE_LEN    3

struct SessionBuf {
    size_t   head      = 0;
    size_t   tail      = 0;
    bool     is_done   = false;
    uint32_t timestamp = 0;             
    uint8_t  data[SES_BUF_SZ];

    SemaphoreHandle_t notify = nullptr;

    size_t available() const {
        if (head >= tail) return head - tail;
        return (SES_BUF_SZ - tail) + head;
    }
    size_t free_space() const {
        return SES_BUF_SZ - available() - 1;
    }
    void write(const char* src, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            if (free_space() == 0) break; 
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

// Modified for ESP-IDF v4.x asynchronous queue handling
struct AsyncReqTask {
    httpd_handle_t hd;  /// server handle
    int            fd;  /// client socket fd
    uint32_t       tid; /// session id
};

class XServer {
private:
    uint16_t       _port;
    const char     *_ssid;
    const char     *_password;
    xQueWeb        *_web;
    TaskHandle_t   _task;
    httpd_handle_t _httpd = nullptr;                   

    std::map<uint32_t, SessionBuf> _active;
    SemaphoreHandle_t              _mutex;
    uint32_t                       _tx_id;

    QueueHandle_t     _async_queue        = nullptr;
    SemaphoreHandle_t _worker_ready_count = nullptr;  
    TaskHandle_t      _workers[ASYNC_WORKER_COUNT]    = { nullptr };

    static void worker_task(void *pv);
    
    void      setup();
    bool      parse_req(uint32_t id, char *txt);
    void      handle_rsp();

    bool      read_form(httpd_req_t *req, char *out, size_t out_sz);  
    uint32_t  open_session();                                         
    void      close_session(uint32_t tid);                            
    void      stream_session(httpd_handle_t hd, int fd, uint32_t tid); // Modified signature
    esp_err_t submit_async(httpd_req_t *req);

    void      run();

public:
    XServer(const char* ssid, const char* password, uint16_t port = 80) :
        _ssid(ssid),
        _password(password),
        _port(port),
        _web(NULL),
        _task(NULL),
        _tx_id(0) {
        _mutex = xSemaphoreCreateMutex();
    }
    ~XServer() {
        vSemaphoreDelete(_mutex);
    }

    bool begin(xQueWeb *web, int priority);
};

#endif // _XSERVER_H

