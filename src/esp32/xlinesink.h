/// -*- mode: c++ -*-
#ifndef _XLINESINK_H
#define _XLINESINK_H

#include "xactor.h"
#include "xserver.h"

class LineSink {
private:
    uint32_t    _session_id;
    job_class_t _cls;

    char decode_char(const char *src) {
        char a = src[0];
        char b = src[1];
        a = (a >= 'A' && a <= 'F') ? a - 'A' + 10 : (a >= 'a' && a <= 'f') ? a - 'a' + 10 : a - '0';
        b = (b >= 'A' && b <= 'F') ? b - 'A' + 10 : (b >= 'a' && b <= 'f') ? b - 'a' + 10 : b - '0';
        return (char)((a << 4) | b);
    }

public:
    LineSink(uint32_t session_id, job_class_t cls) : _session_id(session_id), _cls(cls) {}

    void split_and_stream(const char* raw_src, size_t src_len) {
        char   line_buf[QUE_BUF_SZ];
        size_t w_idx = 0;
        size_t r_idx = 0;

        while (r_idx < src_len) {
            char c = raw_src[r_idx];
            if (c == '%') {
                if (r_idx + 2 < src_len) {
                    c = decode_char(&raw_src[r_idx + 1]);
                    r_idx += 2;
                }
            } else if (c == '+') {
                c = ' ';
            }
            r_idx++;

            if (c == '\r') continue;
            if (c == '\n') {
                if (w_idx > 0) {
                    line_buf[w_idx] = '\0';
                    ActorMsg msg{ MSG_FORTH_EXEC, 1, (int)_session_id, nullptr, "" };
                    strncpy(msg.buf, line_buf, QUE_BUF_SZ);
                    Sys.send(msg);
                    w_idx = 0;
                }
                continue;
            }
            if (w_idx < (QUE_BUF_SZ - 1)) line_buf[w_idx++] = c;
        }

        if (w_idx > 0) {
            line_buf[w_idx] = '\0';
            ActorMsg msg{ MSG_FORTH_EXEC, 1, (int)_session_id, nullptr, "" };
            strncpy(msg.buf, line_buf, QUE_BUF_SZ);
            Sys.send(msg);
        }

        ActorMsg eos{ MSG_FORTH_DONE, 1, (int)_session_id, nullptr, "" };
        Sys.send(eos);
    }
};

#endif // _XLINESINK_H
