/// -*- mode: c++ -*-
#ifndef _XLINESINK2_H
#define _XLINESINK2_H

#include "xactor.h"

#define FORTH_POST_SLOW 20
#define FORTH_POST_WAIT 200

typedef enum {
    SINK_OK = 0,
    SINK_LINE_TOO_LONG,   ///< a single line would not fit one message (would silently split a word)
    SINK_MBOX_FULL        ///< Forth mailbox stayed full for the whole bounded wait
} sink_result_t;

/// Runs on the httpd thread. It consumes nothing from any queue, so it is the one
/// place that can safely block while waiting for room in the Forth mailbox.
///
/// Order guarantee: lines are posted FIFO, and the default httpd config runs one
/// server task, so two submissions never interleave in the mailbox.
typedef void (*EventCallback)(void* ctx, const char* line);

class LineSink {
private:
    uint32_t      _sid;
    void*         _ctx;
    EventCallback _on_overflow;
    
    static int hex(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    }

    bool post(ActorMsgType type, const char *line = "", size_t n = 0) {
        ActorMsg m { type, FORTH_ACTOR_GLOBAL_ID, _sid };    ///< zeroed: buf is NUL-terminated for any n < QUE_BUF_SZ
        if (type == MSG_FORTH_DONE) m.line_count = n;
        else if (n > 0) memcpy(m.buf, line, n);
        if (!Sys.send(m, FORTH_POST_SLOW)) {                 /// * slow feed to Forth VM
            if (_on_overflow) _on_overflow(_ctx, line);
            return false;
        }
        return true;
    }

    /// Whole submission is rejected, atomically from the user's point of view:
    /// lines that already got queued are dropped, running one is aborted, and the
    /// session is told why and closed.
    sink_result_t reject(sink_result_t why, const char *text) {
        DEBUG("linesink::reject[%d] '%s'", _sid, text);
        
        ActorMsg x { MSG_FORTH_ABORT, FORTH_ACTOR_GLOBAL_ID, _sid };
        Sys.send(x, 0, true);

        ActorMsg fb { MSG_FORTH_FEEDBACK, _sid, _sid };
        snprintf(fb.buf, sizeof(fb.buf), "\r\n[submission rejected: %s]\r\n", text);
        Sys.send(fb, FORTH_POST_WAIT);

        // Preferred: DONE through the Forth mailbox, so Forth also clears its skip
        // entry and emits "[aborted]". If that is full too, close the session
        // directly; its timeout timer is the last line of defence.
        post(MSG_FORTH_DONE);
        return why;
     }

public:
    LineSink(uint32_t sid, EventCallback cb = NULL, void *ctx =NULL)
        : _sid(sid), _on_overflow(cb), _ctx(ctx) {}

    sink_result_t split_and_stream(const char *raw, size_t len) {
        char   line[QUE_BUF_SZ];
        size_t w    = 0;           ///< buffer index
        size_t r    = 0;           ///< raw index
        bool   full = false;
        int    lc   = 0;           ///< line count

        while (r < len) {
            char c = raw[r++];

            if (c == '%') {
                // Only decode a real %XX; anything else stays a literal '%'.
                if (r + 1 < len) {
                    int hi = hex(raw[r]), lo = hex(raw[r + 1]);
                    if (hi >= 0 && lo >= 0) {
                        c = (char)((hi << 4) | lo);
                        r += 2;
                    }
                }
            }
            else if (c == '+') c = ' ';

            if (c == '\r' || c == '\0') continue;   // NUL would truncate the line in the VM
            if (c == '\n') {
                if (w > 0) {
                    if (!post(MSG_FORTH_EXEC, line, w)) { full = true; break; }
                    lc++;
                    w = 0;
                }
                continue;
            }
            if (w >= QUE_BUF_SZ - 1) return reject(SINK_LINE_TOO_LONG, "line too long");
            
            line[w++] = c;
        }
        if (!full && w > 0) { line[w] = '\0'; lc++; }

        // Always notify the framework to release the SessionActor context
        if (w > 0 && !post(MSG_FORTH_EXEC, line, w)) return SINK_MBOX_FULL;
        if (!post(MSG_FORTH_DONE, "", lc))           return SINK_MBOX_FULL;

        return SINK_OK;
    }
};

#endif // _XLINESINK2_H
