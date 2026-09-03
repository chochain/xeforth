#include "xforth.h"

xQueGL *XForth::_out_q = NULL;
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
        (BaseType_t)priority,  // Priority assignment configuration
        &_task,                // Target task handle tracker
        0                      // Pin strictly to Core 0 (leaving Core 1 free for LVGL)
    );
    return (xReturned == pdPASS);
}

void XForth::runInterpreterLoop() {
    Serial.printf("core%d xforth> Background thread online.\n", _core);

    msg_web_t rx_msg;
    while (1) {
        // Wait indefinitely (portMAX_DELAY) using 0% CPU cycles until a packet hits the queue
        if (xQueueReceive((QueueHandle_t)_in_q, &rx_msg, portMAX_DELAY) == pdTRUE) {
            Serial.printf("core%d xforth> incoming cmd -> %s\n", _core, rx_msg.buf);
            
            // Execute non-fragmenting multi-token text processing
            parseAndExecuteTokens(rx_msg.buf);
        }
        // Brief safety heartbeat yield hook
        vTaskDelay(_tick);
    }
}

void XForth::feedback(int len, const char *rst) {
    static msg_gl_t msg;
    Serial.printf("%d> %s\n", len, rst);
        
    int sz = std::min(len, (QUE_BUF_SZ - 1));
    memcpy(msg.buf, rst, sz);                 /// leave last byte to
    msg.buf[sz] = '\0';                       /// ensure \0 terminated
    msg.op_code = VECTOR_LINE;
        
    if (xQueueSend((QueueHandle_t)_out_q, &msg, 0) != pdTRUE) {
        Serial.printf("xforth out_q failed on %s\n", rst);
    }
}

void XForth::parseAndExecuteTokens(char* cmd) {
    if (cmd == NULL || strlen(cmd) == 0) return;

    forth_vm(cmd, feedback);             /// one-line per call
    return;

    // Extract the very first token word from the continuous text buffer
    // using the reentrant, thread-safe strtok_r function
    char* save_ptr;
    char* idiom = strtok_r(cmd, " ", &save_ptr);
    
    while (idiom != NULL) {
        // Pass individual parsed tokens directly to your low-level C engine
        // by referencing their raw memory string pointers
        Serial.printf("forth << %s\n", idiom);
        
        // Seek out the next individual space-separated command segment
        idiom = strtok_r(NULL, " ", &save_ptr);
    }
}
