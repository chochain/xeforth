#ifndef _XFORTH_H
#define _XFORTH_H

#if (ARDUINO || ESP32)
#include <Arduino.h>
#include "xque.h"

extern int  forth_vm(const char *cmd, void(*hook)(int, const char*));

class XForth {
private:
    uint32_t     _core;                   /// core id
    TaskHandle_t _task;                   /// task id
    uint32_t     _tick;                   /// heartbeat_delay_ms
    xQueWeb      *_in_q;
    xQueGL       *_out_q;

    // 🚨 FreeRTOS tasks inside classes MUST be declared as "static void"
    static void vTaskForthBridge(void *pv) {
        // Cast the generic void pointer directly back into a class instance context
        XForth *vm = (XForth*)pv;
        vm->runInterpreterLoop();
    }

    // This internal worker function handles the actual execution logic
    void runInterpreterLoop();
    
    // Thread-safe internal helper to tokenize and split compound string buffers
    void parseAndExecuteTokens(char* cmd);

public:
    XForth(uint32_t id, uint32_t heartbeat_ms) : 
        _core(id), 
        _tick(pdMS_TO_TICKS(heartbeat_ms)), 
        _in_q(NULL), 
        _task(NULL) {}

    // Initializes internal configurations and spins up the FreeRTOS worker thread
    bool begin(xQueWeb *in_q, xQueGL *out_q, int priority);
};

#else // !(ARDUINO || ESP32)

#include <thread>
#include <iostream>

/* Raw C linkage wrapper stub matching your eventual low-level token execution files */
extern "C" {
    void forth_vm(const char *token, xQueGL *out_q) {
        static const draw_vec_t draw_cmd[] = {
            { VECTOR_LINE, 10, 10, 200, 10 },
            { VECTOR_LINE, 200, 10, 200, 200 },
            { VECTOR_LINE, 200, 200, 10, 10 }
        };
        static int idx = 0;
        /* If token parsing matches an action, your primitive constructs a graphics packet */
        if (strcmp(token, "LOGO-LINE") == 0) {
            std::cout << "core0 xforth> processing: " << token << std::endl;
            out_q->send_non_blocking(draw_cmd[idx++]);
        }
    }
}

class SimulatedForth {
private:
    std::thread  *_thread;
    xQueWeb      *_in_q;
    xQueGL       *_out_q;

    void runInterpreterLoop(void) {
        std::cout << "core0> Forth VM listening pipeline online." << std::endl;
        que_msg_t rx_msg;

        while (true) {
            /* Block indefinitely using 0% host CPU cycles until a web packet lands */
            _in_q->receive_blocking(rx_msg);
            std::cout << "core0 xforth> cmd received: " << rx_msg.buf << std::endl;

            /* Parse text bytes via reentrant thread-safe strtok_r logic matching your hardware architecture */
            char buf[QUE_BUF_SZ];
            strncpy(buf, rx_msg.buf, QUE_BUF_SZ);
            
            char *save_ptr;
            char *idiom = strtok_r(buf, " ", &save_ptr);
            while (idiom != NULL) {
                forth_vm(idiom, _out_q);
                idiom = strtok_r(NULL, " ", &save_ptr);
            }
        }
    }

public:
    SimulatedForth(void) : _thread(NULL), _in_q(NULL), _out_q(NULL) {}
    
    ~SimulatedForth() {
        if (_thread) { delete _thread; }
    }

    bool begin(xQueWeb *in_q, xQueGL *out_q, int priority) {
        _in_q  = in_q;
        _out_q = out_q;
        /* Spin up thread execution path using standard object context injection */
        _thread = new std::thread(&SimulatedForth::runInterpreterLoop, this);
        _thread->detach(); /* Run detached in background */

        return true;
    }
};

#endif // (ARDUINO || ESP32)
#endif // FORTH_PROCESSOR_H
