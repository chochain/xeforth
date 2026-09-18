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
#include <Arduino.h>
#include "src/esp32/mcu.h"                ///< MCU specific Forth words

const char *WIFI_SSID = "Amitofo_4F";     ///< use your own SSID
const char *WIFI_PASS = "25325754";       ///< and the password
const int   WIFI_PORT = 80;               ///< and the password

ActorSystem Sys; // Global instantiation assignment
uint32_t ForthActor::_active_session_id = 0;
std::atomic<bool> ForthActor::_abort_requested(false);

XServer    myWebServer(WIFI_SSID, WIFI_PASS, WIFI_PORT);
ForthActor *globalForthActor = nullptr;
XGL        *myUiRenderer     = nullptr;
TimerHandle_t telemetryTimer  = nullptr;

void telemetry_timer_callback(TimerHandle_t xTimer) {
    ActorMsg msg;
    msg.type = MSG_SYS_TELEMETRY;
    msg.target_id = GUI_ACTOR_GLOBAL_ID; 
    msg.memory.free_heap_kb  = ESP.getFreeHeap() / 1024;
    msg.memory.free_psram_kb = ESP.getFreePsram() / 1024;
    Sys.send(msg);
}

void setup() {
    delay(200);
    Serial.begin(115200);
    
    mcu_init(); 

    // 1. Boot up the central conveyor thread pool system on Core 0
    Sys.begin(3, 5);

    // 2. Register the Core 0 Brain Actor (Global ID = 1)
    globalForthActor = new ForthActor(1);
    Sys.register_actor(globalForthActor);

    // 3. Register the Core 1 Graphics Engine Canvas Actor (Global ID = 2)
    myUiRenderer = new XGL(2, 480, 480);
    Sys.register_actor(myUiRenderer);
    myUiRenderer->begin(10); 

    // 4. Initialize Web Services Endpoint Gates on Core 0
    myWebServer.begin(6);

    // 5. Start the Telemetry Pump
    telemetryTimer = xTimerCreate(
        "sys_metric_pump",
        pdMS_TO_TICKS(500),         
        pdTRUE,                     
        nullptr,
        telemetry_timer_callback
    );
    
    if (telemetryTimer != nullptr) {
        xTimerStart(telemetryTimer, 0);
    }

    vTaskDelete(NULL);
}

void loop() {}
