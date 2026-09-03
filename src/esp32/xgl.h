#ifndef _XGL_H
#define _XGL_H
#include "xque.h"

#if (ARDUINO || ESP32)
#include <Arduino.h>
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

class XGL {
private:
    uint32_t              _width;
    uint32_t              _height;
    xQueGL                *_gl_q;
    TaskHandle_t          _task;
    
    // 📺 Embedded Arduino_GFX Hardware Display Infrastructure Components
    Arduino_DataBus       *_bus;
    Arduino_ESP32RGBPanel *_panel;
    Arduino_RGB_Display   *_display; // Standard class wrapping RGB panel logic
    TAMC_GT911            *_ts;

    // LVGL internal canvas properties
    lv_disp_draw_buf_t    _draw_buf;
    lv_color_t            *_disp_draw_buf;
    lv_obj_t              *_term_log;
    lv_obj_t              *_chart;
    lv_chart_series_t     *_cpu_series;
    lv_chart_series_t     *_ram_series;

    static void vTaskRenderBridge(void *pv) {
        XGL *gl = (XGL*)pv;
        gl->runRenderLoop();
    }

    void runRenderLoop();
    
    // Internal hardware initialization method
    void initHardwarePanel();
    void parse(char *cmd);
    void term_print(const char *text, lv_color_t textColor);

public:
    XGL(uint32_t width = SCREEN_WIDTH, uint32_t height = SCREEN_HEIGHT) :
        _width(width),
        _height(height),
        _gl_q(NULL),
        _task(NULL),
        _bus(NULL),
        _panel(NULL),
        _display(NULL),
        _ts(NULL),
        _disp_draw_buf(NULL),
        _term_log(NULL),
        _chart(NULL),
        _cpu_series(NULL),
        _ram_series(NULL) {}

    bool begin(xQueGL *gl_q, int priority);
};

#else // !(ARDUINO || ESP32)
#include <thread>
#include <iostream>

class SimulatedLVGL {
private:
    std::thread *_thread;
    xQueGL      *_vec_q;

    void runRenderLoop(void) {
        std::cout << "core1 LVGL> engine loop active." << std::endl;
        draw_vec_t vec;

        while (true) {
            /* Drain all outstanding vector transformations generated from Core 0 */
            while (_vec_q->receive_non_blocking(vec)) {
                if (vec.op_code == VECTOR_LINE) {
                    /* This is where your Linux SDL2/SDL3 canvas plotting routine inserts */
                    std::cout << "🎨 core1 LVGL>: render ("
                              << vec.x1 << "," << vec.y1 << ") to ("
                              << vec.x2 << "," << vec.y2 << ")" << std::endl;
                }
            }

            /* Emulate: lv_timer_handler() execution steps */
            std::this_thread::sleep_for(std::chrono::milliseconds(16)); /* Lock loop target at ~60fps */
        }
    }

public:
    SimulatedLVGL(void) : _thread(NULL), _vec_q(NULL) {}
    
    ~SimulatedLVGL() {
        if (_thread) { delete _thread; }
    }

    bool begin(xQueGL *vec_q, int priority) {
        _vec_q = vec_q;
        _thread = new std::thread(&SimulatedLVGL::runRenderLoop, this);
        _thread->detach();

        return true;
    }
};

#endif // (ARDUINO || ESP32)
#endif // _XGL_H

