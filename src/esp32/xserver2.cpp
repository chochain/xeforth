/// -*- mode: c++ -*-
///
/// @file
/// @brief Web Server class implementation (esp_http_server v2.0.16)
///
#include "xlinesink2.h"          /// include xactor.h, xforth_actor.h
#include "xserver_actor.h"
#include "xserver2.h"

static constexpr char *HTML_INDEX PROGMEM = R"XX(<!DOCTYPE html>
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
    .abort-btn { background:#880000; color:#fff; border:none; padding:10px; font-weight:bold; cursor:pointer; margin-bottom:5px; }
  </style>
</head>
<body>
  <div id='container'>
    <div id='log'
      hx-on::after-swap="if (this.scrollHeight - this.scrollTop - this.clientHeight < 300) this.scrollTop = this.scrollHeight">xeForth v1.0 Initialized...<br/></div>
    <form id='tib-form'
      hx-post='/execute'
      hx-target='#log'
      hx-swap='beforeend'
      hx-on::after-request="this.reset()"
      onsubmit="const log=document.getElementById('log'); log.innerHTML += '<div class=\'cmd-entry\'>&gt; ' + document.getElementById('tib').value.replace(/\n/g,'<br/>') + '</div>'; log.scrollTop = log.scrollHeight">
    <textarea id='tib' name='forth_code'
      placeholder='Type Forth code here...'
      onkeydown="if(event.keyCode===13 && !event.shiftKey) {
        event.preventDefault();
        htmx.trigger('#tib-form', 'submit');
      }"></textarea>
    <div id="abort-control-slot">server will inject button here</div> 
    </form>
  </div>
</body>
</html>
)XX";

// Modify execute_handler inside xserver.cpp:
esp_err_t execute_handler(httpd_req_t *req) {
    XServer *server = static_cast<XServer*>(req->user_ctx);
    int client_sockfd = httpd_req_to_sockfd(req);
    if (client_sockfd < 0) return ESP_FAIL;

    char raw_val[FORM_BUF_SZ];
    size_t raw_len = 0;
    if (!server->read_form(req, raw_val, sizeof(raw_val), raw_len)) {
        return ESP_FAIL;
    }

    // 1. Allocate a strictly localized session ID
    uint32_t sid = Sys.alloc_id();         ///< session id
    
    SessionActor *ses = new SessionActor(sid, client_sockfd, req->handle);
    Sys.register_actor(ses);

    // 2. ⚡ THE TRICK: Instantly inject an abort button tied to this exact ID back to the caller's browser.
    // HTMX hx-swap-oob (Out-Of-Bounds) will swap this directly into the target slot automatically.
    char oob_buf[256];
    snprintf(oob_buf, sizeof(oob_buf),
        "<div id='abort-control-slot' hx-swap-oob='true'>"
        "<button class='abort-btn' hx-post='/abort?id=%u' hx-target='#log' hx-swap='beforeend'>"
        "STOP (session %u)</button></div>", sid, sid);
             
    // Force transmission down the raw client socket instantly
    httpd_socket_send(req->handle, client_sockfd, oob_buf, strlen(oob_buf), 0);

    // 3. Kick off execution
    LineSink sink(sid, JOB_DEMAND);
    sink_result_t rc = sink.split_and_stream(raw_val, raw_len);
    if (rc != SINK_OK) {
        LOG("execute: submission %u rejected (%d)\n", sid, (int)rc);
    }
    return ESP_OK;
}

esp_err_t abort_handler(httpd_req_t *req) {
    // 1. Safe extraction of the target query parameters from the HTTP URI context
    char     buf[32];
    uint32_t sid = 0; ///< target session to kill
    
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char val[16];
        if (httpd_query_key_value(buf, "id", val, sizeof(val)) == ESP_OK) {
            sid = strtoul(val, nullptr, 10);
        }
    }

    // 2. Only broadcast preemption if a valid extraction occurred
    if (sid != 0) {
        ActorMsg x { MSG_FORTH_ABORT, FORTH_ACTOR_GLOBAL_ID, sid, -1 };
        Sys.send_priority(x);
        
        // 3. Clear out the abort button from the caller's interface since it was triggered
        httpd_resp_set_type(req, "text/html");
        return httpd_resp_send(req,
            "<div id='abort-control-slot' hx-swap-oob='true'></div>"
            "<div style='color:red;'>[Interrupt Broadcasted to Session]</div>",
            HTTPD_RESP_USE_STRLEN);
    }

    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid Session ID");
    return ESP_FAIL;
}

bool XServer::begin(int priority) {
    setup();
    return true;
}

void XServer::setup() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(_ssid, _password);
    while (WiFi.status() != WL_CONNECTED) {
        LOG("%c", '.');
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    LOG("\ncore0 xsvr> live at http://%s\n", WiFi.localIP().toString().c_str());

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = _port;
    config.core_id     = 0;
    config.stack_size  = 8192; // read_form() alone puts 2 KB on the stack; 4096 overflowed

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
