/// -*- mode: c++ -*-
/// @file
/// @brief xeForth implemented for ESP32
///
///====================================================================
#include "soc/soc.h"                      /// * for brown out detector
#include "soc/rtc_cntl_reg.h"             /// * RTC control registers
///
///> ESP32 WiFi setup
///
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "src/esp32/mcu_actor.h"          ///< MCU specific Forth words

//const char *WIFI_SSID = "Amitofo_4F";     ///< use your own SSID
//const char *WIFI_PASS = "25325754";       ///< and the password
const char *WIFI_SSID = "iDafu";          ///< use your own SSID
const char *WIFI_PASS = "AlseTron";       ///< and the password
const int   WIFI_PORT = 80;               ///< and the password

#define WORKER_TASK_COUNT 1

ActorSystem Sys; // Global instantiation assignment

uint32_t          ForthActor::_active_sid   = 0;
std::atomic<bool> ForthActor::_abort(false);
uint32_t          ForthActor::_feedback_cnt = 0;

XServer       gWebServer(WIFI_SSID, WIFI_PASS, WIFI_PORT);
ForthActor    gForthActor(FORTH_ACTOR_ID);
XGL           gUiRenderer(GUI_ACTOR_ID, 480, 480);
TimerHandle_t gTimer       = nullptr;

void timer_callback(TimerHandle_t xTimer) {
    ActorMsg msg { MSG_SYS_TELEMETRY, GUI_ACTOR_ID, 0 };
    msg.memory.free_heap_kb  = ESP.getFreeHeap() / 1024;
    msg.memory.free_psram_kb = ESP.getFreePsram() / 1024;
    Sys.send(msg);
}

void setup() {
    delay(200);
    Serial.begin(115200);
    
    // 1. Boot up the central conveyor thread pool system on Core 0 at priority 5
    Sys.begin(WORKER_TASK_COUNT, 5);
    Serial.printf("\n[SYSTEM] xeForth Actor Engine Initializing...\n");

    // 2. Register the Core 0 Forth Actor
    Sys.register_actor(&gForthActor);
    Serial.printf("[SYSTEM] Forth Actor Registered (ID: %d)\n", FORTH_ACTOR_ID);

    // 3. Register the Core 1 Graphics Engine Canvas Actor at priority 10
    Sys.register_actor(&gUiRenderer);
    gUiRenderer.begin(10); 
    Serial.printf("[SYSTEM] GUI Actor Registered (ID: %d)\n", GUI_ACTOR_ID);

    // 4. Start the Web Server Subsystem
    // This internally boots the network link and registers the SessionCoordinator
    Serial.printf("[SYSTEM] WebServer %s.\n", gWebServer.begin(4) ? "started" : "failed");

    mcu_init();
#if 0
    // 5. Start the Telemetry Pump
    gTimer = xTimerCreate(
        "sys_metric_pump",
        pdMS_TO_TICKS(500),
        pdTRUE,                     
        nullptr,
        timer_callback
    );
    if (!gTimer || xTimerStart(gTimer, pdMS_TO_TICKS(50)) != pdPASS) {
        ERR("sys_metric_timer not available");
    }
#endif 
    vTaskDelete(NULL);
}

void loop() {}
