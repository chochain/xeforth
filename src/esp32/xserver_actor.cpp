#include "xactor.h"

#define REQ_TIMEOUT_MS 5000

class SessionActor : public BaseActor {
private:
    int            _fd;
    httpd_handle_t _hd;
    bool           _headers_sent;
    TimerHandle_t  _timeout_timer; // FreeRTOS software timer handle

    // Static callback triggered outside actor context by FreeRTOS daemon task
    static void timer_callback(TimerHandle_t xTimer) {
        // Retrieve the targeted Actor ID stored inside the timer's local storage ID
        uint32_t target_actor_id = (uint32_t)pvTimerGetTimerID(xTimer);
        
        ActorMsg timeout_msg;
        timeout_msg.type = MSG_SESSION_TIMEOUT;
        timeout_msg.target_id = target_actor_id;
        
        // Dispatch back into the serialization queue safely
        Sys.send(timeout_msg);
    }

    void send_chunk(const char* data, size_t len) {
        if (!_headers_sent) {
            const char* headers = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nTransfer-Encoding: chunked\r\nConnection: keep-alive\r\n\r\n";
            httpd_socket_send(_hd, _fd, headers, strlen(headers), 0);
            
            const char* wrapper = "<div class='rsp-entry'>";
            char hbuf[32];
            snprintf(hbuf, sizeof(hbuf), "%X\r\n", strlen(wrapper));
            httpd_socket_send(_hd, _fd, hbuf, strlen(hbuf), 0);
            httpd_socket_send(_hd, _fd, wrapper, strlen(wrapper), 0);
            httpd_socket_send(_hd, _fd, "\r\n", 2, 0);
            
            _headers_sent = true;
        }

        if (len > 0) {
            char hbuf[32];
            snprintf(hbuf, sizeof(hbuf), "%X\r\n", len);
            httpd_socket_send(_hd, _fd, hbuf, strlen(hbuf), 0);
            httpd_socket_send(_hd, _fd, data, len, 0);
            httpd_socket_send(_hd, _fd, "\r\n", 2, 0);
        }
    }

    void handle_timeout() {
        // Stream text execution warning downstream to user console interface
        const char* timeout_msg = "\r\n[Forth execution timeout - Connection severed]\r\n";
        send_chunk(timeout_msg, strlen(timeout_msg));
        
        terminate_session();
    }

    void terminate_session() {
        // Stop and purge active software timers safely
        if (_timeout_timer != nullptr) {
            xTimerStop(_timeout_timer, 0);
            xTimerDelete(_timeout_timer, 0);
            _timeout_timer = nullptr;
        }

        // Deliver HTML closing wrappers safely
        const char* final_wrapper = "</div><br/>";
        char hbuf[32];
        snprintf(hbuf, sizeof(hbuf), "%X\r\n", strlen(final_wrapper));
        httpd_socket_send(_hd, _fd, hbuf, strlen(hbuf), 0);
        httpd_socket_send(_hd, _fd, final_wrapper, strlen(final_wrapper), 0);
        httpd_socket_send(_hd, _fd, "\r\n", 2, 0);
        
        // Push terminal empty chunk to close keep-alive transactions
        httpd_socket_send(_hd, _fd, "0\r\n\r\n", 5, 0);
        
        // Clean out instance from framework lookup maps
        Sys.unregister_actor(this->id);
        delete this;
    }

public:
    SessionActor(uint32_t actor_id, int client_fd, httpd_handle_t server_hd) 
        : BaseActor(actor_id), _fd(client_fd), _hd(server_hd), _headers_sent(false), _timeout_timer(nullptr) {
        
        // Instantiate the software timer targeting this exact instance tracking ID
        char timer_name[16];
        snprintf(timer_name, sizeof(timer_name), "tmr_ses_%d", actor_id);
        
        _timeout_timer = xTimerCreate(
            timer_name,
            pdMS_TO_TICKS(REQ_TIMEOUT_MS),
            pdFALSE,                         // One-shot timer (do not auto-reload)
            (void*)actor_id,                 // Pass our identification primitive
            timer_callback
        );
    }

    ~SessionActor() override {
        // Fallback protection layer
        if (_timeout_timer != nullptr) {
            xTimerDelete(_timeout_timer, 0);
        }
    }

    void receive(const ActorMsg &msg) override {
        switch (msg.type) {
            case MSG_WEB_SUBMIT: {
                // Ignite timeout clock ticking immediately upon forwarding down to pipeline
                if (_timeout_timer != nullptr) {
                    xTimerStart(_timeout_timer, 0);
                }

                ActorMsg forward = msg;
                forward.type = MSG_FORTH_EXEC;
                forward.target_id = 1; // Direct path routing to target ForthActor
                Sys.send(forward);
                break;
            }
            case MSG_FORTH_FEEDBACK:
                // Every time the engine yields text data back successfully, we refresh the window!
                // (Optional: reset timeout counter if you want it to trigger only on total stagnation)
                if (_timeout_timer != nullptr) {
                    xTimerReset(_timeout_timer, 0);
                }
                send_chunk(msg.buf, strlen(msg.buf));
                break;

            case MSG_FORTH_DONE:
                // Forth finished working within time allowances
                terminate_session();
                break;

            case MSG_SESSION_TIMEOUT:
                // Clock elapsed before Forth processing hung up or returned status markers
                handle_timeout();
                break;

            default:
                break;
        }
    }
};
