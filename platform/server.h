///
/// @file
/// @brief ESP32 Async Web Server
/// 
///====================================================================
#ifndef EMBEDDED_WEB_SERVER_H
#define EMBEDDED_WEB_SERVER_H

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>

class EmbeddedWebServer {
private:
    uint16_t       _port;
    const char*    _ssid;
    const char*    _password;
    QueueHandle_t  _outgoing_queue;
    TaskHandle_t   _task_handle;
    AsyncWebServer _server;          // Direct compilation inclusion

    // 🚨 FreeRTOS tasks inside classes MUST be declared as "static void"
    static void vTaskServerBridge(void *pvParameters) {
        // Cast the generic void pointer directly back into a class instance context
        EmbeddedWebServer* instance = (EmbeddedWebServer*)pvParameters;
        instance->runServerLoop();
    }

    // This internal worker function handles the actual execution logic
    void runServerLoop();

public:
    EmbeddedWebServer(const char* ssid, const char* password, uint16_t port = 80) :
        _ssid(ssid),
        _password(password),
        _port(port),
        _outgoing_queue(NULL),
        _task_handle(NULL),
        _server(port) {}

    // Establishes WiFi parameters and spins up the FreeRTOS background worker
    bool begin(QueueHandle_t shared_queue, UBaseType_t task_priority);
};

#endif
