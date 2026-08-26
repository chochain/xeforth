///
/// @file
/// @brief ESP32-S3 4848S040 LVGL renderer interface
///
#include "xlvgl.h"
#include <esp_heap_caps.h>

// Example callback function required by LVGL to flush compiled frame buffers to the display
void my_disp_flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
    // Look up our custom display class context passed via user_data
    Arduino_RGB_Display* display = (Arduino_RGB_Display*)disp_drv->user_data;
    
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    // Push raw pixel streams via high-speed DMA straight down to the ST7701S panel
    display->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);

    // Inform LVGL that the frame buffer flush is complete
    lv_disp_flush_ready(disp_drv);
}

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

    Serial.println("[LVGLRenderer]: Arduino_GFX drivers active inside Core 1 thread task.");

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
    gfx->begin();
    gfx->fillScreen(BLACK);

    lv_init();
}

void LVGLRenderer::initHardwarePanel() {
    // 1. Initialize the 3-wire SPI Bus used to transmit configuration registers to the ST7701S
    // (Pins vary based on your specific 4848S040 board version - match your working example code)
    _bus = new Arduino_ESP32SPI(
        -1 /* DC */, 39 /* CS */, 48 /* SCK */, 47 /* MOSI */, -1 /* MISO */, VSPI_HOST
    );

    // 2. Configure the sub-pixel high-speed parallel RGB timing blocks
    _rgbpanel = new ESP32RGBPanel(
        18 /* DE */, 17 /* VSYNC */, 16 /* HSYNC */, 21 /* PCLK */,
        4 /* R0 */, 3 /* R1 */, 2 /* R2 */, 1 /* R3 */, 0 /* R4 */,
        10 /* G0 */, 9 /* G1 */, 8 /* G2 */, 7 /* G3 */, 6 /* G4 */, 5 /* G5 */,
        15 /* B0 */, 14 /* B1 */, 13 /* B2 */, 12 /* B3 */, 11 /* B4 */,
        1 /* hsync_polarity */, 10 /* hsync_front_porch */, 8 /* hsync_pulse_width */, 50 /* hsync_back_porch */,
        1 /* vsync_polarity */, 10 /* vsync_front_porch */, 8 /* vsync_pulse_width */, 20 /* vsync_back_porch */
    );

    // 3. Chain components into the main RGB Display wrapper constructor instance
    _display = new Arduino_RGB_Display(
        _screen_width, _screen_height, _rgbpanel, 0 /* RGB rotation step */, 
        true /* auto_flush */, _bus, 38 /* GFX hardware RESET pin pointer line */, 
        st7701_4848s040_init_operations, sizeof(st7701_4848s040_init_operations)
    );

    // Start the display driver
    _display->begin();

    // 4. Initialize Core LVGL framework engine configurations
    lv_init();

    // Allocate frame buffers for LVGL's internal rendering engine (Separate from your Forth Canvas)
    // Allocating in Internal SRAM keeps rendering speeds high, or use PSRAM if memory is tight
    static lv_disp_draw_buf_t draw_buf;
    size_t lv_buf_size = _screen_width * 40 * sizeof(lv_color_t); // 40-row strip buffer
    lv_color_t *buf1   = (lv_color_t *)heap_caps_malloc(lv_buf_size, MALLOC_CAP_INTERNAL);
    lv_disp_draw_buf_init(&draw_buf, buf1, NULL, _screen_width * 40);

    // Register display driver variables to hook LVGL straight to Arduino_GFX
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res   = _screen_width;
    disp_drv.ver_res   = _screen_height;
    disp_drv.flush_cb  = my_disp_flush_cb;
    disp_drv.draw_buf  = &draw_buf;
    
    // Crucial: Pass the _display pointer into user_data so the callback can access it safely
    disp_drv.user_data = (void*)_display; 
    
    lv_disp_drv_register(&disp_drv);

    _ts = new TAMC_GT911(TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST, width, height);

    // activate devices
    pinMode(38, OUTPUT);
    digitalWrite(38, HIGH); // Backlight ON

    Wire.begin(TOUCH_SDA, TOUCH_SCL); 
    ts.begin();
    ts.setRotation(ROTATION_NORMAL);
}



