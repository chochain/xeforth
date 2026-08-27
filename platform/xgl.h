#ifndef LVGL_RENDERER_H
#define LVGL_RENDERER_H
#include "xque.h"

#if (ARDUINO || ESP32)
#include <Arduino.h>
#include <lvgl.h>
#include <Arduino_GFX_Library.h> // Include your standard GFX header
#include <TAMC_GT911.h>
#include <Wire.h>

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
        _ts(NULL),
        _canvas_obj(NULL),
        _canvas_buffer(NULL) {}

    bool begin(QueueHandle_t vector_queue, UBaseType_t task_priority);
};

#else // !(ARDUINO || ESP32)
#include <thread>
#include <iostream>

class SimulatedLVGL {
private:
    std::thread *_render_thread;
    XQueue<vector_draw_packet_t> *_vector_queue;

    void runRenderLoop(void) {
        std::cout << "core1 LVGL> engine loop active." << std::endl;
        vector_draw_packet_t incoming_draw;

        while (true) {
            /* Drain all outstanding vector transformations generated from Core 0 */
            while (_vector_queue->receive_non_blocking(incoming_draw)) {
                if (incoming_draw.op_code == VECTOR_LINE) {
                    /* This is where your Linux SDL2/SDL3 canvas plotting routine inserts */
                    std::cout << "🎨 core1 LVGL>: render ("
                              << incoming_draw.x1 << "," << incoming_draw.y1 << ") to ("
                              << incoming_draw.x2 << "," << incoming_draw.y2 << ")" << std::endl;
                }
            }

            /* Emulate: lv_timer_handler() execution steps */
            std::this_thread::sleep_for(std::chrono::milliseconds(16)); /* Lock loop target at ~60fps */
        }
    }

public:
    SimulatedLVGL(void) : _render_thread(NULL), _vector_queue(NULL) {}
    
    ~SimulatedLVGL() {
        if (_render_thread) { delete _render_thread; }
    }

    void begin(XQueue<vector_draw_packet_t> *vec_q) {
        _vector_queue = vec_q;
        _render_thread = new std::thread(&SimulatedLVGL::runRenderLoop, this);
        _render_thread->detach();
    }
};

#endif // (ARDUINO || ESP32)
#endif // LVGL_RENDERER_H

