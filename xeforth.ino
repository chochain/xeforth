/// -*- mode: c++ -*-
/// @file
/// @brief xeForth implemented for ESP32
///
/*
[ WEB BROWSER                ] 
[ CORE 0: Web Server Task    ] (Priority 6)
[ CORE 0: Forth VM Task      ] (priority 5)
[ CORE 1: LVGL Drawing Task  ] (priority 10)
[ ST7701S / TAMC_GT911       ]
*/
///====================================================================
#include "soc/soc.h"                      /// * for brown out detector
#include "soc/rtc_cntl_reg.h"             /// * RTC control registers
///
///> ESP32 WiFi setup
///
#include "src/esp32/mcu.h"                ///< MCU specific Forth words

const char *WIFI_SSID = "Amitofo_4F";     ///< use your own SSID
const char *WIFI_PASS = "25325754";       ///< and the password
const int   WIFI_PORT = 80;               ///< and the password

// Define structural payload contracts uniformly across your files
// Instantiate Global Message-Routing Pipelines
xQueWeb *web_bridge = NULL;
xQueUI  *ui_bridge  = NULL;

// Instantiate the distinct, modular systems with custom parameters
XServer myWebServer(WIFI_SSID, WIFI_PASS, WIFI_PORT);
XGL     myUiRenderer(480, 480);
XForth  myForthEngine(200, 10);

void setup() {
    delay(200);
    Serial.begin(115200);

    // 1. Build the non-fragmenting communications pipeline channels
    web_bridge = new xQueWeb(5, 5);
    ui_bridge  = new xQueUI(5, 5);

    if (web_bridge == NULL || ui_bridge == NULL) {
        Serial.println("Critical: Failed to generate system pipelines.");
        while(1);
    }
    // 2. Deploy Web Server Engine ──> Core 0 (Priority 6)
    myWebServer.begin(web_bridge, 6);

    // 3. Deploy High-Performance Graphic Canvas Engine ──> Core 1 (Priority 10)
    // We give the UI the highest priority layer to guarantee responsive drawing updates
    myUiRenderer.begin(ui_bridge, 10);

    // 4. Deploy Forth VM Interpreter Engine ──> Core 0 (Priority 5)
    mcu_init();                         ///> initialize Forth VM

    myForthEngine.begin(web_bridge, ui_bridge, 5);

    // 5. Safely delete the empty Arduino loop task to reclaim internal SRAM boundaries
    vTaskDelete(NULL);
}

void loop() {
    // Left empty and uncalled because loopTask is securely deleted
}
