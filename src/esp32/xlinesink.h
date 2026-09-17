#ifndef _XLINESINK_H
#define _XLINESINK_H
#include <Arduino.h>
#include "xque.h"

/// Decodes an application/x-www-form-urlencoded value one character at a
/// time, splits it into QUE_BUF_SZ-capped lines on '\n', and enqueues each
/// line to a xQueWeb — REALTIME jobs jump the queue (put_req_priority),
/// everything else waits up to a shared per-submission time budget.
///
/// One LineSink is built per submission (see XServer::_decode_and_enqueue).
/// It never materializes a full decoded transcript — only one QUE_BUF_SZ
/// line lives at a time, which is the whole point of decoding this way
/// instead of decoding into a full buffer and re-scanning it afterward.
class LineSink {
public:
    LineSink(xQueWeb *web, uint32_t tid, job_class_t cls, uint32_t budget_ms)
        : _web(web), _tid(tid), _cls(cls), _started(millis()), _budget_ms(budget_ms) {}

    /// Decodes + enqueues `raw` (still URL-encoded, `raw_len` bytes).
    /// Returns false if a line failed to enqueue (queue full / budget
    /// exhausted) — lines already sent still count in lc()/lc_total().
    bool run(const char *raw, size_t raw_len) {
        for (size_t i = 0; i < raw_len; ++i) {
            char d = _decode_one(raw, raw_len, i);
            if (d == '\n') { _flush(); continue; }
            if (d == '\r') continue;   // swallow bare CR (the other half of %0D%0A)
            _append(d);
        }
        _flush();   // trailing line with no terminating '\n'
        return _ok;
    }

    size_t lc()       const { return _lc; }
    size_t lc_total() const { return _lc_total; }

private:
    static int _hex_nibble(char h) {
        if (h >= '0' && h <= '9') return h - '0';
        if (h >= 'a' && h <= 'f') return h - 'a' + 10;
        if (h >= 'A' && h <= 'F') return h - 'A' + 10;
        return -1;
    }

    // Decodes one logical character of a application/x-www-form-urlencoded
    // value starting at raw[i], advancing i past any %XX it consumes.
    static char _decode_one(const char *raw, size_t raw_len, size_t &i) {
        char c = raw[i];
        if (c == '+') return ' ';
        if (c == '%' && i + 2 < raw_len) {
            int hi = _hex_nibble(raw[i + 1]), lo = _hex_nibble(raw[i + 2]);
            if (hi >= 0 && lo >= 0) {
                i += 2;
                return (char)((hi << 4) | lo);
            }
        }
        return c;
    }

    void _append(char d) {
        if (_overflowed) return;
        if (_llen < QUE_BUF_SZ - 1) _line[_llen++] = d;
        else                        _overflowed    = true;  // drop rest of this line
    }

    void _flush() {
        if (_llen > 0) {              // mirrors strtok_r: empty segments don't count
            _lc_total++;
            if (_ok) {
                uint32_t   elapsed = millis() - _started;
                TickType_t wait    = pdMS_TO_TICKS(elapsed < _budget_ms ? _budget_ms - elapsed : 0);
                if (!_enqueue(wait)) _ok = false;
            }
        }
        _llen       = 0;
        _overflowed = false;
    }

    // Builds one msg_web_t and sends it down the lane its job class calls
    // for. REALTIME jumps the queue (see XQueue::send_priority); everything
    // else waits up to wait_ticks for room.
    bool _enqueue(TickType_t wait_ticks) {
        msg_web_t cmd{};
        cmd.id  = _tid;
        cmd.eos = false;
        memcpy(cmd.buf, _line, _llen);

        bool sent = (_cls == JOB_REALTIME) ? _web->put_req_priority(cmd)
                                            : _web->put_req_wait(cmd, wait_ticks);
        if (sent) {
            DEBUG(" >> <%d>'%s'\n", (int)_llen, (char*)cmd.buf);
            _lc++;
            return true;
        }
        LOG("_web->put_req failed/budget exhausted: '%s'\n", (char*)cmd.buf);
        return false;
    }

    xQueWeb     *_web;
    uint32_t     _tid;
    job_class_t  _cls;
    uint32_t     _started;
    uint32_t     _budget_ms;

    bool   _ok       = true;
    size_t _lc       = 0;
    size_t _lc_total = 0;

    char   _line[QUE_BUF_SZ];
    size_t _llen       = 0;
    bool   _overflowed = false;
};

#endif // _XLINESINK_H
