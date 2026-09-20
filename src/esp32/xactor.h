/// -*- mode: c++ -*-
/// @file
/// @brief Central Actor System definitions & thread-pool multiplexer
///
#ifndef _XACTOR_H
#define _XACTOR_H

#include <Arduino.h>
#include <map>
#include <algorithm>
#include "esp_http_server.h"

#define QUE_DEPTH       20
#define QUE_BUF_SZ      128

#define ERR(msg)        Serial.println(msg)
//#define DEBUG(fmt, ...)
#define DEBUG(fmt, ...) Serial.printf(fmt, __VA_ARGS__)
#define LOG(fmt, ...)   Serial.printf(fmt, __VA_ARGS__)

#define FORTH_ACTOR_GLOBAL_ID 1
#define GUI_ACTOR_GLOBAL_ID   2

typedef enum {
    JOB_BATCH    = 0,         /// submit-and-collect: queued, no live interaction expected
    JOB_DEMAND   = 1,         /// interactive/time-sharing: low-latency, session held open
    JOB_REALTIME = 2          /// preemptive: serviced ahead of BATCH/DEMAND, not FIFO order
} job_class_t;

enum ActorMsgType {
    MSG_WEB_SUBMIT,           /// Incoming raw multi-line payload block
    MSG_FORTH_EXEC,           /// Process a single newline-delimited command string
    MSG_FORTH_FEEDBACK,       /// Text streamed back dynamically by the Forth VM execution layer
    MSG_FORTH_DONE,           /// Explicit end-of-submission marker for an active session block
    MSG_FORTH_ABORT,          /// Emergency priority break request to kill a long run
    MSG_GUI_DRAW_CMD,         /// Forward string outputs straight to the LVGL terminal
    MSG_GUI_TOUCH_TRIGGER,    /// Touch coordinate packets dispatched from Core 1 to Core 0
    MSG_SYS_TELEMETRY,        /// Periodic hardware memory metric tracking frame
    MSG_SESSION_TIMEOUT       /// network inactivity guard
};

struct ActorMsg {
    ActorMsgType type;
    uint32_t     target_id;   /// Unique ID of the destination Actor
    uint32_t     sid;         /// Session id (== SessionActor id). Distinct from fd on purpose.
    int          fd;          /// Client socket handle or original source tracking reference
    httpd_handle_t hd;        /// Web server handle context
    
    union {
        char buf[QUE_BUF_SZ]; /// Standard 128 bytes text string space
        struct {
            int16_t x;
            int16_t y;
            uint8_t state; 
        } touch;
        struct {
            uint32_t free_heap_kb;
            uint32_t free_psram_kb;
        } memory;             /// 8-byte payload structure for live metrics
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
    QueueHandle_t                  _queue;          /// map M workers => N actors
    std::map<uint32_t, BaseActor*> _registry;       /// id => actor
    SemaphoreHandle_t              _mutex;
    TaskHandle_t                  *_workers;
    int                            _worker_count;
    uint32_t                       _next_id;

    static void dispatch(void *pv) {
        ActorSystem *sys = static_cast<ActorSystem*>(pv);
        ActorMsg msg;
        while (1) {
            /// blocked at 0% CPU until new msg arrived
            if (xQueueReceive(sys->_queue, &msg, portMAX_DELAY) == pdTRUE) {
                BaseActor *actor = sys->get_actor(msg.target_id);
                if (actor) actor->receive(msg);
                /// no delay here, so no context switching
            }
        }
    }

public:
    ActorSystem() : _queue(nullptr), _workers(nullptr), _worker_count(0), _next_id(100) {
        _mutex = xSemaphoreCreateMutex();
    }

    void begin(int n, int priority) {
        _worker_count = n;
        _queue   = xQueueCreate(QUE_DEPTH, sizeof(ActorMsg));
        _workers = new TaskHandle_t[n];
        
        for (int i = 0; i < n; ++i) {
            char name[16];
            snprintf(name, sizeof(name), "xactor_wrk%d", i);
            xTaskCreatePinnedToCore(
                dispatch, name, 8192,
                this, priority, &_workers[i], 0);   /// Pinned to Core 0
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

    /// Bounded-wait send. ONLY call from threads that are not this queue's
    /// consumer (Forth task, httpd thread). Never from a dispatcher worker.
    bool send(const ActorMsg &msg, TickType_t ticks=0, bool priority=false) {
        if (!_queue) return false;
        return priority
            ? xQueueSendToFront(_queue, &msg, ticks) == pdPASS
            : xQueueSend(_queue, &msg, ticks) == pdPASS;
    }

    BaseActor *get_actor(uint32_t id) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        auto it = _registry.find(id);
        BaseActor* actor = (it != _registry.end()) ? it->second : nullptr;
        xSemaphoreGive(_mutex);
        
        return actor;
    }
};

extern ActorSystem Sys;

#endif // _XACTOR_H
