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

#define QUE_DEPTH       32
#define QUE_BUF_SZ      128
#define QUE_WAIT_TICKS  5

#define LOG(fmt, ...)   Serial.printf(fmt, __VA_ARGS__)
#define DEBUG(fmt, ...) Serial.printf(fmt, __VA_ARGS__)

// Static Global Target Registry IDs
#define SESMUX_ACTOR_ID 1  // Central Traffic Cop Actor ID
#define FORTH_ACTOR_ID  2
#define GUI_ACTOR_ID    3

enum ActorMsgType {
    MSG_FORTH_EXEC,           /// Forward individual raw command line to VM
    MSG_FORTH_FEEDBACK,       /// Text streamed back dynamically by the Forth VM
    MSG_FORTH_EXEC_EOF,       /// Generation checkpoint completed by Forth
    MSG_FORTH_EOF_ACK,        /// Network Layer verification callback handshake
    MSG_FORTH_DONE,           /// Explicit macro execution processing complete
    MSG_FORTH_ABORT,          /// Emergency priority drop request
    MSG_SESSION_TIMEOUT,      /// Stagnation safety event
    MSG_GUI_DRAW_CMD,         /// Forward string outputs straight to the LVGL terminal
    MSG_GUI_TOUCH_TRIGGER,    /// Touch coordinate packets dispatched from Core 1 to Core 0
    MSG_SYS_TELEMETRY         /// Periodic hardware memory metric tracking frame
};

struct ActorMsg {
    ActorMsgType   type;
    uint32_t       target_id;   /// Destination Actor target register
    uint32_t       sid;         /// Session ID tracking tag
    int            fd;          /// Socket file descriptor tracking hook
    httpd_handle_t hd;          /// Webserver process descriptor link
    
    union {
        char buf[QUE_BUF_SZ];   /// Inline 128-byte data transmission window
        uint32_t line_count;
        uint32_t feedback_sent;
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
                LOG("actor%d.%d >> '%s'\n",
                    msg.target_id, msg.sid,
                    msg.type==MSG_FORTH_DONE ? "DONE" : (char*)msg.buf);
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
    bool send(const ActorMsg &msg, TickType_t ticks=QUE_WAIT_TICKS, bool priority=false) {
        LOG("actor%d.%d << '%s'",
            msg.target_id, msg.sid,
            msg.type==MSG_FORTH_DONE ? "DONE" : (char*)msg.buf);
        if (!_queue) return false;
        bool rst = priority
             ? xQueueSendToFront(_queue, &msg, ticks) == pdPASS
             : xQueueSend(_queue, &msg, ticks) == pdPASS;
        LOG("%s\n", rst ? "" : " => queue full");
        
        return rst;
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
