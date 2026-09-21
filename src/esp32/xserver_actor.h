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
    int            _fd;
    httpd_handle_t _hd;
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

    void send_chunk(const char *data, size_t len) {
        if (len == 0) return;
        
        char hbuf[16];
        snprintf(hbuf, sizeof(hbuf), "%zX\r\n", len);
        httpd_socket_send(_hd, _fd, hbuf, strlen(hbuf), 0);
        httpd_socket_send(_hd, _fd, data, len, 0);
        httpd_socket_send(_hd, _fd, "\r\n", 2, 0);
    }

    void set_abort_button(bool on) {
        char oob[256];
        snprintf(oob, sizeof(oob), 
            "<div id='abort-control-slot' hx-swap-oob='true'>"
            "<button class='%s-btn'"
            "hx-post='/abort?id=%u' hx-target='#log' hx-swap='beforeend'>"
            "%s (session %u)</button></div>",
                 on ? "abort" : "done", this->id, on ? "STOP" : "DONE", this->id);
        
        // Send the button component instantly down the raw client pipe socket wire
        send_chunk(oob, strlen(oob));
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
        
        // 2. Transmit the HTMX Out-Of-Bounds Abort Button locked onto this specific session ID
        set_abort_button(true);

        // 3. Open the monospaced wrapper frame for the dynamic incoming text logs
        const char* open_wrapper = "<div class='rsp-entry'>";
        send_chunk(open_wrapper, strlen(open_wrapper));
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
    }

    void terminate_session() {
        // Close out the HTML visualization tag layers cleanly
        const char* final_wrapper = "</div><br/>";
        send_chunk(final_wrapper, strlen(final_wrapper));
        
        set_abort_button(false);
        
        // Send the terminal empty chunk signaling end-of-transfer transaction
        httpd_socket_send(_hd, _fd, "0\r\n\r\n", 5, 0);
        
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
