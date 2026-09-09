#include "xforth.h"

xQueWeb *XForth::_web = nullptr;
xQueUI  *XForth::_ui  = nullptr;
bool XForth::begin(xQueWeb *web, xQueUI *ui, int priority) {
    if (web == NULL) return false;
    _web = web;
    _ui  = ui;

    // 2. Launch the background FreeRTOS execution thread on Core 0
    // We pass "this" (the memory address of this class instance) into the 4th parameter slot!
    BaseType_t xReturned = xTaskCreatePinnedToCore(
        [](void *pv) { static_cast<XForth*>(pv)->run(); },
        "Forth_Core_Task",     // Task string identifier name
        8192,                  // Task stack depth allocation (bytes)
        (void*)this,           // 👈 PASS 'THIS' CONTEXT POINTER HERE
        (BaseType_t)priority,  // Priority assignment configuration
        &_task,                // Target task handle tracker
        0                      // Pin strictly to Core 0 (leaving Core 1 free for LVGL)
    );
    return (xReturned == pdPASS);
}

void XForth::handle_web_req() {
    msg_web_t req;
    while (_web->get_req(req)) {
        Serial.printf("core%d xforth> incoming cmd -> %s\n", _core, (char*)req.buf);
            
        // Execute non-fragmenting multi-token text processing
        outer(req.id, (char*)req.buf);
        
        // CC:Brief safety heartbeat yield hook
    }
}

void XForth::handle_ui_rsp() {
    msg_gui_t rsp;
    while (_ui->get_rsp(rsp)) {
        // do nothing for now
    }
}

void XForth::run() {
    Serial.printf("core%d xforth> Background thread online.\n", _core);

    while (1) {
        handle_web_req();
        handle_ui_rsp();
        vTaskDelay(_tick);
    }
}

void XForth::feedback(int len, const char *rst) {
    static msg_gui_t gui_req;
    static msg_web_t web_rsp;
    
    Serial.printf("%d> %s", len, rst);
        
    int sz = std::min(len, (QUE_BUF_SZ - 1));
    memcpy(gui_req.buf, rst, sz);             /// leave last byte to
    gui_req.buf[sz] = '\0';                   /// ensure \0 terminated
    gui_req.op_code = VECTOR_CMD;
    
    if (!_ui->put_req(gui_req)) {
        Serial.printf("xforth gui_req failed on %s\n", rst);
    }
    memcpy(web_rsp.buf, rst, sz);             /// leave last byte to
    web_rsp.buf[sz] = '\0';                   /// ensure \0 terminated
        
    if (!_web->put_rsp(web_rsp)) {
        Serial.printf("xforth web_rsp failed on %s\n", rst);
    }
}

void XForth::outer(uint32_t id, char *cmd) {
    if (cmd == NULL || strlen(cmd) == 0) return;

    forth_vm(cmd, feedback);             /// one-line per call
    
    msg_web_t rsp = { id, 0x0, true };
    _web->put_rsp(rsp);
    
    return;
    
#if 0  /* outer interpreter, without istringstream */
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
#endif    
}
