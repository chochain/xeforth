#ifndef FORTH_PROCESSOR_H
#define FORTH_PROCESSOR_H

#if (ARDUINO || ESP32)
#include <Arduino.h>
#include "xbridge.h"
#include "xque.h"

extern int  forth_vm(const char *cmd, void(*hook)(int, const char*));

class XForth {
private:
    uint32_t      _processor_id;
    uint32_t      _heartbeat_delay_ms;
    QueueHandle_t _incoming_queue;
    TaskHandle_t  _task_handle;

    // 🚨 FreeRTOS tasks inside classes MUST be declared as "static void"
    static void vTaskForthBridge(void *pvParameters) {
        // Cast the generic void pointer directly back into a class instance context
        ForthProcessor* instance = (ForthProcessor*)pvParameters;
        instance->runInterpreterLoop();
    }

    // This internal worker function handles the actual execution logic
    void runInterpreterLoop();
    
    // Thread-safe internal helper to tokenize and split compound string buffers
    void parseAndExecuteTokens(char* input_line);

public:
    XForth(uint32_t id, uint32_t heartbeat_ms) : 
        _processor_id(id), 
        _heartbeat_delay_ms(heartbeat_ms), 
        _incoming_queue(NULL), 
        _task_handle(NULL) {}

    // Initializes internal configurations and spins up the FreeRTOS worker thread
    bool begin(QueueHandle_t shared_queue, UBaseType_t task_priority);
};

#else // !(ARDUINO || ESP32)

#include <thread>
#include <iostream>

/* Raw C linkage wrapper stub matching your eventual low-level token execution files */
extern "C" {
    inline void mock_forth_interpret_token(const char *token, XQueue<vector_draw_packet_t> *out_pipe) {
        static const vector_draw_packet_t draw_cmd[] = {
            { VECTOR_LINE, 10, 10, 200, 10 },
            { VECTOR_LINE, 200, 10, 200, 200 },
            { VECTOR_LINE, 200, 200, 10, 10 }
        };
        static int idx = 0;
        /* If token parsing matches an action, your primitive constructs a graphics packet */
        if (strcmp(token, "LOGO-LINE") == 0) {
            std::cout << "core0 xforth> processing: " << token << std::endl;
            out_pipe->send_non_blocking(draw_cmd[idx++]);
        }
    }
}

class SimulatedForth {
private:
    std::thread *_worker_thread;
    XQueue<web_cmd_packet_t> *_in_queue;
    XQueue<vector_draw_packet_t> *_out_queue;

    void runInterpreterLoop(void) {
        std::cout << "core0> Forth VM listening pipeline online." << std::endl;
        web_cmd_packet_t rx_msg;

        while (true) {
            /* Block indefinitely using 0% host CPU cycles until a web packet lands */
            _in_queue->receive_blocking(rx_msg);
            std::cout << "core0 xforth> cmd received: " << rx_msg.raw_forth_text << std::endl;

            /* Parse text bytes via reentrant thread-safe strtok_r logic matching your hardware architecture */
            char buffer[MAX_WEB_LINE_LEN];
            strncpy(buffer, rx_msg.raw_forth_text, MAX_WEB_LINE_LEN);
            
            char *save_ptr;
            char *token = strtok_r(buffer, " ", &save_ptr);
            while (token != NULL) {
                mock_forth_interpret_token(token, _out_queue);
                token = strtok_r(NULL, " ", &save_ptr);
            }
        }
    }

public:
    SimulatedForth(void) : _worker_thread(NULL), _in_queue(NULL), _out_queue(NULL) {}
    
    ~SimulatedForth() {
        if (_worker_thread) { delete _worker_thread; }
    }

    void begin(XQueue<web_cmd_packet_t> *in_q, XQueue<vector_draw_packet_t> *out_q) {
        _in_queue = in_q;
        _out_queue = out_q;
        /* Spin up thread execution path using standard object context injection */
        _worker_thread = new std::thread(&SimulatedForth::runInterpreterLoop, this);
        _worker_thread->detach(); /* Run detached in background */
    }
};

#endif // (ARDUINO || ESP32)
#endif // FORTH_PROCESSOR_H
