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
    .abort-btn { background:#800000; color:#fff; border:1px; padding:10px; font-weight:bold; cursor:pointer; margin-top:5px; }
    .done-btn  { background:#008000; color:#fff; border:1px; padding:10px; font-weight:bold; cursor:pointer; margin-top:5px; }
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

struct HttpContext {
    httpd_handle_t hd;
    int            fd;
};

// Static bridging translator function inside xserver.cpp
static void handle_overflow(void* arg, const char* failed_line) {
    auto* ctx = static_cast<HttpContext*>(arg);
    if (!ctx) return;

    const char* err = "<div style='color:#ffaa00;'>\r\n[SYSTEM ERROR] Pipeline Saturated: Script truncated, engine busy.</div>";
    char hdr[16];
    snprintf(hdr, sizeof(hdr), "%X\r\n", strlen(err));
    
    httpd_handle_t hd = ctx->hd;
    int            fd = ctx->fd;
    
    // Direct socket flush handles the immediate bypass beautifully
    httpd_socket_send(hd, fd, hdr, strlen(hdr), 0);
    httpd_socket_send(hd, fd, err, strlen(err), 0);
    httpd_socket_send(hd, fd, "\r\n", 2, 0);
}

static esp_err_t handle_abort(httpd_req_t *req) {
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
        Sys.send(x, 0, true);          /// priority message to front of queue
        
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

// Modify execute_handler inside xserver.cpp:
static esp_err_t handle_execute(httpd_req_t *req) {
    XServer *server = static_cast<XServer*>(req->user_ctx);
    int client_sockfd = httpd_req_to_sockfd(req);
    if (client_sockfd < 0) return ESP_FAIL;

    char raw_val[FORM_BUF_SZ];
    size_t raw_len = 0;
    if (!server->read_form(req, raw_val, sizeof(raw_val), raw_len)) {
        return ESP_FAIL;
    }

    // Allocate a strictly localized session ID
    uint32_t sid = Sys.alloc_id();         ///< session id

    DEBUG("session[%d] created\n", sid);

    SessionActor *ses = new SessionActor(sid, client_sockfd, req->handle);
    Sys.register_actor(ses);

    // Kick off execution
    HttpContext ctx { req->handle, client_sockfd };
    LineSink    sink(sid, handle_overflow, &ctx);
    sink_result_t rc = sink.split_and_stream(raw_val, raw_len);
    if (rc != SINK_OK) LOG("execute: req[%u] rejected (%d)\n", sid, (int)rc);

    return ESP_OK;
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
        .handler = handle_execute,
        .user_ctx = this
    };
    httpd_register_uri_handler(_httpd, &exec_uri);

    httpd_uri_t abort_uri = {
        .uri = "/abort",
        .method = HTTP_POST,
        .handler = handle_abort,
        .user_ctx = this
    };
    httpd_register_uri_handler(_httpd, &abort_uri);
    
    LOG("\ncore0 xsvr> live at http://%s\n", WiFi.localIP().toString().c_str());
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
