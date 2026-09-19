/// -*- mode: c++ -*-
#ifndef _XSERVER_ACTOR_H
#define _XSERVER_ACTOR_H

#include "xactor.h"
#include "xforth_actor.h"

#define REQ_TIMEOUT_MS 5000   ///< max silence (no Forth output) before the session is aborted

/// One HTTP response stream. Lives on the dispatcher; assumes ActorSystem runs a
/// single worker (worker_count == 1), because route() drops its mutex before
/// calling receive(), so `delete this` is only safe when receives are serialized.
class SessionActor : public BaseActor {
private:
    int            _fd;
    httpd_handle_t _hd;
    bool           _hdr_sent;
    TimerHandle_t  _timer;

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

    void send_raw_chunk(const char *data, size_t len) {
        char hbuf[16];
        snprintf(hbuf, sizeof(hbuf), "%zX\r\n", len);
        httpd_socket_send(_hd, _fd, hbuf, strlen(hbuf), 0);
        httpd_socket_send(_hd, _fd, data, len, 0);
        httpd_socket_send(_hd, _fd, "\r\n", 2, 0);
    }

    /// Sends the status line + headers + opening wrapper exactly once. Every path
    /// that writes to the socket goes through this, including terminate_session().
    void ensure_headers() {
        if (_hdr_sent) return;
        const char *headers =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "Transfer-Encoding: chunked\r\n"
            "Connection: keep-alive\r\n\r\n";
        httpd_socket_send(_hd, _fd, headers, strlen(headers), 0);
        send_raw_chunk("<div class='rsp-entry'>", strlen("<div class='rsp-entry'>"));
        _hdr_sent = true;
    }

    void send_chunk(const char *data, size_t len) {
        ensure_headers();
        if (len > 0) send_raw_chunk(data, len);
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

    void terminate_session() {
        stop_timer();

        ensure_headers();   // Forth may have finished without printing anything
        const char *tail = "</div><br/>";
        send_raw_chunk(tail, strlen(tail));
        httpd_socket_send(_hd, _fd, "0\r\n\r\n", 5, 0);

        Sys.unregister_actor(this->id);
        delete this;        // caller must return immediately
    }

public:
    SessionActor(uint32_t actor_id, int client_fd, httpd_handle_t server_hd)
        : BaseActor(actor_id),
          _fd(client_fd), _hd(server_hd), _hdr_sent(false), _timer(nullptr) {

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
        if (_timer == nullptr ||
            xTimerStart(_timer, pdMS_TO_TICKS(50)) != pdPASS) {
            LOG("session %u: timeout timer unavailable\n", (unsigned)actor_id);
        }
#endif        
    }

    ~SessionActor() override {
        stop_timer();   // fallback if destroyed without terminate_session()
    }

    void receive(const ActorMsg &msg) override {
        switch (msg.type) {
        case MSG_FORTH_FEEDBACK:
            // Output is progress: restart the stagnation window.
            if (_timer != nullptr) xTimerReset(_timer, 0);
            send_chunk(msg.buf, strlen(msg.buf));
            break;

        case MSG_FORTH_DONE:
            terminate_session();
            break;

        case MSG_SESSION_TIMEOUT:
            handle_timeout();
            break;

        default:
            LOG("session %u: unknown msg.type=%d\n", (unsigned)id, (int)msg.type);
            break;
        }
    }
};

#endif // _XSERVER_ACTOR_H
