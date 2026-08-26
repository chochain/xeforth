#ifndef LVGL_RENDERER_H
#define LVGL_RENDERER_H

#include <Arduino.h>
#include <xlvgl.h>
#include <Arduino_GFX_Library.h> // Include your standard GFX header
#include <TAMC_GT911.h>
#include <Wire.h>
#include <lvgl.h>

#define SCREEN_WIDTH  480
#define SCREEN_HEIGHT 480

#define TOUCH_SDA  19
#define TOUCH_SCL  45  
#define TOUCH_INT  4     // B0, might blip on start up
#define TOUCH_RST  5     // B1

class LVGLRenderer {
private:
    uint32_t             _screen_width;
    uint32_t             _screen_height;
    QueueHandle_t        _incoming_vector_queue;
    TaskHandle_t         _task_handle;
    
    // 📺 Embedded Arduino_GFX Hardware Display Infrastructure Components
    Arduino_DataBus      *_bus;
    ESP32RGBPanel        *_rgbpanel;
    Arduino_RGB_Display  *_display; // Standard class wrapping RGB panel logic
    TAMC_GT911           *_ts;

    // LVGL internal canvas properties
    lv_obj_t             *_canvas_obj;
    uint8_t              *_canvas_buffer;
    lv_draw_line_dsc_t   _line_dsc;

    static void vTaskRenderBridge(void *pvParameters) {
        LVGLRenderer* instance = (LVGLRenderer*)pvParameters;
        instance->runRenderLoop();
    }

    void runRenderLoop();
    
    // Internal hardware initialization method
    void initHardwarePanel();

public:
    LVGLRenderer(uint32_t width = SCREEN_WIDTH, uint32_t height = SCREEN_HEIGHT) :
        _screen_width(width),
        _screen_height(height),
        _incoming_vector_queue(NULL),
        _task_handle(NULL),
        _bus(NULL),
        _rgbpanel(NULL),
        _display(NULL),
        _ts(
        _canvas_obj(NULL),
        _canvas_buffer(NULL) {}

    bool begin(QueueHandle_t vector_queue, UBaseType_t task_priority);
};

#endif // LVGL_RENDERER_H

