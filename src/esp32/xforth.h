#ifndef _XFORTH_H
#define _XFORTH_H

#if (ARDUINO || ESP32)
#include <Arduino.h>
#include "xque.h"

extern int  forth_vm(const char *cmd, void(*hook)(int, const char*));

class XForth {
private:
    static xQueUI  *_ui;                  ///< _ui  message bridge
    static xQueWeb *_web;                 ///< _web message bridge
    uint32_t     _core;                   ///< core id
    TaskHandle_t _task;                   ///< task id
    uint32_t     _tick;                   ///< heartbeat_delay_ms

    static void feedback(int i, const char *rst);

    // Thread-safe internal helper to tokenize and split compound string buffers
    void outer(uint32_t id, char *cmd);
    
    // This internal worker function handles the actual execution logic
    void handle_web_req();
    void handle_ui_rsp();
    void run();

public:
    XForth(uint32_t id, uint32_t heartbeat_ms) : 
        _core(id), 
        _tick(pdMS_TO_TICKS(heartbeat_ms)), 
        _task(NULL) {}

    // Initializes internal configurations and spins up the FreeRTOS worker thread
    bool begin(xQueWeb *web, xQueUI *ui, int priority);
};

#else // !(ARDUINO || ESP32)

#include <thread>
#include <iostream>

/* Raw C linkage wrapper stub matching your eventual low-level token execution files */
extern "C" {
    void forth_vm(const char *token, xQueWeb *web, xQueUI *ui) {
        static const msg_gui_t ui_cmd[] = {
            { VECTOR_LINE, 10, 10, 200, 10, "line 0" },
            { VECTOR_LINE, 200, 10, 200, 200, "line 1" },
            { VECTOR_LINE, 200, 200, 10, 10, "line 2" }
        };
        static int idx = 0;
        /* If token parsing matches an action, your primitive constructs a graphics packet */
        if (strcmp(token, "LOGO-LINE") == 0) {
            std::cout << "core0 xforth> processing: " << token << std::endl;
            ui->put_req(ui_cmd[idx++]);
        }
    }
}

class SimulatedForth {
private:
    std::thread  *_thread;
    xQueWeb      *_web;
    xQueUI       *_ui;

    void outer(uint32_t id, char *cmd) {
        char *save_ptr;
        char *idiom = strtok_r(cmd, " ", &save_ptr);
        while (idiom != NULL) {
            forth_vm(idiom, _web, _ui);
            idiom = strtok_r(NULL, " ", &save_ptr);
        }
    }

    void handle_web_req() {
        msg_web_t msg;
        int       id = 0;
        while (_web->get_req(msg)) {
            std::cout << "core0 xforth> cmd received: " << msg.buf << std::endl;

            /* Parse text bytes via reentrant thread-safe strtok_r logic matching your hardware architecture */
            char buf[QUE_BUF_SZ];
            strncpy(buf, (char*)msg.buf, QUE_BUF_SZ);
            
            outer(++id, buf);
        }
    }

    void run(void) {
        std::cout << "core0> Forth VM active..." << std::endl;

        while (1) {
            handle_web_req();
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        }
    }

public:
    SimulatedForth(void) : _thread(NULL), _web(NULL), _ui(NULL) {}
    ~SimulatedForth() {
        if (_thread) { delete _thread; }
    }

    bool begin(xQueWeb *web, xQueUI *ui, int priority) {
        _web = web;
        _ui  = ui;
        /* Spin up thread execution path using standard object context injection */
        _thread = new std::thread(&SimulatedForth::run, this);
        _thread->detach(); /* Run detached in background */

        return true;
    }
};

#endif // (ARDUINO || ESP32)
#endif // FORTH_PROCESSOR_H
