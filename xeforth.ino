/// -*- mode: c++ -*-
/// @file
/// @brief xeForth implemented for ESP32
///
/*
[ WEB BROWSER ] 
  │ (HTTP POST "forth_code")
[ CORE 0: Web Server Task ] (Priority 6)
  │ (Parses string, packs struct, calls xQueueSend)
  ▼ [ webToForthQueue ] 
[ CORE 0: Forth VM Task ] (Priority 5)
  | (xQueueReceive, interprets tokens)
  | (Forth interprets GUI word)                    |
  ▼ [ ui_bridge.snd_q ]                          [ ui_bridge.rcv_q ]
[ CORE 1: LVGL Drawing Task ] (Priority 10)
  | (xQueueReceive, maps line onto lv_canvas)      ^
  | (lv_timer_handler pushs pixels via DMA)        |
  v                                                |
[ 4848S040 IPS DISPLAY PANEL ]                    [TAMC_GT911]
*/
///====================================================================
#include "soc/soc.h"                      /// * for brown out detector
#include "soc/rtc_cntl_reg.h"             /// * RTC control registers
///
///> ESP32 WiFi setup
///
#include "src/esp32/mcu.h"                ///< MCU specific Forth words

//const char *WIFI_SSID = "Amitofo_4F_5G"; ///< use your own SSID
//const char *WIFI_PASS = "25325754";      ///< and the password
const char *WIFI_SSID = "iDafu";           ///< use your own SSID
const char *WIFI_PASS = "AlseTron";       ///< and the password
const int   WIFI_PORT = 80;               ///< and the password

// Define structural payload contracts uniformly across your files
// Instantiate Global Message-Routing Pipelines
xQueWeb *web_bridge = NULL;
xQueUI  *ui_bridge  = NULL;

// Instantiate the distinct, modular systems with custom parameters
XServer myWebServer(WIFI_SSID, WIFI_PASS, WIFI_PORT);
XForth  myForthEngine(701, 10);
XGL     myUiRenderer(480, 480);

void setup() {
    delay(200);                     ///< warm up external devices
    Serial.begin(115200);

    // 1. Build the non-fragmenting communications pipeline channels
    web_bridge = new xQueWeb(10, 50);
    ui_bridge  = new xQueUI(10, 10);

    if (web_bridge == NULL || ui_bridge == NULL) {
        Serial.println("Critical: Failed to generate system pipelines.");
        while(1);
    }
    mcu_init();                     ///> initialize Forth VM
    mem_stat();

    // 2. Deploy Web Server Engine ──> Core 0 (Priority 6)
    myWebServer.begin(web_bridge, 6);

    // 3. Deploy Forth VM Interpreter Engine ──> Core 0 (Priority 5)
    myForthEngine.begin(web_bridge, ui_bridge, 5);

    // 4. Deploy High-Performance Graphic Canvas Engine ──> Core 1 (Priority 10)
    // We give the UI the highest priority layer to guarantee responsive drawing updates
    myUiRenderer.begin(ui_bridge, 10);

    // 5. Safely delete the empty Arduino loop task to reclaim internal SRAM boundaries
    vTaskDelete(NULL);
}

void loop() {
    // Left empty and uncalled because loopTask is securely deleted
}
