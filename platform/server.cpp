///
/// @file
/// @brief Async Web Server class implementation
///
#include "server.h"

// Define a local structural size check to match our structural payload configurations
#define MAX_FORTH_CMD_LEN 128
typedef struct {
    char command_text[MAX_FORTH_CMD_LEN];
} web_msg_t;

const char *HTML_CHUNKED PROGMEM = R"XX(
HTTP/1.1 200 OK
Content-type:text/plain
Transfer-Encoding: chunked

)XX";

// Embed the responsive HTML interface cleanly inside the flash layout space
const char index_html[] PROGMEM = R"XX(
<!DOCTYPE html>
<html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Forth Console</title>
<style>
  body     { font-family:monospace; background:#1a1a1a; color:#00ff00; padding:20px; }
  textarea { width:100%; height:120px; background:#000; color:#00ff00; border:1px solid #00ff00; padding:10px; font-size:16px; box-sizing:border-box; }
  button   { background:#00ff00; color:#000; border:none; padding:12px; font-weight:bold; cursor:pointer; margin-top:10px; width:100%; font-size:16px; }
</style></head>
<body>
  <h2>📟 FORTH PIPELINE</h2>
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

#if 0
// Embed the HTML code cleanly as a static string block
const char index_html[] PROGMEM = R"XX(
<!-- Paste the index.html code layout content here -->
)XX";

const char *HTML_INDEX PROGMEM = R"XX(
HTTP/1.1 200 OK
Content-type:text/html

<html>
<head>
  <meta charset='UTF-8'><title>eForth on ESP32</title>
  <meta http-equiv="Cross-Origin-Embedder-Policy" content="require-corp">
  <meta http-equiv="Cross-Origin-Opener-Policy" content="same-origin">
  <style>body{font-family:'Courier New',monospace;font-size:14px;}</style>
</head>
<body>
    <div id='log' style='float:left;overflow:auto;height:100%;width:60%;
         background-color:#f8f0f0;'>eForth 5.0</div>
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
    log.innerHTML += '<div id='+id+'><font color=blue>'+
                     req.replace(/\n/g,'<br/>')+'</font><br/></div>'
    fetch(url, {
        method: 'POST', headers: { 'Context-Type': 'text/plain' },
        body: cmd+req+cmd
     }).then(rsp=>rsp.text()).then(txt=>{
        document.getElementById(id).innerHTML +=
            txt.replace(/\n/g,'<br/>').replace(/\s/g,'&nbsp;')
        log.scrollTop=log.scrollHeight
        ary.splice(0,30)
        if (ary.length > 0) send_post(url, ary)
    })
}
function forth() {
    let ary = tib.value.split('\n')
    send_post('/input', ary)
    tib.value = ''; tib.focus(); return false
}
window.onload = ()=>forth()
</script></html>

)XX";
#endif 

bool EmbeddedWebServer::begin(xQueWeb *web_q, UBaseType_t task_priority) {
    if (web_q == NULL) return false;
    _out_q = web_q;

    // Launch the background FreeRTOS execution thread on Core 0
    // We pass "this" (the memory address of this class instance) into the 4th parameter slot!
    BaseType_t xReturned = xTaskCreatePinnedToCore(
        vTaskServerBridge,     // Static function bridge pointer
        "Web_Async_Task",      // Task string identifier name
        4096,                  // Task stack depth allocation (bytes)
        (void*)this,           // 👈 PASS 'THIS' CONTEXT POINTER HERE
        task_priority,         // Priority assignment configuration
        &_task,                // Target task handle tracker
        0                      // Pin strictly to Core 0 (leaving Core 1 free for LVGL)
    );
    return (xReturned == pdPASS);
}

void EmbeddedWebServer::runServerLoop() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(_ssid, _password);
    
    while (WiFi.status() != WL_CONNECTED) {
        vTaskDelay(pdMS_TO_TICKS(500)); 
    }
    
    Serial.printf("\n[EmbeddedWebServer Class]: Dashboard live at http://%s\n", 
                  WiFi.localIP().toString().c_str());

    // Route A: Serve the UI Dashboard Home Page
    _server.on("/", HTTP_GET, [](AsyncWebServerRequest *req){
        req->send_P(200, "text/html", index_html);
    });

    // Route B: Handle Incoming Async Data Submissions
    // Using a C++ lambda expression that captures the 'this' instance context pointer cleanly via [this]
    _server.on("/execute", HTTP_POST, [this](AsyncWebServerRequest *req){
        if (req->hasParam("forth_code", true)) {
            AsyncWebParameter* p = req->getParam("forth_code", true);
            
            que_msg_t msg;
            strncpy(msg.buf, p->value().c_str(), QUE_BUF_SZ - 1);
            msg.buf[QUE_BUF_SZ - 1] = '\0';

            // Non-blocking payload push straight across threads using our member queue
            if (xQueueSend(this->_out_q, &msg, 0) == pdTRUE) {
                req->send(200, "text/plain", "Queued.");
            } else {
                req->send(500, "text/plain", "Queue Buffer Full Error");
            }
        } else {
            req->send(400, "text/plain", "Bad Parameters");
        }
    });

    // Start server. It binds system network handles to background core interrupts.
    _server.begin();

    while (1) {
        // Core HTTP events are handled in the background via hardware network interrupts,
        // so this main thread loop sleeps deeply to let other Core 0 tasks execute.
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
    

