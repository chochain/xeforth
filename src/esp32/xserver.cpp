///
/// @file
/// @brief Web Server class implementation (esp_http_server v2.0.16)
///
#include <algorithm>
#include "xserver.h"

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
    </form>
  </div>
</body>
</html>
)XX";

///> url => text converter
static size_t _url_decode(const char *src, size_t src_len, char *dst, size_t dst_sz) {
    auto hex = [](char h) -> int {
        if (h >= '0' && h <= '9') return h - '0';
        if (h >= 'a' && h <= 'f') return h - 'a' + 10;
        if (h >= 'A' && h <= 'F') return h - 'A' + 10;
        return -1;
    };
    size_t o = 0;
    for (size_t i = 0; i < src_len && o + 1 < dst_sz; ++i) {
        char c = src[i];
        if (c == '+') {
            dst[o++] = ' ';
        } else if (c == '%' && i + 2 < src_len) {
            int hi = hex(src[i + 1]), lo = hex(src[i + 2]);
            if (hi >= 0 && lo >= 0) {
                dst[o++] = (char)((hi << 4) | lo);
                i += 2;
            } else {
                dst[o++] = c;
            }
        } else {
            dst[o++] = c;
        }
    }
    dst[o] = '\0';
    return o;
}
///@name - static web worker task (see xserver.h ASYNC_WORKER_COUNT)
///@{
void XServer::worker_task(void *pv) {
    XServer *self = static_cast<XServer*>(pv);
    AsyncReqTask task;

    while (1) {
        if (xQueueReceive(self->_async_queue, &task, portMAX_DELAY) == pdTRUE) {
            // req body was already read + parsed synchronously in submit_async(),
            // inside the URI handler's valid scope. This task only ever touches
            // (hd, fd, tid) — plain values, no dangling pointer risk.
            self->_stream_session(task.hd, task.fd, task.tid);
            self->_close_session(task.tid);
            xSemaphoreGive(self->_worker_ready_count);
        }
    }
}
///@}
///@name - web server main loop
///@{
void XServer::run() {
    setup();
    while (1) {
        handle_rsp();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void XServer::setup() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(_ssid, _password);

    while (WiFi.status() != WL_CONNECTED) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        LOG("%c",'.');
    }

    LOG("\ncore0 xsvr> live at http://%s\n",
                  WiFi.localIP().toString().c_str());

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port       = _port;
    config.lru_purge_enable  = true;
    config.core_id           = 0;
    config.max_open_sockets  = ASYNC_WORKER_COUNT + 2;
    config.stack_size        = 8192;        /// budget for decode buffer

    if (httpd_start(&_httpd, &config) != ESP_OK) {
        ERR("xsvr> httpd_start failed");
        return;
    }

    _async_queue        = xQueueCreate(ASYNC_QUEUE_LEN, sizeof(AsyncReqTask));
    _worker_ready_count = xSemaphoreCreateCounting(ASYNC_WORKER_COUNT, ASYNC_WORKER_COUNT);
    for (int i = 0; i < ASYNC_WORKER_COUNT; ++i) {
        char name[16];
        snprintf(name, sizeof(name), "xsvr_wrk%d", i);
        xTaskCreatePinnedToCore(XServer::worker_task, name, 8192, (void*)this, 5, &_workers[i], 0);
    }

    httpd_uri_t root_uri = {
        .uri      = "/",
        .method   = HTTP_GET,
        .handler  = [](httpd_req_t *req) {
            httpd_resp_set_type(req, "text/html");
            return httpd_resp_send(req, HTML_INDEX, HTTPD_RESP_USE_STRLEN);
        },
        .user_ctx = this
    };
    httpd_register_uri_handler(_httpd, &root_uri);

    httpd_uri_t exec_uri = {
        .uri      = "/execute",
        .method   = HTTP_POST,
        .handler  = [](httpd_req_t *req) {
            return static_cast<XServer*>(req->user_ctx)->handle_web_req(req);
        },
        .user_ctx = this
    };
    httpd_register_uri_handler(_httpd, &exec_uri);
}
///@}
///@name - web request handler
///@{
esp_err_t XServer::handle_web_req(httpd_req_t *req) {
    if (xSemaphoreTake(_worker_ready_count, 0) != pdTRUE) {
        ERR("xsvr> no worker available, request rejected");
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_send(req, "Server busy, try again", HTTPD_RESP_USE_STRLEN);
    }

    int sockfd = httpd_req_to_sockfd(req);
    if (sockfd < 0) {
        xSemaphoreGive(_worker_ready_count);
        return ESP_FAIL;
    }

    /// Read + URL-decode the POST body here, while req is still valid.
    /// Bounded by FORM_BUF_SZ — this is the only req-touching work left,
    /// and it's exactly what esp_http_server expects a handler to do.
    /// takes 2K here, task.stack_size adjusted to 8K
    /// consider auto decoded = std::make_unique<char[]>(FORM_BUF_SZ);
    char decoded[FORM_BUF_SZ];

    if (!_read_form(req, decoded, sizeof(decoded))) {
        xSemaphoreGive(_worker_ready_count);
        return ESP_FAIL;   // read_form() already sent the error response
    }

    uint32_t tid = _open_session();

    // parse_req() only touches _web (a thread-safe MBox), not req — safe here.
    if (!_parse_req(tid, decoded)) {
        _close_session(tid);
        xSemaphoreGive(_worker_ready_count);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Queue full");
        return ESP_FAIL;
    }

    // Hand off only plain values. No req pointer crosses the task boundary.
    AsyncReqTask task{ req->handle, sockfd, tid };
    if (xQueueSend(_async_queue, &task, pdMS_TO_TICKS(100)) != pdTRUE) {
        ERR("xsvr> async queue full, request rejected");
        _close_session(tid);
        xSemaphoreGive(_worker_ready_count);
        return ESP_FAIL;
    }

    // Return without calling httpd_resp_send*(). The worker task owns the
    // socket from here via httpd_socket_send(), independent of req's lifetime.
    return ESP_OK;
}

bool XServer::_read_form(httpd_req_t *req, char *out, size_t out_sz) {
    int total = req->content_len;
    if (total <= 0 || total >= FORM_BUF_SZ) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad Parameters");
        return false;
    }

    std::string raw(total, '\0');
    int received = 0;
    while (received < total) {
        int r = httpd_req_recv(req, &raw[received], total - received);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad Parameters");
            return false;
        }
        received += r;
    }

    const char *key  = "forth_code=";
    size_t      klen = strlen(key);
    size_t      pos  = raw.find(key);
    if (pos == std::string::npos) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad Parameters");
        return false;
    }
    pos += klen;
    size_t end  = raw.find('&', pos);
    size_t vlen = (end == std::string::npos ? raw.size() : end) - pos;

    _url_decode(raw.c_str() + pos, vlen, out, out_sz);
    return true;
}

bool XServer::_parse_req(uint32_t tid, char *txt) {
    std::string_view view(txt, strlen(txt));
    std::string_view delim("\n");
    size_t    start = 0;
    msg_web_t cmd;
    cmd.id = tid;
    while (start < view.size()) {
        start = view.find_first_not_of(delim, start);
        if (start == std::string_view::npos) break;

        size_t end = view.find_first_of(delim, start);
        std::string_view token = (end == std::string_view::npos)
            ? view.substr(start)
            : view.substr(start, end - start);

        if (!token.empty() && token.back() == '\r') token.remove_suffix(1);
        if (!token.empty()) {
            size_t sz = std::min(token.size(), (size_t)(QUE_BUF_SZ - 1));
            memcpy(cmd.buf, token.data(), sz);
            cmd.buf[sz] = '\0';
            if (_web->put_req(cmd)) {
                LOG(" >> <%d>'%s'\n", (int)sz, (char*)cmd.buf);
            }
            else {
                LOG("_web->put_req failed: '%s'\n", (char*)cmd.buf);
                return false;
            }
        }
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return true;
}

uint32_t XServer::_open_session() {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    uint32_t   tid  = ++_tx_id;
    SessionBuf &ses = _active[tid];
    ses.head        = 0;
    ses.tail        = 0;
    ses.is_done     = false;
    ses.timestamp   = millis();
    if (ses.notify == nullptr) ses.notify = xSemaphoreCreateBinary();
    xSemaphoreGive(_mutex);
    return tid;
}

void XServer::_close_session(uint32_t tid) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _active.erase(tid);
    xSemaphoreGive(_mutex);
}
///@}
///@name - web response handler
///@{
void XServer::handle_rsp() {
    msg_web_t msg;
    while (_web->get_rsp(msg)) {
        LOG("xs#handle_rsp <<%c [%d]'%s' ", msg.eos ? 'X' : '+', msg.id, (char*)msg.buf);
        xSemaphoreTake(_mutex, portMAX_DELAY);
        auto it = _active.find(msg.id);
        if (it != _active.end()) {
            SessionBuf &ses = it->second;
            if (msg.eos) ses.is_done = true;
            else         ses.write((const char*)msg.buf, strlen((char*)msg.buf));

            if (ses.notify != nullptr) xSemaphoreGive(ses.notify);
        }
        else LOG("xs#handle_rsp %d not active\n", msg.id);
        xSemaphoreGive(_mutex);
    }
}

// Completely reworked to use lower level socket chunking for custom async delivery
void XServer::_stream_session(httpd_handle_t hd, int fd, uint32_t tid) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    auto it = _active.find(tid);
    xSemaphoreGive(_mutex);
    if (it == _active.end()) return;

    SessionBuf &ses = it->second; // Send HTTP headers manually since we have taken direct socket control ownership
    const char* initial_chunks = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nTransfer-Encoding: chunked\r\nConnection: keep-alive\r\n\r\n";
    if (httpd_socket_send(hd, fd, initial_chunks, strlen(initial_chunks), 0) <= 0) { return; }

    const char* open_wrapper = "<div class='rsp-entry'>";
    char chunk_header[32];       // 🚀 FIX 2: Increased buffer footprint size to safely parse lengths

    snprintf(chunk_header, sizeof(chunk_header), "%X\r\n", strlen(open_wrapper));
    httpd_socket_send(hd, fd, chunk_header, strlen(chunk_header), 0);
    httpd_socket_send(hd, fd, open_wrapper, strlen(open_wrapper), 0);
    httpd_socket_send(hd, fd, "\r\n", 2, 0);

    uint8_t  out[SES_BUF_SZ];
    uint32_t started = millis();
    while (true) {
        xSemaphoreTake(ses.notify, pdMS_TO_TICKS(WAIT_POLL_MS));
        xSemaphoreTake(_mutex, portMAX_DELAY);
        size_t n    = ses.read(out, sizeof(out));
        bool   done = ses.is_done && ses.available() == 0;
        xSemaphoreGive(_mutex);

        if (n > 0) {
            snprintf(chunk_header, sizeof(chunk_header), "%X\r\n", n);
            if (httpd_socket_send(hd, fd, chunk_header, strlen(chunk_header), 0) <= 0 ||
                httpd_socket_send(hd, fd, (const char*)out, n, 0) <= 0 ||
                httpd_socket_send(hd, fd, "\r\n", 2, 0) <= 0) { break; }
        }
        if (done) break;

        if (millis() - started > REQ_TIMEOUT_MS) {
            const char* timeout_msg = "[Forth execution timeout]";
            snprintf(chunk_header, sizeof(chunk_header), "%X\r\n", strlen(timeout_msg));
            httpd_socket_send(hd, fd, chunk_header, strlen(chunk_header), 0);
            httpd_socket_send(hd, fd, timeout_msg, strlen(timeout_msg), 0);
            httpd_socket_send(hd, fd, "\r\n", 2, 0);
            break;
        }
    }
    // Send closing layout wrappers and terminal chunk signals safely
    const char* final_wrapper = "</div><br/>";
    snprintf(chunk_header, sizeof(chunk_header), "%X\r\n", strlen(final_wrapper));
    httpd_socket_send(hd, fd, chunk_header, strlen(chunk_header), 0);
    httpd_socket_send(hd, fd, final_wrapper, strlen(final_wrapper), 0);
    httpd_socket_send(hd, fd, "\r\n", 2, 0);
    // Final end-of-transfer empty chunk marker
    httpd_socket_send(hd, fd, "0\r\n\r\n", 5, 0);
}
///@}
///@name - public interface
///@{
bool XServer::begin(xQueWeb *web, int priority) {
    if (web == NULL) return false;
    _web = web;

    BaseType_t xReturned = xTaskCreatePinnedToCore(
        [](void *pv) { static_cast<XServer*>(pv)->run(); },
        "Forth_Rsp_Pump",
        4096,
        (void*)this,
        (BaseType_t)priority,
        &_task,
        0
    );
    return (xReturned == pdPASS);
}
///@}
