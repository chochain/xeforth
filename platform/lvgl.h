#ifndef LVGL_RENDERER_H
#define LVGL_RENDERER_H

#include <Arduino.h>
#include <lvgl.h>

class LVGLRenderer {
private:
    uint32_t      _screen_width;
    uint32_t      _screen_height;
    QueueHandle_t _incoming_vector_queue;
    TaskHandle_t  _task_handle;
    
    // LVGL drawing resources
    lv_obj_t*     _canvas_obj;
    uint8_t*      _canvas_buffer;
    lv_draw_line_dsc_t _line_dsc;

    // 🚨 FreeRTOS tasks inside classes MUST be declared as "static void"
    static void vTaskRenderBridge(void *pvParameters) {
        // Cast the generic void pointer directly back into a class instance context
        LVGLRenderer* instance = (LVGLRenderer*)pvParameters;
        instance->runRenderLoop();
    }

    // This internal worker function handles the actual execution logic on Core 1
    void runRenderLoop();
    
    // Internal hardware initializer for the 4848S040 screen panel
    void initHardwarePanel();

public:
    LVGLRenderer(uint32_t width = 480, uint32_t height = 480) :
        _screen_width(width),
        _screen_height(height),
        _incoming_vector_queue(NULL),
        _task_handle(NULL),
        _canvas_obj(NULL),
        _canvas_buffer(NULL) {}

    // Allocates memory layers and spins up the FreeRTOS rendering thread on Core 1
    bool begin(QueueHandle_t vector_queue, UBaseType_t task_priority);
};

#endif

