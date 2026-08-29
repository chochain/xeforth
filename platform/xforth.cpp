#include "xforth.h"

bool XForth::begin(xQueWeb *in_q, xQueGL *out_q, int priority) {
    if (in_q == NULL) return false;
    _in_q  = in_q;
    _out_q = out_q;

    // 2. Launch the background FreeRTOS execution thread on Core 0
    // We pass "this" (the memory address of this class instance) into the 4th parameter slot!
    BaseType_t xReturned = xTaskCreatePinnedToCore(
        vTaskForthBridge,      // Static function bridge pointer
        "Forth_Core_Task",     // Task string identifier name
        8192,                  // Task stack depth allocation (bytes)
        (void*)this,           // 👈 PASS 'THIS' CONTEXT POINTER HERE
        priority,              // Priority assignment configuration
        &_task,                // Target task handle tracker
        0                      // Pin strictly to Core 0 (leaving Core 1 free for LVGL)
    );
    return (xReturned == pdPASS);
}

void XForth::runInterpreterLoop() {
    Serial.printf("[ForthProcessor Class ID %u]: Background thread online on Core 0.\n", _processor_id);

    web_msg_t rx_msg;
    while (1) {
        // Wait indefinitely (portMAX_DELAY) using 0% CPU cycles until a packet hits the queue
        if (xQueueReceive(_in_q, &rx_msg, portMAX_DELAY) == pdTRUE) {
            
            Serial.printf("\n[Forth Class Interface]: Evaluating incoming string array -> %s\n", rx_msg.cmd);
            
            // Execute non-fragmenting multi-token text processing
            parseAndExecuteTokens(rx_msg.cmd);
        }
        // Brief safety heartbeat yield hook
        vTaskDelay(_ticks);
    }
}

void XForth::parseAndExecuteTokens(char* cmd) {
    if (cmd == NULL || strlen(cmd) == 0) return;

    // Extract the very first token word from the continuous text buffer
    // using the reentrant, thread-safe strtok_r function
    char* save_ptr;
    char* idiom = strtok_r(cmd, " ", &save_ptr);
    
    while (idiom != NULL) {
        // Pass individual parsed tokens directly to your low-level C engine
        // by referencing their raw memory string pointers
        forth_vm(idiom, _out_q);
        
        // Seek out the next individual space-separated command segment
        idiom = strtok_r(NULL, " ", &save_ptr);
    }
}
