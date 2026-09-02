///
/// @file
/// @brief ESP32-S3 4848S040 LVGL renderer interface
///
#include "xgl.h"
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

//bool XGL::begin(xQueGL *vec_q, int priority) {
bool XGL::begin(xQueWeb *vec_q, int priority) {
    if (vec_q == NULL) return false;
    _vec_q = vec_q;

    // Launch the background FreeRTOS execution thread pinned strictly to CORE 1
    // We pass "this" into the 4th parameter slot to bridge the class context natively.
    BaseType_t xReturned = xTaskCreatePinnedToCore(
        vTaskRenderBridge,     // Static function bridge pointer
        "LVGL_Render_Task",    // Task string identifier name
        8192,                  // Task stack depth allocation (bytes)
        (void*)this,           // 👈 PASS 'THIS' CONTEXT POINTER HERE
        priority,              // High priority layer to prevent frame stutter
        &_task,                // Target task handle tracker
        1                      // Pinned strictly to CORE 1
    );
    return (xReturned == pdPASS);
}

void XGL::runRenderLoop() {
    // 1. Fire up your working v8.4 physical panel display driver code
    initHardwarePanel();

    // 2. Allocate the 480x480 true-color frame buffer strictly in External PSRAM
    // 480 * 480 * 2 bytes per pixel (RGB565) = 460.8 KB
    size_t buf_sz = _width * _height * sizeof(lv_color_t);
    _canvas_buf = (uint8_t*)heap_caps_malloc(buf_sz, MALLOC_CAP_SPIRAM);
    
    if (_canvas_buf == NULL) {
        Serial.println("Fatal: Failed to allocate frame canvas buffer in PSRAM!");
        vTaskDelete(NULL);
    }

    // 3. Instantiate the LVGL Canvas widget container
    _canvas_obj = lv_canvas_create(lv_scr_act());
    lv_canvas_set_buffer(_canvas_obj, _canvas_buf, _width, _height, LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(_canvas_obj, lv_color_black(), LV_OPA_COVER);

    // 4. Initialize your v8.4 drawing descriptors once to prevent setup bloat
    lv_draw_line_dsc_init(&_line_dsc);
    _line_dsc.color = lv_color_make(0, 255, 0); // Neo-Green Logo theme color
    _line_dsc.width = 2;                        // 2-pixel stroke thickness

    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_make(10, 12, 16), 0);
    
    // ==================== PANEL 2: SCROLLING TERMINAL CANVAS (Bottom) ====================
    _term_log = lv_textarea_create(lv_scr_act());
    lv_obj_set_size(_term_log, _width - 20, _height - 20);
    lv_obj_align(_term_log, LV_ALIGN_BOTTOM_MID, 0, -10);
    
    // Force a classic retro-monospaced terminal color layout
    lv_obj_set_style_bg_color(_term_log, lv_color_make(5, 6, 8), 0);
    lv_obj_set_style_text_color(_term_log, lv_color_make(50, 255, 100), 0); // Terminal Green
    lv_obj_set_style_border_color(_term_log, lv_color_make(35, 40, 50), 0);
    
    // Hide standard cursor adjustments to prevent user selection interference
    lv_textarea_set_cursor_click_pos(_term_log, false);
    
    // Add Boot Greetings Text String
    lv_textarea_set_text(_term_log, "xeForth v1.0 Initialized.\nListening for Stream Packets...\nType 'help' via Serial line.\n\n");
    
    Serial.println("core1 XGL> Arduino_GFX drivers active.");
    
//    draw_vec_t vec;
    que_msg_t  msg;

    while (1) {
        // 5. Drain the entire queue backlog of vector tasks sent from Forth on Core 0
//        while (xQueueReceive((QueueHandle_t)_vec_q, &vec, 0) == pdTRUE) {
        while (xQueueReceive((QueueHandle_t)_vec_q, &msg, portMAX_DELAY) == pdTRUE) {
            Serial.printf("xgl >> %s\n", msg.buf);
            
#if 0
            switch (vec.op_code) {
                case VECTOR_LINE: {
                    // Map parameters straight to an LVGL v8.4 coordinate array structure
                    lv_point_t pts[2] = {
                        { vec.x1, vec.y1 },
                        { vec.x2, vec.y2 }
                    };
                    lv_canvas_draw_line(_term_log, pts, 2, &_line_dsc);

                    // Direct vector drawing call into our isolated canvas object
                    lv_textarea_add_text(_term_log, "hit here");
    
                    // Auto-scroll logic: lock view frame to bottom lines
                    uint32_t txt_len = strlen(lv_textarea_get_text(_term_log));
                    lv_textarea_set_cursor_pos(_term_log, txt_len);
                    break;
                }
                case VECTOR_CLEAR:
//                    lv_canvas_fill_bg(_canvas_obj, lv_color_black(), LV_OPA_COVER);
                    lv_textarea_set_text(_term_log, "");
                    break;
            }
#endif
        }
        // 6. Force LVGL to run layout ticks, handle touch states, and pump DMA pixels
        lv_timer_handler();

        // 7. Yield to feed the Core 1 FreeRTOS hardware watchdog timers
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void XGL::initHardwarePanel() {
    // 1. Initialize the 3-wire SPI Bus used to transmit configuration registers to the ST7701S
    // (Pins vary based on your specific 4848S040 board version - match your working example code)
    _bus = new Arduino_ESP32SPI(
        GFX_NOT_DEFINED /* DC */, 39 /* CS */, 48 /* SCK */, 47 /* MOSI */, GFX_NOT_DEFINED /* MISO */);

    // 2. Configure the sub-pixel high-speed parallel RGB timing blocks
    _panel = new Arduino_ESP32RGBPanel(
        18 /* DE */, 17 /* VSYNC */, 16 /* HSYNC */, 21 /* PCLK */,
        11 /* R0 */, 12 /* R1 */, 13 /* R2 */, 14 /* R3 */, 0  /* R4 */,
        8  /* G0 */, 20 /* G1 */, 3  /* G2 */, 46 /* G3 */, 9  /* G4 */, 10 /* G5 */,
        4  /* B0 */, 5  /* B1 */, 6  /* B2 */, 7  /* B3 */, 15 /* B4 */,
        1  /* hsync_polarity */, 10 /* hsync_front_porch */, 8  /* hsync_pulse_width */, 50 /* hsync_back_porch */,
        1  /* vsync_polarity */, 10 /* vsync_front_porch */, 8  /* vsync_pulse_width */, 20 /* vsync_back_porch */,
        1  /* pclk_active_neg */,   // read from falling edge, a little more breathing room
        9000000 /* pixel clock */   // <-- CRUCIAL FIX: Forcibly drops the clock to 9MHz (from 18MHz) to free up PSRAM bus bandwidth!
    );

    // 3. Chain components into the main RGB Display wrapper constructor instance
    _display = new Arduino_RGB_Display(
        _width, _height, _panel, 0 /* RGB rotation step */, 
        true /* auto_flush */, _bus, -1 /* GFX hardware RESET pin pointer line */, 
        st7701_type9_init_operations, sizeof(st7701_type9_init_operations)
    );

    // 4. Touch screen driver
    _ts = new TAMC_GT911(TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST, _width, _height);

    // 4. Initialize Core LVGL framework engine configurations
    lv_init();

    // Allocate frame buffers for LVGL's internal rendering engine (Separate from your Forth Canvas)
    // Allocating in Internal SRAM keeps rendering speeds high, or use PSRAM if memory is tight
    // Allocate a high-speed 40-line rendering slice block inside internal PSRAM memory
    uint32_t           buf_sz = _width * 40;       // 40-row strip buffer
    lv_color_t         *buf1  = (lv_color_t *)ps_malloc(buf_sz * sizeof(lv_color_t));
    if (buf1==NULL) while(1);
    
    lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, buf1, NULL, buf_sz);

    // Register display driver variables to hook LVGL straight to Arduino_GFX
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res   = _width;
    disp_drv.ver_res   = _height;
    disp_drv.flush_cb  = my_disp_flush_cb;
    disp_drv.draw_buf  = &draw_buf;
    
    // Crucial: Pass the _display pointer into user_data so the callback can access it safely
    disp_drv.user_data = (void*)_display; 
    
    lv_disp_drv_register(&disp_drv);

    // activate display and touch panel
    pinMode(38, OUTPUT);
    digitalWrite(38, HIGH);               // Backlight ON
    
    _display->begin();
    _display->fillScreen(BLACK);

    Wire.begin(TOUCH_SDA, TOUCH_SCL); 
    _ts->begin();
    _ts->setRotation(ROTATION_NORMAL);
}



