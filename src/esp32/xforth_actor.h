/// -*- mode: c++ -*-
#ifndef _XFORTH_ACTOR_H
#define _XFORTH_ACTOR_H
#pragma once

#include "xactor.h"
#include <atomic>

extern int forth_vm(const char *cmd, void(*hook)(int, const char*));

class ForthActor : public BaseActor {
private:
    static uint32_t          _active_sid;           ///< active session
    static std::atomic<bool> _abort;                ///< abort flag
    static uint32_t          _feedback_cnt;

    static void feedback(int len, const char *rst) {
        DEBUG("  xforth[%d] >> <%d>%s", _active_sid, len, rst);
        if (_abort.load()) return;

        ++_feedback_cnt;

        // 1. Route stream chunks straight to the static SessionCoordinator
        ActorMsg fb { MSG_FORTH_FEEDBACK, COORDINATOR_ACTOR_GLOBAL_ID, _active_sid };
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
        LOG("[SYSTEM] Forth Actor Registered (ID: %d)\n", FORTH_ACTOR_GLOBAL_ID);
    }

    void receive(const ActorMsg &msg) override {
        switch (msg.type) {
        case MSG_FORTH_EXEC:
            DEBUG("  xforth[%d] << '%s'\n", msg.sid, (char*)msg.buf);
            _active_sid = msg.sid;
            _abort.store(false);
            _feedback_cnt = 0;

            forth_vm(msg.buf, feedback);
            
            // Core Generation complete. Send EOF and pause execution of the next command.
            {
                ActorMsg eof { MSG_FORTH_EXEC_EOF, COORDINATOR_ACTOR_GLOBAL_ID, msg.sid };
                eof.feedback_sent = _feedback_cnt;
                Sys.send(eof);
            }
            break;

        case MSG_FORTH_EOF_ACK:
            DEBUG("  xforth[%d] << EOF_ACT\n", msg.sid);
            // The Coordinator confirmed network buffers are clear. Safe to close transaction out-of-order free!
            {
                ActorMsg done { MSG_FORTH_DONE, COORDINATOR_ACTOR_GLOBAL_ID, msg.sid };
                Sys.send(done);
            }
            break;

        case MSG_FORTH_ABORT:
            DEBUG("  xforth[%d] << ABORT\n", msg.sid);
            if (msg.sid == _active_sid) {
                _abort.store(true);
            }
            break;

        case MSG_GUI_TOUCH_TRIGGER:
            LOG("xgl touch: (%d, %d)\n", msg.touch.x, msg.touch.y);
            break;
            
        default:
            LOG("msg.type=%d not supported\n", msg.type);
            break;
        }
    }
};
#endif // _XFORTH_ACTOR_H
