/// -*- mode: c++ -*-
#ifndef _XFORTH_ACTOR_H
#define _XFORTH_ACTOR_H

#include "xactor.h"
#include <atomic>  // Fix: Includes missing atomic utilities explicitly

#define GUI_ACTOR_GLOBAL_ID 2

extern int forth_vm(const char *cmd, void(*hook)(int, const char*));

class ForthActor : public BaseActor {
private:
    static uint32_t          _active_session_id;
    static std::atomic<bool> _abort_requested;

    static void vm_feedback_bridge(int len, const char *rst) {
        if (_abort_requested.load()) return;

        // 1. Send feedback back over the web stream interface (Core 0 Session)
        ActorMsg msg;
        msg.type = MSG_FORTH_FEEDBACK;
        msg.target_id = _active_session_id; 
        int sz = std::min(len, 127);
        memcpy(msg.buf, rst, sz);
        msg.buf[sz] = '\0';
        Sys.send(msg);

        // 2. Cross-Core Actor Flow: Send feedback straight across cores to the XGL Display Actor (ID 2)
        ActorMsg gui_msg;
        gui_msg.type = MSG_GUI_DRAW_CMD;
        gui_msg.target_id = GUI_ACTOR_GLOBAL_ID; // Routes directly to Core 1
        memcpy(gui_msg.buf, rst, sz);
        gui_msg.buf[sz] = '\0';
        Sys.send(gui_msg);
    }

public:
    ForthActor(uint32_t actor_id) : BaseActor(actor_id) {
        _abort_requested.store(false);
    }

    static bool check_abort_signal() { return _abort_requested.load(); }

    void receive(const ActorMsg &msg) override {
        switch (msg.type) {
        case MSG_FORTH_EXEC:
            _active_session_id = msg.fd;
            _abort_requested.store(false);
            forth_vm(msg.buf, vm_feedback_bridge);
            break;

        case MSG_GUI_TOUCH_TRIGGER:
            Serial.printf("Brain Received Touch Event from Core 1! Position: (%d, %d)\n", 
                          msg.touch.x, msg.touch.y);
            break;

        case MSG_FORTH_ABORT: {
            _abort_requested.store(true);
            ActorMsg feedback{ MSG_FORTH_FEEDBACK, msg.fd };
            snprintf(feedback.buf, sizeof(feedback.buf), "\r\n[SYSTEM] Broken via Display Interface.\r\n");
            Sys.send(feedback);

            ActorMsg done{ MSG_FORTH_DONE, msg.fd };
            Sys.send(done);
        } break;
            
        case MSG_FORTH_DONE: {
            ActorMsg end_session{ MSG_FORTH_DONE, msg.fd };
            Sys.send(end_session);
        } break;
            
        default:
            Serial.printf("msg.type=%d not supported\n", msg.type);
            break;
        }
    }
};

// C-linkage bridge function to wire directly into your core C Forth engine's interpreter loops
extern "C" bool forth_engine_check_abort() {
    return ForthActor::check_abort_signal();
}

#endif // _XFORTH_ACTOR_H
