///
/// @file
/// @brief ESP32-S3 4848S040 LVGL renderer interface
///
#include "lvgl.h"
#include <esp_heap_caps.h>

// Structural payload mapping matching your global Plumbing contract
typedef enum {
    VECTOR_CLEAR,
    VECTOR_LINE
} vector_op_t;

typedef struct {
    uint8_t  op_code;
    int16_t  x1;
    int16_t  y1;
    int16_t  x2;
    int16_t  y2;
} vector_draw_packet_t;

bool LVGLRenderer::begin(QueueHandle_t vector_queue, UBaseType_t task_priority) {
    if (vector_queue == NULL) return false;
    _incoming_vector_queue = vector_queue;

    // Launch the background FreeRTOS execution thread pinned strictly to CORE 1
    // We pass "this" into the 4th parameter slot to bridge the class context natively.
    BaseType_t xReturned = xTaskCreatePinnedToCore(
        vTaskRenderBridge,     // Static function bridge pointer
        "LVGL_Render_Task",    // Task string identifier name
        8192,                  // Task stack depth allocation (bytes)
        (void*)this,           // 👈 PASS 'THIS' CONTEXT POINTER HERE
        task_priority,         // High priority layer to prevent frame stutter
        &_task_handle,         // Target task handle tracker
        1                      // Pinned strictly to CORE 1
    );

    return (xReturned == pdPASS);
}

void LVGLRenderer::runRenderLoop() {
    // 1. Fire up your working v8.4 physical panel display driver code
    initHardwarePanel();

    // 2. Allocate the 480x480 true-color frame buffer strictly in External PSRAM
    // 480 * 480 * 2 bytes per pixel (RGB565) = 460.8 KB
    size_t buffer_size = _screen_width * _screen_height * sizeof(lv_color_t);
    _canvas_buffer = (uint8_t*)heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM);
    
    if (_canvas_buffer == NULL) {
        Serial.println("Fatal: Failed to allocate frame canvas buffer in PSRAM!");
        vTaskDelete(NULL);
    }

    // 3. Instantiate the LVGL Canvas widget container
    _canvas_obj = lv_canvas_create(lv_scr_act());
    lv_canvas_set_buffer(_canvas_obj, _canvas_buffer, _screen_width, _screen_height, LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(_canvas_obj, lv_color_black(), LV_OPA_COVER);

    // 4. Initialize your v8.4 drawing descriptors once to prevent setup bloat
    lv_draw_line_dsc_init(&_line_dsc);
    _line_dsc.color = lv_color_make(0, 255, 0); // Neo-Green Logo theme color
    _line_dsc.width = 2;                        // 2-pixel stroke thickness

    Serial.println("[LVGLRenderer Class]: Canvas pipeline running smoothly on Core 1.");

    vector_draw_packet_t incoming_packet;

    while (1) {
        // 5. Drain the entire queue backlog of vector tasks sent from Forth on Core 0
        while (xQueueReceive(_incoming_vector_queue, &incoming_packet, 0) == pdTRUE) {
            
            switch (incoming_packet.op_code) {
                case VECTOR_LINE: {
                    // Map parameters straight to an LVGL v8.4 coordinate array structure
                    lv_point_t points[2] = {
                        { incoming_packet.x1, incoming_packet.y1 },
                        { incoming_packet.x2, incoming_packet.y2 }
                    };
                    
                    // Direct vector drawing call into our isolated canvas object
                    lv_canvas_draw_line(_canvas_obj, points, 2, &_line_dsc);
                    break;
                }
                case VECTOR_CLEAR:
                    lv_canvas_fill_bg(_canvas_obj, lv_color_black(), LV_OPA_COVER);
                    break;
            }
        }

        // 6. Force LVGL to run layout ticks, handle touch states, and pump DMA pixels
        lv_timer_handler();

        // 7. Yield to feed the Core 1 FreeRTOS hardware watchdog timers
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void LVGLRenderer::initHardwarePanel() {
    // Paste your existing, verified 4848S040 ST7701 panel 
    // and touch initialization setup block lines right here.
    // e.g., lv_init(); my_st7701_driver_init();
}
