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

#if 0
// Embed the responsive HTML interface cleanly inside the flash layout space
const char HTML_INDEX[] PROGMEM = R"XX(
<!DOCTYPE html>
<html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Forth Console</title>
<style>
  body     { font-family:monospace; background:#1a1a1a; color:#00ff00; padding:20px; }
  textarea { width:100%; height:120px; background:#000; color:#00ff00; border:1px solid #00ff00; padding:10px; font-size:16px; box-sizing:border-box; }
  button   { background:#00ff00; color:#000; border:none; padding:12px; font-weight:bold; cursor:pointer; margin-top:10px; width:100%; font-size:16px; }
</style></head>
<body>
  <h2>FORTH PIPELINE</h2>
  <textarea id="code" placeholder="Enter commands..."></textarea>
  <button onclick="send()">EXECUTE</button>
  <script>
    function send() {
      let f=new FormData();
      f.append("forth_code", document.getElementById("code").value);
      fetch('/execute',{ method:'POST',body:f })
      .then(r=>r.text())
      .then(d=>console.log(d));
    }
  </script>
</body></html>
)XX";
#endif

// Embed the HTML code cleanly as a static string block
const char *HTML_INDEX PROGMEM = R"XX(
HTTP/1.1 200 OK
Content-type:text/html

<html>
<head>
  <meta charset='UTF-8'><title>xeForth on ESP32</title>
  <meta http-equiv="Cross-Origin-Embedder-Policy" content="require-corp">
  <meta http-equiv="Cross-Origin-Opener-Policy" content="same-origin">
  <style>body{font-family:'Courier New',monospace;font-size:14px;}</style>
</head>
<body>
    <div id='log' style='float:left;overflow:auto;height:100%;width:60%;
         background-color:#f8f0f0;'>xeForth 1.0</div>
    <textarea id='tib' style='height:100%;width:40%;resize:none'
        onkeydown='if (13===event.keyCode) forth()'></textarea>
</body>
<script>
let log = document.getElementById('log')
let tib = document.getElementById('tib')
let idx = 0
function send_post(url, ary) {
    let id  = '_'+(idx++).toString()
    let cmd = '\n---CMD'+id+'\n'
    let req = ary.slice(0,30).join('\n')
    let frm = new FormData()
    frm.append('forth_code', req)
    log.innerHTML += '<div id='+id+'><font color=blue>'+
                     req.replace(/\n/g,'<br/>')+'</font><br/></div>'
    fetch(url, {
        method: 'POST', headers: { 'Context-Type': 'text/plain' },
        body: frm
    })
    .then(rsp=>rsp.text())
    .then(txt=>{
        document.getElementById(id).innerHTML +=
            txt.replace(/\n/g,'<br/>').replace(/\s/g,'&nbsp;')
        log.scrollTop=log.scrollHeight
        ary.splice(0,30)
        if (ary.length > 0) send_post(url, ary)
    })
}
function forth() {
    let ary = tib.value.split('\n')
    if (ary.length > 0) send_post('/execute', ary)
    tib.value = ''; tib.focus(); return false
}
window.onload = ()=>forth()
</script></html>

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
    Serial.printf("xs#process tid=%d => ", _tx_id);
    xSemaphoreTake(_mutex, portMAX_DELAY);
    uint32_t   tid= ++_tx_id;
    SessionBuf buf;
    buf.req       = req;             /// <--- Save the pointer
    buf.timestamp = millis();        /// set time-to-live
    _active[tid]  = buf;
    buf.is_done   = false;
    xSemaphoreGive(_mutex);

    AsyncWebServerResponse *rsp = req->beginChunkedResponse(
        "text/plain", 
        [this, tid](uint8_t *buf, size_t max, size_t index) -> size_t {
            return this->feed_web_rsp(tid, buf, max);      /// callback handler
        });

    // Commit headers out to browser
    req->send(rsp);                                        /// set response handler

    Serial.printf("xs#process _active=%d << req[%d] %s => ",
                  _active.count(tid), tid, (char*)p->value().c_str());
    if (!parse_req(tid, (char*)p->value().c_str())) {      /// request buffer full
        xSemaphoreTake(_mutex, portMAX_DELAY);
        _active.erase(tid);
        xSemaphoreGive(_mutex);
        Serial.printf("failed _active.count(%d)=%d", tid, _active.count(tid));
    }
    Serial.printf("\n");
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
                 Serial.printf(" >> [%d]'%s' ", (int)sz, (char*)cmd.buf);
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
    if (_active.count(tid) > 0) {
        SessionBuf &ses = _active[tid];
            
        if (ses.available() > 0) bsz = ses.read(buf, max);
            
        // ONLY return 0 (EOF) if Forth said it is done AND the buffer is dry
//        Serial.printf("xs#feed_web_rsp ses.is_done=%d ses.available()=%d\n", ses.is_done, ses.available());
        if (ses.is_done && ses.available() == 0) {
            _active.erase(tid);
            bsz = 0;
        }
        // CRITICAL: If we have no data right now, but Forth isn't done, 
        // return a tiny dummy value or a space, OR return 0 but do NOT erase.
        // To keep the connection alive without closing, we return 0 here safely 
        // because we will manually wake up the TCP client from the other task.
    }
    else Serial.printf("xs#feed_web_rsp _active.count(%d)=%d ", tid, _active.count(tid));
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
            if (ses.req == nullptr)                   Serial.print("xserver ses.req is NULL");
            else if (ses.req->client() == nullptr)    Serial.print("xserver ses.req->client() is NULL");
            else if (!ses.req->client()->connected()) Serial.print("xserver ses.req->client() not connected");
            else {
                ses.req->client()->write(NULL, 0);    /// Triggers the network stack to flush/poll
            }
        }
        else Serial.printf("xs#handle_rsp %d not active ", msg.id);
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
    

