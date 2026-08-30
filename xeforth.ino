/// -*- mode: c++ -*-
/// @file
/// @brief xeForth implemented for ESP32
///
/*
[ WEB BROWSER ] 
       │ (HTTP POST "forth_code")
       ▼
[ CORE 0: Web Server Task ] (Priority 6)
       │ (Parses string, packs struct, calls xQueueSend)
       ▼ [ webToForthQueue ] 
[ CORE 0: Forth VM Task ] (Priority 5)
       │ (Suspends/Awakes via xQueueReceive, interprets tokens)
       │ (Forth script calls LOGO-LINE primitive word)
       ▼ [ logo_queue ] 
[ CORE 1: LVGL Drawing Task ] (Priority 10)
       │ (Drains queue via xQueueReceive, maps line onto lv_canvas)
       │ (Calls lv_timer_handler to push pixels via DMA)
       ▼
[ 4848S040 IPS DISPLAY PANEL ]
*/
///====================================================================
#include "soc/soc.h"                      /// * for brown out detector
#include "soc/rtc_cntl_reg.h"             /// * RTC control registers
///
///> ESP32 WiFi setup
///
#include "src/esp32/mcu.h"                ///< MCU specific Forth words

const char *WIFI_SSID = "Amitofo_4F_5G";  ///< use your own SSID
const char *WIFI_PASS = "25325754";       ///< and the password
const int   WIFI_PORT = 80;               ///< and the password

// Define structural payload contracts uniformly across your files
// Instantiate Global Message-Routing Pipelines
xQueWeb *webToForthQueue  = NULL;
xQueGL  *forthToLvglQueue = NULL;

// Instantiate the distinct, modular systems with custom parameters
XServer myWebServer(WIFI_SSID, WIFI_PASS, WIFI_PORT);
XForth  myForthEngine(701, 10);
XGL     myUiRenderer(480, 480);

void setup() {
    Serial.begin(115200);

    // 1. Build the non-fragmenting communications pipeline channels
    webToForthQueue  = (xQueWeb*)xQueueCreate(10, sizeof(que_msg_t));
    forthToLvglQueue = (xQueGL* )xQueueCreate(50, sizeof(draw_vec_t));

    if (webToForthQueue == NULL || forthToLvglQueue == NULL) {
        Serial.println("Critical: Failed to generate system pipelines.");
        while(1);
    }
    mcu_init();                     ///> initialize Forth VM
    mem_stat();

    // 2. Deploy Web Server Engine ──> Core 0 (Priority 6)
    myWebServer.begin(webToForthQueue, 6);

    // 3. Deploy Forth VM Interpreter Engine ──> Core 0 (Priority 5)
    myForthEngine.begin(webToForthQueue, forthToLvglQueue, 5);

    // 4. Deploy High-Performance Graphic Canvas Engine ──> Core 1 (Priority 10)
    // We give the UI the highest priority layer to guarantee responsive drawing updates
    myUiRenderer.begin(forthToLvglQueue, 10);

    // 5. Safely delete the empty Arduino loop task to reclaim internal SRAM boundaries
    vTaskDelete(NULL);
}

void loop() {
    // Left empty and uncalled because loopTask is securely deleted
}
