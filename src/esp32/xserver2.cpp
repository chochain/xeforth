/// -*- mode: c++ -*-
///
/// @file
/// @brief Web Server class implementation (esp_http_server v2.0.16)
///
#include "xserver_coordinator.h"
#include "xserver2.h"
#include "esp_heap_caps.h"

static const char HTML_INDEX[] PROGMEM = R"XX(<!DOCTYPE html>
<html>
<head>
  <meta charset='UTF-8'>
  <title>xeForth Panel</title>
  <script src="https://unpkg.com/htmx.org@2.0.4"></script>
  <style>
    body { font-family:'Courier New', monospace; font-size:14px; background:#111; color:#0f0; padding:10px; margin:0; }
    #container { display: flex; height: 95vh; }
    #control   { flex: 0 1 auto; flex-direction: column; background-color:#222; }
    .abort-btn { background:#555; color:#aaa; border:none; font-weight:bold; cursor:not-allowed; width:100%; transition: 0.2s; }
    .abort-btn[data-run="true"] { background:#f00; color:#fff; cursor:pointer; }
    #log { flex: 0 0 58%; background-color:#222; border: 1px solid #333; overflow-y:auto; padding:10px; box-sizing:border-box; white-space: pre-wrap; }
    #tib-form  { flex: 0 0 40%; display: flex; flex-direction: column; }
    #tib-form form { flex: 1; display: flex; flex-direction: column; margin: 0; }
    #tib { flex: 1; background:#000; color:#0f0; border:1px solid #333; resize:none; padding:10px; font-family:inherit; font-size:inherit; }
    .cmd-entry { color: #0cf; margin-top: 5px; }
  </style>
  <script>
    // Define a tiny global HTMX extension to map a textarea straight into a raw HTTP POST body payload
    htmx.defineExtension('raw-text-body', {
      onEvent: function (name, evt) {
        if (name === "htmx:configRequest") {
          // Set text/plain MIME type header context variables
          evt.detail.headers['Content-Type'] = "text/plain"; // 
        }
      },
      encodeParameters: function(xhr, parameters, elt) {
        // Bypass key/value parsing completely, return the exact string inside the textarea
        const tibField = document.getElementById('tib-hidden-copy');
        return tibField ? tibField.value : "";
      }
    });
  </script>
</head>
<body>
  <!-- Hidden memory element to hold onto the code line during the HTMX request loop cycle -->
  <input type="hidden" id="tib-hidden-copy" value="" />
  <div id="staging" style="display:none;"
    hx-on::after-swap="
      const log = document.getElementById('log');
      const raw = this.innerText; 
      if (raw.length > 0) {
        log.appendChild(document.createTextNode(raw));
        this.innerText = ''; 
        log.scrollTop = log.scrollHeight;
      }
    "></div>
  <div id='container'>
    <div id='control'>
      <button id='abort' class="abort-btn"
        data-run  ="false"
        data-sid  ="0"
        hx-target ="#log"
        hx-swap   ="beforeend"
        hx-on::before-request="
          const sid = this.getAttribute('data-sid');
          const na  = this.getAttribute('data-run') == 'false';
          if (na || sid === '0') { event.preventDefault(); return; }
          this.setAttribute('hx-post', `/abort?id=${sid}`);
          htmx.process(this);
        "
        hx-on::response-error="
          const log = document.getElementById('log');
          const sid = this.getAttribute('data-sid');
          const err = event.detail.xhr.statusText;
          log.innerHTML += `<div style='color:red;'>[INTERRUPT] ${err} (Session ${sid})</div>`;
          log.scrollTop = log.scrollHeight;
          this.setAttribute('data-sid', '0');
        ">X
      </button>
    </div>
    <div id='log' 
      hx-on::after-swap="
        if (this.scrollHeight - this.scrollTop - this.clientHeight < 300) this.scrollTop = this.scrollHeight
      ">xeForth v1.0 Initialized...<br/></div>
    <div id='tib-form'>
      <!-- 2. Apply our custom extension directly to the HTML Form layout -->
      <form hx-post='/execute' hx-target='#staging' hx-swap='innerHTML' hx-ext='raw-text-body'
        hx-on::before-request="
          // Back up the textarea code to our hidden landing field before clearing the UI input
          const tibValue = document.getElementById('tib').value;
          document.getElementById('tib-hidden-copy').value = tibValue;
          document.getElementById('tib').value='';
          const sid = Math.floor(Date.now() % 10000);   
          const btn = document.getElementById('abort');
          btn.setAttribute('data-sid', sid);
          btn.setAttribute('data-run', 'true');
        "
        hx-on::after-request="
          const btn = document.getElementById('abort');
          btn.setAttribute('data-run', 'false');
          document.getElementById('tib-hidden-copy').value = '';
          this.reset();
        "
        onsubmit="
          const log=document.getElementById('log'); 
          log.innerHTML += 
            '<div class=\'cmd-entry\'>&gt; ' +
            document.getElementById('tib').value.replace(/\n/g,'<br/>') + 
            '</div>'; 
          log.scrollTop = log.scrollHeight">
        <textarea id='tib' placeholder='Type Forth code here...'
          onkeydown="
            if (event.keyCode===13 && !event.shiftKey) { 
              event.preventDefault(); 
              htmx.trigger(this.form, 'submit'); 
            }"></textarea>
      </form>
    </div>
  </div>
</body>
</html>
)XX";

struct HttpContext {
    httpd_handle_t hd;
    int            fd;
};

static void handle_overflow(void* arg, const char* failed_line) {
    auto* ctx = static_cast<HttpContext*>(arg);
    if (!ctx) return;

    const char* err = "<div style='color:#ffaa00;'>\r\n[SYSTEM ERROR] Pipeline Saturated: Script truncated, engine busy.<div>";
    char hdr[16];
    snprintf(hdr, sizeof(hdr), "%X\r\n", strlen(err));
    
    httpd_handle_t hd = ctx->hd;
    int            fd = ctx->fd;
    
    httpd_socket_send(hd, fd, hdr, strlen(hdr), 0);
    httpd_socket_send(hd, fd, err, strlen(err), 0);
    httpd_socket_send(hd, fd, "\r\n", 2, 0);
}

// Instantiate the single persistent static tracking coordinator instance
static SessionCoordinator gCoordinator(COORDINATOR_ACTOR_GLOBAL_ID);

static esp_err_t handle_abort(httpd_req_t *req) {
    char     buf[32];
    uint32_t sid = 0;
    
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char val[16];
        if (httpd_query_key_value(buf, "id", val, sizeof(val)) == ESP_OK) {
            sid = strtoul(val, nullptr, 10);
        }
    }

    if (sid != 0) {
        ActorMsg x { MSG_FORTH_ABORT, FORTH_ACTOR_GLOBAL_ID, sid };
        Sys.send(x, 0, true);
        
        ActorMsg coord_abort { MSG_FORTH_ABORT, COORDINATOR_ACTOR_GLOBAL_ID, sid };
        Sys.send(coord_abort, 0, true);

        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Execution Aborted");
        return ESP_OK;
    }
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid Session ID");
    return ESP_FAIL;
}

// Explicitly updated to pull streaming data directly across the bus into PSRAM
bool XServer::read_form_psram(httpd_req_t *req, char **out_psram_ptr) {
    int total = req->content_len;
    LOG("xserver2 content len=%d\n", total);
    if (total <= 0 || total >= 8192) { 
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid Payload Length");
        return false;
    }

    // 1. Allocate a flat character buffer directly on the 8 MB Octal PSRAM chip
    char* raw = (char*) heap_caps_malloc(total + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (raw == nullptr) {
        LOG("[ERROR] Failed to allocate memory capsule (%x) in PSRAM!\n", total+1);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of External Memory");
        return false;
    }

    // 2. Stream packets straight out of the LwIP network socket into the PSRAM destination pointer
    int received = 0;
    while (received < total) {
        int r = httpd_req_recv(req, raw + received, total - received);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0) {
            heap_caps_free(raw);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Network Stream Aborted");
            return false;
        }
        received += r;
    }
    
    // 3. Simple null-termination. Pristine RAW plain-text content ready for the Coordinator iterator!
    raw[total] = '\0';
    LOG("xserver2 raw = '%s'\n", raw);

    *out_psram_ptr = raw;
    return true;
}

static esp_err_t handle_execute(httpd_req_t *req) {
    XServer *server = static_cast<XServer*>(req->user_ctx);
    int client_sockfd = httpd_req_to_sockfd(req);
    if (client_sockfd < 0) return ESP_FAIL;

    LOG("xserver2 handle_execute fd=%d\n", client_sockfd);
    // Stream the code directly to external memory
    char* psram_string_buffer = nullptr;
    if (!server->read_form_psram(req, &psram_string_buffer)) {
        return ESP_FAIL;
    }

    uint32_t sid = Sys.alloc_id();
    size_t total_bytes = strlen(psram_string_buffer);

    // Register session metadata into the fixed index matrix
    gCoordinator.register_new_connection(sid, client_sockfd, req->handle, psram_string_buffer, total_bytes);
    
    return ESP_OK;
}

bool XServer::begin(int priority) {
    Sys.register_actor(&gCoordinator);
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
    config.stack_size  = 8192; 

    if (httpd_start(&_httpd, &config) != ESP_OK) return;

    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = [](httpd_req_t *req) {
            httpd_resp_set_type(req, "text/html");
            return httpd_resp_send(req, (char*)HTML_INDEX, HTTPD_RESP_USE_STRLEN);
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
