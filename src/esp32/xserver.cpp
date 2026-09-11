///
/// @file
/// @brief Async Web Server class implementation
///
#include <algorithm>           // std::min
#include "xserver.h"

const char *HTML_CHUNKED PROGMEM = R"XX(
HTTP/1.1 200 OK
Content-type:text/plain
Transfer-Encoding: chunked

)XX";

// Embed the HTMX-driven HTML interface cleanly inside the flash space
const char *HTML_INDEX PROGMEM = R"XX(<!DOCTYPE html>
<html>
<head>
  <!-- 1. Enforce encoding immediately at the absolute start of the head block -->
  <meta charset='UTF-8'>
  <title>xeForth Mainframe Panel</title>
  
  <!-- 2. Point to the official, explicit package distribution destination -->
  <script src="https://unpkg.com/htmx.org@2.0.4"></script>
  <!-- script src="https://unpkg.com"></script -->
  
  <style>
    body { font-family:'Courier New', monospace; font-size:14px; background:#121212; color:#00ff00; padding:10px; margin:0; }
    #container { display: flex; height: 95vh; }
    #log { flex: 0 0 60%; background-color:#1a1a1a; border: 1px solid #333; overflow-y:auto; padding:10px; box-sizing:border-box; }
    #tib-form { flex: 0 0 40%; display: flex; flex-direction: column; }
    #tib { flex: 1; background:#000; color:#00ff00; border:1px solid #333; resize:none; padding:10px; font-family:inherit; font-size:inherit; }
    .cmd-entry { color: #00bcff; margin-top: 5px; }
    .rsp-entry { color: #00ff00; white-space: pre-wrap; }
  </style>
</head>
<body>
    <div id='container'>
        <div id='log'>xeForth Mainframe Initialized...<br/></div>

        <form id='tib-form' 
           hx-post='/execute' 
           hx-target='#log' 
           hx-swap='beforeend'
           onsubmit="document.getElementById('log').innerHTML += '<div class=\'cmd-entry\'>&gt; ' + document.getElementById('tib').value.replace(/\n/g,'<br/>') + '</div>';">

            <textarea id='tib' name='forth_code' 
              placeholder='Type Forth code here...'
              hx-on::after-request="this.value=''"
              onkeydown="if(event.keyCode===13 && !event.shiftKey){
                event.preventDefault(); htmx.trigger('#tib-form', 'submit');
              }"></textarea>
        </form>
    </div>
</body>
</html>
)XX";

bool XServer::begin(xQueWeb *web, int priority) {
    if (web == NULL) return false;
    _web = web;
    
    // Launch the background FreeRTOS execution thread on Core 0
    // We pass "this" (the memory address of this class instance) into the 4th parameter slot!
    BaseType_t xReturned = xTaskCreatePinnedToCore(
        [](void *pv) { static_cast<XServer*>(pv)->run(); },
        "Web_Async_Task",      // Task string identifier name
        4096,                  // Task stack depth allocation (bytes)
        (void*)this,           // 👈 PASS 'THIS' CONTEXT POINTER HERE
        (BaseType_t)priority,  // Priority assignment configuration
        &_task,                // Target task handle tracker
        0                      // Pin strictly to Core 0 (leaving Core 1 free for LVGL)
    );
    return (xReturned == pdPASS);
}

void XServer::setup() {
    WiFi.mode(WIFI_STA);
    Serial.printf("ssid=%s, pw=%s\n", _ssid, _password);
    WiFi.begin(_ssid, _password);
    
    while (WiFi.status() != WL_CONNECTED) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        Serial.print(".");
    }
    
    Serial.printf("\ncore0 xsvr> live at http://%s\n", 
                  WiFi.localIP().toString().c_str());

    // Route A: Serve the UI Dashboard Home Page
    _server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send_P(200, "text/html", HTML_INDEX);
    });
    // Route B: Handle Incoming Async Data Submissions
    _server.on("/execute", HTTP_POST, [this](AsyncWebServerRequest *req) {
        this->process(req);
    });
}

void XServer::process(AsyncWebServerRequest *req) {
    const AsyncWebParameter* p = req->getParam("forth_code", true);
    if (!p) {
        req->send(400, "text/plain", "Bad Parameters");
        return;
    }
    
    xSemaphoreTake(_mutex, portMAX_DELAY);
    uint32_t   tid  = ++_tx_id;      /// get session txn id
    SessionBuf &buf = _active[tid];  /// create SessionBuf on the fly
    buf.req       = req;             /// keep request pointer
    buf.timestamp = millis();        /// set time-to-live
    buf.is_done   = false;
    
    // --- HTMX Echo Element Optimization ---
    // Instantly inject a clean trace container so the user sees what they typed, 
    // immediately followed by the responsive target block for Forth's evaluation output.
    // Optimized: Only write the tiny structural tag wrapper.
    // The user's code echo text is handled client-side in the browser onsubmit macro layer now.
    const char* tag_start = "<div class='rsp-entry'>";
    buf.write(tag_start, strlen(tag_start));
    xSemaphoreGive(_mutex);

    // Launch the responsive chunk stream pipeline
    AsyncWebServerResponse *rsp = req->beginChunkedResponse(
        "text/html", // Switch text/plain to text/html so HTMX parses the markup container classes
        [this, tid](uint8_t *buf, size_t max, size_t index) -> size_t {
            return this->feed_web_rsp(tid, buf, max);      /// callback handler
        });

    rsp->addHeader("Connection", "keep-alive");
    req->send(rsp);

    if (!parse_req(tid, (char*)p->value().c_str())) {      /// request buffer full
        xSemaphoreTake(_mutex, portMAX_DELAY);
        _active.erase(tid);
        xSemaphoreGive(_mutex);
    }
}

bool XServer::parse_req(uint32_t tid, char *txt) {
    std::string_view view(txt, strlen(txt));
    std::string_view delim("\n");
    size_t    start = 0;
    msg_web_t cmd;

    cmd.id = tid;
    while (start < view.size()) {
        // 1. Skip leading delimiters
        start = view.find_first_not_of(delim, start);
        if (start == std::string_view::npos) break; // Reached the end

        // 2. Find the end of the current token
        size_t end = view.find_first_of(delim, start);

        // 3. Slice out the token view (non-destructively)
        std::string_view token = (end == std::string_view::npos) 
            ? view.substr(start) 
            : view.substr(start, end - start);

        if (!token.empty()) {
            size_t sz = std::min(token.size(), (size_t)(QUE_BUF_SZ - 1));
            memcpy(cmd.buf, token.data(), sz);        /// leave last byte to
            cmd.buf[sz] = '\0';                       /// ensure \0 terminated

            if (_web->put_req(cmd)) {
                 Serial.printf(" >> <%d>'%s' ", (int)sz, (char*)cmd.buf);
            }
            else {
                Serial.printf("_web->put_req failed: '%s'\n", (char*)cmd.buf);
                return false;
            }
        }

        // Move past the current token
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return true;
}

size_t XServer::feed_web_rsp(uint32_t tid, uint8_t *buf, size_t max) {
    size_t bsz = 0;
            
    xSemaphoreTake(_mutex, portMAX_DELAY);
    if (_active.find(tid) != _active.end()) {
        SessionBuf &ses = _active[tid];
            
        bsz = ses.read(buf, max);
            
        // If data is finished and buffer is completely cleared out
        if (ses.is_done && ses.available() == 0) {
            
            // Append a closing tag wrapper element string to complete the HTMX DOM block
            const char* close_tag = "</div><br/>";
            size_t tag_len = strlen(close_tag);
            
            if (max >= tag_len) {
                memcpy(buf, close_tag, tag_len);
                bsz = tag_len;
            }
            
            _active.erase(tid); // Drop session from map footprint context tree safely
            xSemaphoreGive(_mutex);
            return bsz; // Return the terminal HTML tag footprint bytes to close stream
        }
    }
    xSemaphoreGive(_mutex);
    
    return bsz;
}

void XServer::handle_rsp() {
    msg_web_t msg;
    while (_web->get_rsp(msg)) {
        Serial.printf("xs#handle_rsp <<%c [%d]'%s' ", msg.eos ? 'X' : '+', msg.id, (char*)msg.buf);
        xSemaphoreTake(_mutex, portMAX_DELAY);
        if (_active.count(msg.id) > 0) {
            SessionBuf &ses = _active[msg.id];
                
            if (msg.eos) ses.is_done = true;           /// move from Forth responses to session buffer
            else ses.write((const char*)msg.buf, strlen((char*)msg.buf));
                
            // --- THE CRITICAL WAKEUP ---
            // If the TCP client is connected, nudge it to trigger the pull callback again
            if (ses.req == nullptr)                   Serial.print("xserver ses.req is NULL\n");
            else if (ses.req->client() == nullptr)    Serial.print("xserver ses.req->client() is NULL\n");
            else if (!ses.req->client()->connected()) Serial.print("xserver ses.req->client() not connected\n");
            else {
                ses.req->client()->write(NULL, 0);    /// Triggers the network stack to flush/poll
            }
        }
        else Serial.printf("xs#handle_rsp %d not active\n", msg.id);
        xSemaphoreGive(_mutex);
    }
}

void XServer::check_timeout() {
    uint32_t now = millis();

    xSemaphoreTake(_mutex, portMAX_DELAY);
    for (auto it = _active.begin(); it != _active.end(); ) {
        // If request has been waiting longer than 5000ms
        if ((now - it->second.timestamp) < 5000) ++it;
        else {
            // Safely tell the client they timed out and free the connection memory
            it->second.req->send(504, "text/plain", "Forth execution timeout");
                
            // Erase from map safely while iterating
            it = _active.erase(it);
        }
    }
    xSemaphoreGive(_mutex);
}

void XServer::run() {
    setup();
    // Start server. It binds system network handles to background core interrupts.
    _server.begin();

    while (1) {
        handle_rsp();
//        check_timeout();
        // Core HTTP events are handled in the background via hardware network interrupts,
        // so this main thread loop sleeps deeply to let other Core 0 tasks execute.
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
    

