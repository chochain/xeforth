/// -*- mode: c++ -*-
#ifndef _XSERVER_COORDINATOR_H
#define _XSERVER_COORDINATOR_H

#include "xactor.h"

#define REQ_TIMEOUT_MS 5000
#define MAX_SESSIONS   8

struct SessionState {
    uint32_t       sid       = 0;
    int            fd        = -1;
    httpd_handle_t hd        = nullptr;
    bool           is_active = false;
    
    char           *psram_code_block = nullptr;
    size_t         read_index = 0;
    size_t         total_len  = 0;

    uint32_t       feedback_count  = 0;
    uint32_t       feedback_total  = 0;
    bool           waiting_for_eof = false;
};

class SessionCoordinator : public BaseActor {
private:
    SessionState      _sessions[MAX_SESSIONS];
    SemaphoreHandle_t _mutex;           // esp_http_server socket send isolation mutex

    int find_session_slot(uint32_t sid) {
        for (int i = 0; i < MAX_SESSIONS; i++) {
            if (_sessions[i].is_active && _sessions[i].sid == sid) return i;
        }
        return -1;
    }

    int get_empty_slot() {
        for (int i = 0; i < MAX_SESSIONS; i++) {
            if (!_sessions[i].is_active) {
                return i; // Found an available structural slot!
            }
        }
        return -1; // Index full
    }
    
    void send_chunk(httpd_handle_t hd, int fd, const char *data, size_t len) {
        if (!data || len == 0 || fd < 0 || !hd) return;

        if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            char hbuf[16];
            snprintf(hbuf, sizeof(hbuf), "%zx\r\n", len);
            httpd_socket_send(hd, fd, hbuf, strlen(hbuf), 0);
            httpd_socket_send(hd, fd, data, len, 0);
            httpd_socket_send(hd, fd, "\r\n", 2, 0);
            xSemaphoreGive(_mutex);
        }
    }

    void dispatch_next_line(int slot) {
        SessionState &s = _sessions[slot];
    
        // Wrap the entire parsing block inside a flat execution loop
        while (true) {
            if (s.read_index >= s.total_len) {
                // All text segments consumed. Safely trigger completion event.
                ActorMsg done { MSG_FORTH_DONE, COORDINATOR_ACTOR_GLOBAL_ID, s.sid };
                Sys.send(done);
                return; // 🟢 Safe exit, 0 recursion risk
            }

            char line_buf[QUE_BUF_SZ];
            size_t w = 0;

            // Extract characters until reaching a clean newline delimiter
            while (s.read_index < s.total_len) {
                char c = s.psram_code_block[s.read_index++];
            
                if (c == '+') c = ' ';
                if (c == '\r' || c == '\0') continue;
            
                if (c == '\n') {
                    if (w > 0) break; // Complete command captured!
                    continue;
                }

                if (w < QUE_BUF_SZ - 1) {
                    line_buf[w++] = c;
                }
            }
            line_buf[w] = '\0';

            if (w > 0) {
                // Valid command found! Dispatch to Forth VM and break the loop
                ActorMsg m { MSG_FORTH_EXEC, FORTH_ACTOR_GLOBAL_ID, s.sid };
                memcpy(m.buf, line_buf, w + 1);
                Sys.send(m);
                return; // 🟢 Exit function. Wait for EOF handshake token.
            }
        
            // If w == 0, the loop naturally continues to evaluate the next line 
            // without allocating any extra stack frames!
        }
    }
    
    void check_and_handshake(int slot) {
        SessionState &s = _sessions[slot];
        
        // Only proceed if we have received the EOF message AND all feedback packets have arrived
        if (s.waiting_for_eof && (s.feedback_count == s.feedback_total)) {
            // Reset counters for the next line sequence step
            s.feedback_count = 0;
            s.feedback_total = 0;
            s.waiting_for_eof = false;

            // 1. Handshake ACK back to Forth Actor to release its lock state
            ActorMsg ack{ MSG_FORTH_EOF_ACK, FORTH_ACTOR_GLOBAL_ID, s.sid };
            Sys.send(ack);

            // 2. Move to the next string segment inside PSRAM
            dispatch_next_line(slot);
        }
    }
    
    void terminate_session_slot(int slot) {
        if (slot < 0 || !_sessions[slot].is_active) return;
        
        SessionState &s = _sessions[slot];
        if (s.fd >= 0 && s.hd && xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            httpd_socket_send(s.hd, s.fd, "0\r\n\r\n", 5, 0);
            xSemaphoreGive(_mutex);
        }

        LOG("[SC] Session %u cleanly closed.\n", s.sid);
        s.is_active = false;
        s.sid       = 0;
        s.fd        = -1;
        s.hd        = nullptr;
    }

public:
    SessionCoordinator(uint32_t actor_id) : BaseActor(actor_id) {
        _mutex = xSemaphoreCreateBinary();
        xSemaphoreGive(_mutex);
    }

    ~SessionCoordinator() override {
        if (_mutex) vSemaphoreDelete(_mutex);
    }

    void register_new_connection(uint32_t sid, int fd, httpd_handle_t hd, char* psram_buf, size_t len) {
        int slot = get_empty_slot();
        if (slot == -1) {
            LOG("[SC] Max session capacity %d hit! Rejecting request.\n", MAX_SESSIONS);
            heap_caps_free(psram_buf); // Release allocation payload block
            return;
        }
        
        SessionState &s = _sessions[slot];
        s.sid              = sid;
        s.fd               = fd;
        s.hd               = hd;
        s.psram_code_block = psram_buf; 
        s.total_len        = len;
        s.read_index       = 0;
        s.feedback_count   = 0;
        s.feedback_total   = 0;
        s.waiting_for_eof  = false;
        s.is_active        = true;

        // Flush HTTP Chunked Transfer Encoding initial response headers line-by-line
        const char* headers =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "Transfer-Encoding: chunked\r\n"
            "Connection: keep-alive\r\n\r\n";
        httpd_socket_send(hd, fd, headers, strlen(headers), 0);

        // Begin your execution sequence loop [1]
        dispatch_next_line(slot);
    }

    void receive(const ActorMsg &msg) override {
        auto clean = [this](SessionState &s, const char *err) {
            if (s.psram_code_block != nullptr) {
                heap_caps_free(s.psram_code_block);
                s.psram_code_block = nullptr;
            }
            if (err) send_chunk(s.hd, s.fd, err, strlen(err));
        };
        int slot = find_session_slot(msg.sid);
        if (slot < 0) return;

        SessionState &s = _sessions[slot];

        switch (msg.type) {
        case MSG_FORTH_FEEDBACK:
            DEBUG("[SC] %u >> '%s'\n", msg.sid, (char*)msg.buf);
            s.feedback_count++; // 👈 Track arrival
            send_chunk(s.hd, s.fd, msg.buf, strlen(msg.buf));
            
            // Check if this feedback was the last piece we were waiting for
            check_and_handshake(slot);
            break;

        case MSG_FORTH_EXEC_EOF:
            DEBUG("[SC] %u EXEC_EOF => ACK\n", msg.sid);
            // Forth VM completed its processing loop step and is waiting!
            // 1. Force the LwIP network buffers to clear out onto the physical wire
            // (Optional: flush hardware sockets if necessary)

            // Forth VM has finished generating, record the target number it sent
            s.feedback_total  = msg.feedback_sent;
            s.waiting_for_eof = true;

            // Verify if all feedbacks are already here, or if we need to wait for late packets
            check_and_handshake(slot);
            break;
            
        case MSG_FORTH_DONE:
            DEBUG("[SC] %u DONE\n", msg.sid);
            clean(s, "");
            terminate_session_slot(slot);
            break;

        case MSG_FORTH_ABORT:
            DEBUG("[SC] %u ABORT\n", msg.sid);
            clean(s, "\r\n[SYSTEM INTERRUPT] Execution Aborted via Panel.\r\n");
            terminate_session_slot(slot);
            break;

        default:
            DEBUG("[SC] %u Unknown msg.type=%d\n", msg.sid, msg.type);
            break;
        }
    }
};

#endif // _XSERVER_COORDINATOR_H
