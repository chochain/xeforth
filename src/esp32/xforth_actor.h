/// -*- mode: c++ -*-
#ifndef _XFORTH_ACTOR_H
#define _XFORTH_ACTOR_H
#pragma once
#include "xactor.h"
#include <atomic>  // Fix: Includes missing atomic utilities explicitly

#define FORTH_ACTOR_GLOBAL_ID 1
#define GUI_ACTOR_GLOBAL_ID   2

extern int forth_vm(const char *cmd, void(*hook)(int, const char*));

class ForthActor : public BaseActor {
private:
    static uint32_t          _active_sid;           ///< active session
    static std::atomic<bool> _abort;                ///< abort flag

    static void vm_feedback_bridge(int len, const char *rst) {
        if (_abort.load()) return;

        // 1. Send feedback back over the web stream interface (Core 0 Session)
        ActorMsg fb { MSG_FORTH_FEEDBACK, _active_sid, _active_sid };
        int sz = std::min(len, QUE_BUF_SZ - 1);
        memcpy(fb.buf, rst, sz);
        fb.buf[sz] = '\0';
        Sys.send(fb);

        // 2. Cross-Core Actor Flow: Send feedback straight across cores to the XGL Display Actor (ID 2)
        ActorMsg g { MSG_GUI_DRAW_CMD, GUI_ACTOR_GLOBAL_ID, _active_sid };
        memcpy(g.buf, rst, sz);
        g.buf[sz] = '\0';
        Sys.send(g);
    }

public:
    ForthActor(uint32_t actor_id) : BaseActor(actor_id) {
        _abort.store(false);
    }

    static bool check_abort_signal() { return _abort.load(); }

    void receive(const ActorMsg &msg) override {
        switch (msg.type) {
        case MSG_FORTH_EXEC:
            _active_sid = msg.sid;
            _abort.store(false);
            forth_vm(msg.buf, vm_feedback_bridge);
            break;

        case MSG_GUI_TOUCH_TRIGGER:
            LOG("Brain Received Touch Event from Core 1! Position: (%d, %d)\n", 
                msg.touch.x, msg.touch.y);
            break;

        case MSG_FORTH_ABORT: if (msg.sid == _active_sid) {
            _abort.store(true);
            
            ActorMsg fb { MSG_FORTH_FEEDBACK, msg.sid, msg.sid };
            snprintf(fb.buf, sizeof(fb.buf), "\r\n[SYSTEM] Broken via Display Interface.\r\n");
            Sys.send(fb);
            }
            // no break, continue to DONE
            
        case MSG_FORTH_DONE: {
            ActorMsg x { MSG_FORTH_DONE, msg.sid, msg.sid };
            Sys.send(x);
        } break;
            
        default:
            LOG("msg.type=%d not supported\n", msg.type);
            break;
        }
    }
};

// C-linkage bridge function to wire directly into your core C Forth engine's interpreter loops
#if 0
extern "C" bool forth_abort() {
    return ForthActor::check_abort_signal();
}
#endif 
#endif // _XFORTH_ACTOR_H
