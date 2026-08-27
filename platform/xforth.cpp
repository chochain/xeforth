#include "xforth.h"

bool XForth::begin(QueueHandle_t shared_queue, UBaseType_t task_priority) {
    if (shared_queue == NULL) return false;
    _incoming_queue = shared_queue;

    // 2. Launch the background FreeRTOS execution thread on Core 0
    // We pass "this" (the memory address of this class instance) into the 4th parameter slot!
    BaseType_t xReturned = xTaskCreatePinnedToCore(
        vTaskForthBridge,      // Static function bridge pointer
        "Forth_Core_Task",     // Task string identifier name
        8192,                  // Task stack depth allocation (bytes)
        (void*)this,           // 👈 PASS 'THIS' CONTEXT POINTER HERE
        task_priority,         // Priority assignment configuration
        &_task_handle,         // Target task handle tracker
        0                      // Pin strictly to Core 0 (leaving Core 1 free for LVGL)
    );

    return (xReturned == pdPASS);
}

void XForth::runInterpreterLoop() {
    Serial.printf("[ForthProcessor Class ID %u]: Background thread online on Core 0.\n", _processor_id);

    // Fixed-size message structure matching your web server payload configuration
    // This lives on the task stack, avoiding global system heap fragmentation!
    #define MAX_FORTH_CMD_LEN 128
    typedef struct {
        char command_text[MAX_FORTH_CMD_LEN];
    } web_msg_t;

    web_msg_t rx_msg;

    while (1) {
        // Wait indefinitely (portMAX_DELAY) using 0% CPU cycles until a packet hits the queue
        if (xQueueReceive(_incoming_queue, &rx_msg, portMAX_DELAY) == pdTRUE) {
            
            Serial.printf("\n[Forth Class Interface]: Evaluating incoming string array -> %s\n", rx_msg.command_text);
            
            // Execute non-fragmenting multi-token text processing
            parseAndExecuteTokens(rx_msg.command_text);
        }
        
        // Brief safety heartbeat yield hook
        vTaskDelay(pdMS_TO_TICKS(_heartbeat_delay_ms));
    }
}

void XForth::parseAndExecuteTokens(char* input_line) {
    if (input_line == NULL || strlen(input_line) == 0) return;

    char* save_ptr;
    // Extract the very first token word from the continuous text buffer
    // using the reentrant, thread-safe strtok_r function
    char* idiom = strtok_r(input_line, " ", &save_ptr);
    
    while (idiom != NULL) {
        // Pass individual parsed tokens directly to your low-level C engine
        // by referencing their raw memory string pointers
        forth_vm(idiom, NULL); 
        
        // Seek out the next individual space-separated command segment
        idiom = strtok_r(NULL, " ", &save_ptr);
    }
}
