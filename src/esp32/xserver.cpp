/// -*- mode: c++ -*-
#include "xserver.h"
#include "xserver_actor.h"
#include "xactor.h"
#include "xlinesink.h"

#define FORTH_ACTOR_GLOBAL_ID 1

static const char *HTML_INDEX PROGMEM = R"XX(<!DOCTYPE html>
<html>
<head>
  <meta charset='UTF-8'>
  <title>xeForth Panel</title>
  <script src="https://unpkg.com/htmx.org@2.0.4"></script>
  <style>
    body { font-family:'Courier New', monospace; font-size:14px; background:#121212; color:#00ff00; padding:10px; margin:0; }
    #container { display: flex; height: 95vh; }
    #log { flex: 0 0 60%; background-color:#1a1a1a; border: 1px solid #333; overflow-y:auto; padding:10px; box-sizing:border-box; }
    #tib-form { flex: 0 0 40%; display: flex; flex-direction: column; }
    #tib { flex: 1; background:#000; color:#00ff00; border:1px solid #333; resize:none; padding:10px; font-family:inherit; font-size:inherit; }
    .cmd-entry { color: #00bcff; margin-top: 5px; }
    .rsp-entry { color: #00ff00; white-space: pre-wrap; }
    .abort-btn { background:#ff0000; color:#fff; border:none; padding:10px; font-weight:bold; cursor:pointer; margin-bottom:5px; }
  </style>
</head>
<body>
  <div id='container'>
    <div id='log' hx-on::after-swap="if (this.scrollHeight - this.scrollTop - this.clientHeight < 300) this.scrollTop = this.scrollHeight">xeForth v1.0 Initialized...<br/></div>
    <div id='tib-form'>
      <button class="abort-btn" hx-post="/abort" hx-target="#log" hx-swap="beforeend">EMERGENCY BREAK (ABORT)</button>
      <form hx-post='/execute' hx-target='#log' hx-swap='beforeend' hx-on::after-request="document.getElementById('tib').value=''"
        onsubmit="const log=document.getElementById('log'); log.innerHTML += '<div class='cmd-entry'>&gt; ' + document.getElementById('tib').value.replace(/
/g,'<br/>') + '</div>'; log.scrollTop = log.scrollHeight">
      <textarea id='tib' name='forth_code' placeholder='Type Forth code here...'
        onkeydown="if(event.keyCode===13 && !event.shiftKey) { event.preventDefault(); htmx.trigger(this.form, 'submit'); }"></textarea>
      </form>
    </div>
  </div>
</body>
</html>
)XX";

static uint32_t last_active_session = 0;

esp_err_t execute_handler(httpd_req_t *req) {
    XServer *server = static_cast<XServer*>(req->user_ctx);
    int client_sockfd = httpd_req_to_sockfd(req);
    if (client_sockfd < 0) return ESP_FAIL;

    char raw_val[FORM_BUF_SZ];
    size_t raw_len = 0;
    if (!server->read_form(req, raw_val, sizeof(raw_val), raw_len)) {
        return ESP_FAIL;
    }

    uint32_t session_id = Sys.alloc_id();
    last_active_session = session_id;
    
    SessionActor *session = new SessionActor(session_id, client_sockfd, req->handle);
    Sys.register_actor(session);

    LineSink sink(session_id, JOB_DEMAND);
    sink.split_and_stream(raw_val, raw_len);

    return ESP_OK;
}

esp_err_t abort_handler(httpd_req_t *req) {
    if (last_active_session != 0) {
        ActorMsg abort_msg;
        abort_msg.type = MSG_FORTH_ABORT;
        abort_msg.target_id = FORTH_ACTOR_GLOBAL_ID;
        abort_msg.fd = (int)last_active_session;
        
        // Priority Line-Cut: Insert directly to the front of the queue
        Sys.send_priority(abort_msg);
    }
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, "<div style='color:red;'>[System Interrupt Broadcasted]</div>", HTTPD_RESP_USE_STRLEN);
}

bool XServer::begin(int priority) {
    setup();
    return true;
}

void XServer::setup() {
    WiFi.mode(WIFI_STA);
    while (WiFi.status() != WL_CONNECTED) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    Serial.printf("\ncore0 xsvr> live at http://%s\n", WiFi.localIP().toString().c_str());

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = _port;
    config.core_id     = 0;
    config.stack_size  = 4096; // Lightweight routing thread stack footprint

    if (httpd_start(&_httpd, &config) != ESP_OK) return;

    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = [](httpd_req_t *req) {
            httpd_resp_set_type(req, "text/html");
            return httpd_resp_send(req, HTML_INDEX, HTTPD_RESP_USE_STRLEN);
        },
        .user_ctx = this
    };
    httpd_register_uri_handler(_httpd, &root_uri);

    httpd_uri_t exec_uri = {
        .uri = "/execute",
        .method = HTTP_POST,
        .handler = execute_handler,
        .user_ctx = this
    };
    httpd_register_uri_handler(_httpd, &exec_uri);

    httpd_uri_t abort_uri = {
        .uri = "/abort",
        .method = HTTP_POST,
        .handler = abort_handler,
        .user_ctx = this
    };
    httpd_register_uri_handler(_httpd, &abort_uri);
}

bool XServer::read_form(httpd_req_t *req, char *out, size_t out_sz, size_t &out_len) {
    int total = req->content_len;
    if (total <= 0 || total >= FORM_BUF_SZ) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad Parameters");
        return false;
    }
    char raw[FORM_BUF_SZ];
    int received = 0;
    while (received < total) {
        int r = httpd_req_recv(req, raw + received, total - received);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad Parameters");
            return false;
        }
        received += r;
    }
    raw[total] = '\0';

    const char *key = "forth_code=";
    const char *pos = strstr(raw, key);
    if (!pos) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad Parameters");
        return false;
    }
    pos += strlen(key);
    const char *amp = strchr(pos, '&');
    size_t vlen = amp ? (size_t)(amp - pos) : strlen(pos);

    if (vlen >= out_sz) vlen = out_sz - 1;
    memcpy(out, pos, vlen);
    out[vlen] = '\0';
    out_len = vlen;
    return true;
}
