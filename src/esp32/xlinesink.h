/// -*- mode: c++ -*-
#ifndef _XLINESINK_H
#define _XLINESINK_H

#include "xactor.h"
#include "xforth_actor.h"

#define FORTH_POST_WAIT_MS 2000

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
class LineSink {
private:
    uint32_t    _sid;
    job_class_t _cls;   // NOT honoured yet: see note in the reply (front-posting line by
                        // line would reverse a multi-line submission)

    static int hex(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    }

    bool post(ActorMsgType type, const char *line = "", size_t n = 0) {
        ActorMsg m { type, FORTH_ACTOR_GLOBAL_ID, _sid };  // zeroed: buf is NUL-terminated for any n < QUE_BUF_SZ
        if (n > 0) memcpy(m.buf, line, n);
        Sys.send(m);
    }

    /// Whole submission is rejected, atomically from the user's point of view:
    /// lines that already got queued are dropped, running one is aborted, and the
    /// session is told why and closed.
    sink_result_t reject(sink_result_t why, const char *text) {
        ActorMsg x { MSG_FORTH_ABORT, FORTH_ACTOR_GLOBAL_ID, _sid };
        Sys.send_priority(x);

        ActorMsg fb { MSG_FORTH_FEEDBACK, _sid, _sid };
        snprintf(fb.buf, sizeof(fb.buf), "\r\n[submission rejected: %s]\r\n", text);
        Sys.send(fb, FORTH_POST_WAIT_MS);

        // Preferred: DONE through the Forth mailbox, so Forth also clears its skip
        // entry and emits "[aborted]". If that is full too, close the session
        // directly; its timeout timer is the last line of defence.
        if (!post(MSG_FORTH_DONE)) {
            x.type = MSG_FORTH_DONE;
            x.buf[0] = '\0';
            Sys.send(x, FORTH_POST_WAIT_MS);
        }
        return why;
    }

public:
    LineSink(uint32_t session_id, job_class_t cls) : _sid(session_id), _cls(cls) {}

    sink_result_t split_and_stream(const char *raw, size_t len) {
        char   line[QUE_BUF_SZ];
        size_t w = 0;
        size_t r = 0;

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
            } else if (c == '+') {
                c = ' ';
            }

            if (c == '\r' || c == '\0') continue;   // NUL would truncate the line in the VM

            if (c == '\n') {
                if (w > 0) {
                    if (!post(MSG_FORTH_EXEC, line, w)) return reject(SINK_MBOX_FULL, "Forth busy");
                    w = 0;
                }
                continue;
            }

            if (w >= QUE_BUF_SZ - 1) return reject(SINK_LINE_TOO_LONG, "line too long");
            line[w++] = c;
        }

        if (w > 0 && !post(MSG_FORTH_EXEC, line, w)) return reject(SINK_MBOX_FULL, "Forth busy");
        if (!post(MSG_FORTH_DONE))                   return reject(SINK_MBOX_FULL, "Forth busy");
        return SINK_OK;
    }
};

#endif // _XLINESINK_H
