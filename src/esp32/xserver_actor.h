/// -*- mode: c++ -*-
#ifndef _XSERVER_ACTOR_H
#define _XSERVER_ACTOR_H

#include "xactor.h"

#define REQ_TIMEOUT_MS 5000   ///< max silence (no Forth output) before the session is aborted

/// One HTTP response stream. Lives on the dispatcher; assumes ActorSystem runs a
/// single worker (worker_count == 1), because route() drops its mutex before
/// calling receive(), so `delete this` is only safe when receives are serialized.
class SessionActor : public BaseActor {
private:
    int               _fd;
    httpd_handle_t    _hd;
    TimerHandle_t     _timer;
    SemaphoreHandle_t _mutex;    // esp_http_server is not thread-safe

    // Runs on the FreeRTOS timer daemon: must not block, and must not touch `this`.
    // Only the actor id travels, so a late timeout for a finished session lands on
    // a missing registry entry (ids are never reused) instead of freed memory.
    static void timer_callback(TimerHandle_t xTimer) {
        ActorMsg m;
        m.type      = MSG_SESSION_TIMEOUT;
        m.target_id = (uint32_t)(uintptr_t)pvTimerGetTimerID(xTimer);
        m.sid       = m.target_id;
        m.fd        = -1;
        Sys.send(m);    // zero-wait; if the queue is full the auto-reload timer simply fires again
    }

    void send_chunk(const char *data, size_t len, bool lock=true) {
        if (!data || len==0 || _fd < 0) return;

        bool ok = lock ? xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE : true;
        if (ok) {
            char hbuf[16];
            snprintf(hbuf, sizeof(hbuf), "%zx\r\n", len);
            httpd_socket_send(_hd, _fd, hbuf, strlen(hbuf), 0);
            httpd_socket_send(_hd, _fd, data, len, 0);
            httpd_socket_send(_hd, _fd, "\r\n", 2, 0);
            if (lock) xSemaphoreGive(_mutex);
        }
    }

    void set_session_id(uint32_t sid) {
        char oob[128];
        snprintf(oob, sizeof(oob), 
                 "<button id='abort' class='%s-btn' hx-swap-oob='outerHTML' data-sid='%u'>%u</button>", sid==0 ? "done" : "abort", sid, sid);
        send_chunk(oob, strlen(oob), false);
    }

    /// Sends the status line + headers + opening wrapper exactly once. Every path
    /// that writes to the socket goes through this, including terminate_session().
    void send_headers() {
        // 1. Flush the mandatory HTTP Chunked Transfer Encoding protocol headers
        const char* headers =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "Transfer-Encoding: chunked\r\n"
            "Connection: keep-alive\r\n\r\n";
        httpd_socket_send(_hd, _fd, headers, strlen(headers), 0);
        
        // 2. 🚀 THE OOB VALUE SWAP: Update the hidden metadata token container on the client browser!
        set_session_id(this->id);
    }

    void stop_timer() {
        if (_timer == nullptr) return;
        // Small bounded wait: a dropped stop/delete command would leave a live timer.
        xTimerStop(_timer, pdMS_TO_TICKS(50));
        xTimerDelete(_timer, pdMS_TO_TICKS(50));
        _timer = nullptr;
    }

    void handle_timeout() {
        // Stop the Forth line too. Closing the connection alone would leave the VM
        // burning CPU on a job whose output now has nowhere to go.
        ActorMsg abort{ MSG_FORTH_ABORT, FORTH_ACTOR_GLOBAL_ID, this->id };
        Sys.send(abort);

        const char *msg = "\r\n[Forth execution timeout - aborted]\r\n";
        send_chunk(msg, strlen(msg));
        terminate_session();
    }

public:
    SessionActor(uint32_t actor_id, int client_fd, httpd_handle_t server_hd)
        : BaseActor(actor_id), _fd(client_fd), _hd(server_hd), _timer(nullptr) {

        _mutex = xSemaphoreCreateBinary();
        xSemaphoreGive(_mutex);
#if 0
        char name[16];
        snprintf(name, sizeof(name), "ses_%u", (unsigned)actor_id);

        // Auto-reload: if a timeout message is lost to a full queue, it fires again.
        _timer = xTimerCreate(
            name,
            pdMS_TO_TICKS(REQ_TIMEOUT_MS),
            pdTRUE,
            (void*)(uintptr_t)actor_id,
            timer_callback);

        // Start now, not on first feedback: a VM that hangs before printing must
        // still time out. (The old MSG_WEB_SUBMIT path that started it is gone,
        // since LineSink no longer routes through the session.)
        if (!_timer || xTimerStart(_timer, pdMS_TO_TICKS(50)) != pdPASS) {
            LOG("session %u: timeout timer unavailable\n", (unsigned)actor_id);
        }
#endif
        send_headers();
    }

    ~SessionActor() override {
        stop_timer();   // fallback if destroyed without terminate_session()
        if (_mutex) vSemaphoreDelete(_mutex);
    }

    void terminate_session() {
        if (_fd < 0) return;
        
        if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            set_session_id(0);
        
            // Send the terminal empty chunk signaling end-of-transfer transaction
            httpd_socket_send(_hd, _fd, "0\r\n\r\n", 5, 0);

            _fd = -1;                  /// * prevent future write
            xSemaphoreGive(_mutex);
        }        
        // Unregister and erase this instance context from the post office maps
        Sys.unregister_actor(this->id);
        delete this;
    }

    void receive(const ActorMsg &msg) override {
        switch (msg.type) {
        case MSG_FORTH_FEEDBACK:
            DEBUG("session[%d] << '%s'\n", msg.target_id, (char*)msg.buf);
            // Output is progress: restart the stagnation window.
            if (_timer) xTimerReset(_timer, 0);
            send_chunk(msg.buf, strlen(msg.buf));
            break;

        case MSG_FORTH_DONE:
            DEBUG("session[%d] DONE\n", msg.target_id);
            terminate_session();
            break;

        case MSG_SESSION_TIMEOUT:
            DEBUG("session[%d] TIMEOUT\n", msg.target_id);
            handle_timeout();
            break;

        default:
            LOG("session %u: unknown msg.type=%d\n", (unsigned)id, (int)msg.type);
            break;
        }
    }
};

#endif // _XSERVER_ACTOR_H
