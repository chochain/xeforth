#ifndef FORTH_PROCESSOR_H
#define FORTH_PROCESSOR_H
#include <Arduino.h>

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

