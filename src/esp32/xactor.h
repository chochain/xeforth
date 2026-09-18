/// -*- mode: c++ -*-
/// @file
/// @brief Central Actor System definitions & thread-pool multiplexer
///
#ifndef _XACTOR_H
#define _XACTOR_H

#include <Arduino.h>
#include <map>
#include <algorithm>
#include "xbridge.h"
#include "esp_http_server.h"

enum ActorMsgType {
    MSG_WEB_SUBMIT,        // Incoming raw multi-line payload block
    MSG_FORTH_EXEC,        // Process a single newline-delimited command string
    MSG_FORTH_FEEDBACK,    // Text streamed back dynamically by the Forth VM execution layer
    MSG_FORTH_DONE,        // Explicit end-of-submission marker for an active session block
    MSG_FORTH_ABORT,       // Emergency priority break request to kill a long run
    MSG_GUI_DRAW_CMD,      // Forward string outputs straight to the LVGL terminal
    MSG_GUI_TOUCH_TRIGGER, // Touch coordinate packets dispatched from Core 1 to Core 0
    MSG_SYS_TELEMETRY,     // Periodic hardware memory metric tracking frame
    MSG_SESSION_TIMEOUT    // network inactivity guard
};

struct ActorMsg {
    ActorMsgType type;
    uint32_t     target_id;  // Unique ID of the destination Actor
    int          fd;         // Client socket handle or original source tracking reference
    httpd_handle_t hd;       // Web server handle context
    union {
        char buf[QUE_BUF_SZ]; // Standard 128 bytes text string space
        struct {
            int16_t x;
            int16_t y;
            uint8_t state; 
        } touch;
        struct {
            uint32_t free_heap_kb;
            uint32_t free_psram_kb;
        } memory;             // 8-byte payload structure for live metrics
    };
};

class BaseActor {
public:
    uint32_t id;
    BaseActor(uint32_t actor_id) : id(actor_id) {}
    virtual ~BaseActor() {}
    virtual void receive(const ActorMsg &msg) = 0;
};

class ActorSystem {
private:
    QueueHandle_t                  _actor_queue;
    std::map<uint32_t, BaseActor*> _registry;
    SemaphoreHandle_t              _mutex;
    TaskHandle_t                  *_workers;
    int                            _worker_count;
    uint32_t                       _next_id;

    static void dispatcher_worker(void *pv) {
        ActorSystem *sys = static_cast<ActorSystem*>(pv);
        ActorMsg msg;
        while (1) {
            // Purely Event-Driven: Workers remain completely suspended in 0% CPU state until a message lands
            if (xQueueReceive(sys->_actor_queue, &msg, portMAX_DELAY) == pdTRUE) {
                sys->route(msg);
            }
        }
    }

    void route(const ActorMsg &msg) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        auto it = _registry.find(msg.target_id);
        BaseActor* actor = (it != _registry.end()) ? it->second : nullptr;
        xSemaphoreGive(_mutex);
        
        if (actor) {
            actor->receive(msg);
        }
    }

public:
    ActorSystem() : _actor_queue(nullptr), _workers(nullptr), _worker_count(0), _next_id(100) {
        _mutex = xSemaphoreCreateMutex();
    }

    void begin(int worker_count, int priority) {
        _worker_count = worker_count;
        _actor_queue = xQueueCreate(20, sizeof(ActorMsg));
        _workers = new TaskHandle_t[worker_count];
        
        for (int i = 0; i < worker_count; ++i) {
            char name[16];
            snprintf(name, sizeof(name), "xactor_wrk%d", i);
            xTaskCreatePinnedToCore(dispatcher_worker, name, 6144, this, priority, &_workers[i], 0); // Pinned to Core 0
        }
    }

    uint32_t alloc_id() {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        uint32_t tid = ++_next_id;
        xSemaphoreGive(_mutex);
        return tid;
    }
    
    void register_actor(BaseActor *actor) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        _registry[actor->id] = actor;
        xSemaphoreGive(_mutex);
    }

    void unregister_actor(uint32_t id) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        _registry.erase(id);
        xSemaphoreGive(_mutex);
    }

    bool send(const ActorMsg &msg) {
        return xQueueSend(_actor_queue, &msg, 0) == pdPASS;
    }

    bool send_priority(const ActorMsg &msg) {
        // Leverages xQueueSendToFront to slice straight past buffered FIFO elements
        return xQueueSendToFront(_actor_queue, &msg, 0) == pdPASS;
    }
};

extern ActorSystem Sys;

#endif // _XACTOR_H
