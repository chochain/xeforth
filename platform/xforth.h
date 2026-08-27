#ifndef FORTH_PROCESSOR_H
#define FORTH_PROCESSOR_H
#include <Arduino.h>
#include "xbridge.h"
#include "xque.h"

extern int  forth_vm(const char *cmd, void(*hook)(int, const char*));

class ForthProcessor {
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
    ForthProcessor(uint32_t id, uint32_t heartbeat_ms) : 
        _processor_id(id), 
        _heartbeat_delay_ms(heartbeat_ms), 
        _incoming_queue(NULL), 
        _task_handle(NULL) {}

    // Initializes internal configurations and spins up the FreeRTOS worker thread
    bool begin(QueueHandle_t shared_queue, UBaseType_t task_priority);
};

#include <thread>
#include <iostream>

/* Raw C linkage wrapper stub matching your eventual low-level token execution files */
extern "C" {
    inline void mock_forth_interpret_token(const char *token, SimulationQueue<vector_draw_packet_t> *out_pipe) {
        /* If token parsing matches an action, your primitive constructs a graphics packet */
        if (strcmp(token, "LOGO-LINE") == 0) {
            std::cout << "[Simulated Core 0 C-Forth]: Firing raw LOGO-LINE primitive." << std::endl;
            vector_draw_packet_t draw_cmd = { VECTOR_LINE, 10, 10, 250, 250 };
            out_pipe->send_non_blocking(draw_cmd);
        }
    }
}

class SimulatedForth {
private:
    std::thread *_worker_thread;
    SimulationQueue<web_cmd_packet_t> *_in_queue;
    SimulationQueue<vector_draw_packet_t> *_out_queue;

    void runInterpreterLoop(void) {
        std::cout << "[Thread: Simulated Core 0]: Forth VM listening pipeline online." << std::endl;
        web_cmd_packet_t rx_msg;

        while (true) {
            /* Block indefinitely using 0% host CPU cycles until a web packet lands */
            _in_queue->receive_blocking(rx_msg);
            std::cout << "[Thread: Simulated Core 0]: Processing Text -> " << rx_msg.raw_forth_text << std::endl;

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

    void begin(SimulationQueue<web_cmd_packet_t> *in_q, SimulationQueue<vector_draw_packet_t> *out_q) {
        _in_queue = in_q;
        _out_queue = out_q;
        /* Spin up thread execution path using standard object context injection */
        _worker_thread = new std::thread(&SimulatedForth::runInterpreterLoop, this);
        _worker_thread->detach(); /* Run detached in background */
    }
};
#endif // FORTH_PROCESSOR_H
