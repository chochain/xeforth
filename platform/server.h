///
/// @file
/// @brief ESP32 Async Web Server
/// 
///====================================================================
#ifndef _XSERVER_H
#define _XSERVER_H

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>

class XServer {
private:
    uint16_t       _port;
    const char*    _ssid;
    const char*    _password;
    xQueWeb        _out_g;
    TaskHandle_t   _task;
    AsyncWebServer _server;          // Direct compilation inclusion

    // 🚨 FreeRTOS tasks inside classes MUST be declared as "static void"
    static void vTaskServerBridge(void *pv) {
        // Cast the generic void pointer directly back into a class instance context
        XServer* svr = (XServer*)pv;
        svr->runServerLoop();
    }

    // This internal worker function handles the actual execution logic
    void runServerLoop();

public:
    XServer(const char* ssid, const char* password, uint16_t port = 80) :
        _ssid(ssid),
        _password(password),
        _port(port),
        _out_q(NULL),
        _task(NULL),
        _server(port) {}

    // Establishes WiFi parameters and spins up the FreeRTOS background worker
    bool begin(xQueWeb web_q, UBaseType_t task_priority);
};

#endif // !_XSERVER_H
