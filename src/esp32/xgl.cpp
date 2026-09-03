///
/// @file
/// @brief ESP32-S3 4848S040 LVGL renderer interface
///
#include "xgl.h"
#include <esp_heap_caps.h>

// Example callback function required by LVGL to flush compiled frame buffers to the display
void my_disp_flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
    // Look up our custom display class context passed via user_data
    Arduino_RGB_Display *display = (Arduino_RGB_Display*)disp_drv->user_data;
    
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    // Push raw pixel streams via high-speed DMA straight down to the ST7701S panel
    display->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);

    // Inform LVGL that the frame buffer flush is complete
    lv_disp_flush_ready(disp_drv);
}

// Stable Debounced Touchpad Read Callback
void my_touchpad_read(lv_indev_drv_t *touch_drv, lv_indev_data_t *data) {
    static int last_x = 0;
    static int last_y = 0;
    static uint32_t last_touch_time = 0;
    TAMC_GT911 *ts = (TAMC_GT911*)touch_drv->user_data;
    
    ts->read();
    if (ts->isTouched) {
        int touchX = 480 - ts->points[0].x; 
        int touchY = 480 - ts->points[0].y;

        if (touchX >= 0 && touchX < 480 && touchY >= 0 && touchY < 480) {
            if (millis() - last_touch_time < 30 && abs(touchX - last_x) < 3 && abs(touchY - last_y) < 3) {
                data->state   = LV_INDEV_STATE_PR;
                data->point.x = last_x;
                data->point.y = last_y;
                return;
            }
            last_x = touchX;
            last_y = touchY;
            last_touch_time = millis();

            data->state   = LV_INDEV_STATE_PR;
            data->point.x = touchX;
            data->point.y = touchY;
        }
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

//bool XGL::begin(xQueGL *vec_q, int priority) {
bool XGL::begin(xQueWeb *gl_q, int priority) {
    if (gl_q == NULL) return false;
    _gl_q = gl_q;

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

// Thread-safe terminal stream printer
void XGL::print(const char *text, lv_color_t textColor) {
    // Append text to terminal object canvas
    lv_textarea_add_text(_term_log, text);
    
    // Auto-scroll logic: lock view frame to bottom lines
    uint32_t txt_len = strlen(lv_textarea_get_text(_term_log));
    lv_textarea_set_cursor_pos(_term_log, txt_len);
}

void XGL::parse(char *cmd) {
    // --- YOUR PROCESSING HUB (Serial / Forth / MQTT Interface) ---
    if (strcmp(cmd, "help")==0) {
        print("Guition Shell Commands:\n - CLEAR : Wipe Log Screen\n - STATS : Dump Device Telemetry\n", lv_color_white());
    } 
    else if (strcmp(cmd, "clear")==0) {
        lv_textarea_set_text(_term_log, "");
    } 
    else if (strcmp(cmd, "stats")==0) {
        char stats_buf[64];
        sprintf(stats_buf, "Free Heap: %d KB | Free PSRAM: %d KB\n", 
                ESP.getFreeHeap() / 1024, ESP.getFreePsram() / 1024);
        print(stats_buf, lv_color_make(0, 255, 0));
    } 
    else {
        // Mock output template for your Forth engine
        print("Executing... ok\n", lv_color_make(150, 150, 150));
    }
}

void XGL::runRenderLoop() {
    // 1. Fire up your working v8.4 physical panel display driver code
    initHardwarePanel();

    msg_gl_t  msg;
    while (1) {
        // 5. Drain the entire queue backlog of vector tasks sent from Forth on Core 0
        while (xQueueReceive((QueueHandle_t)_gl_q, &msg, portMAX_DELAY) == pdTRUE) {
            Serial.printf("xgl >> %s\n", msg.buf);
            
            switch (msg.op_code) {
                case VECTOR_LINE: {
#if 0                    
                    // Map parameters straight to an LVGL v8.4 coordinate array structure
                    lv_point_t pts[2] = {
                        { vec.x1, vec.y1 },
                        { vec.x2, vec.y2 }
                    };
                    // Direct vector drawing call into our isolated canvas object
                    lv_textarea_add_text(_term_log, "hit here");
    
                    // Auto-scroll logic: lock view frame to bottom lines
                    uint32_t txt_len = strlen(lv_textarea_get_text(_term_log));
                    lv_textarea_set_cursor_pos(_term_log, txt_len);
#endif
                    print(msg.buf, lv_color_make(0, 255, 255));
                    break;
                }
                case VECTOR_CLEAR:
                    print("clear", lv_color_make(255, 0, 0));
                    break;
            }
        }
        static uint32_t last_tick = 0;
        static uint32_t live_cpu  = random(15, 65); // Replace with your real runtime metrics
        static uint32_t live_ram  = random(5, 95);  // map(ESP.getFreeHeap(), 0, 280000, 100, 0); // Inverse map to get usage percentage
        // 6. Force LVGL to run layout ticks, handle touch states, and pump DMA pixels
        lv_timer_handler();

        // 2. LIVE TELEMETRY LOG DATA MODULATION (Updates every 500ms)
        if ((last_tick=millis()) - last_tick > 50) {
            // Shift existing values backward
            for (int i = 0; i < 29; i++) {
                _cpu_series->y_points[i] = _cpu_series->y_points[i + 1];
                _ram_series->y_points[i] = _ram_series->y_points[i + 1];
            }

            // Fetch actual hardware configurations dynamically
            live_cpu = (uint32_t)(0.8 * live_cpu + 0.2 * random(15, 65)); // Replace with your real runtime metrics
            live_ram = (uint32_t)(0.8 * live_ram + 0.2 * random(5, 95));  // map(ESP.getFreeHeap(), 0, 280000, 100, 0); // Inverse map to get usage percentage

            lv_chart_set_value_by_id(_chart, _cpu_series, 29, live_cpu);
            lv_chart_set_value_by_id(_chart, _ram_series, 29, live_ram);
            lv_chart_refresh(_chart);
        }
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

    // activate display and touch panel
    pinMode(38, OUTPUT);
    digitalWrite(38, HIGH);               // Backlight ON
    
    _display->begin();
    _display->fillScreen(BLACK);
    
    Wire.begin(TOUCH_SDA, TOUCH_SCL); 
    _ts->begin();
    _ts->setRotation(ROTATION_NORMAL);
    
    // 4. Initialize Core LVGL framework engine configurations
    lv_init();

    // Allocate frame buffers for LVGL's internal rendering engine (Separate from your Forth Canvas)
    // Allocating in Internal SRAM keeps rendering speeds high, or use PSRAM if memory is tight
    // Allocate a high-speed 40-line rendering slice block inside internal PSRAM memory
    // 2. Allocate the 480x40 true-color frame buffer strictly in External PSRAM
    size_t     buf_sz = _width * 40;
    _disp_draw_buf = (lv_color_t *)ps_malloc(buf_sz * sizeof(lv_color_t));
    
    if (_disp_draw_buf == NULL) {
        Serial.println("Fatal: Failed to allocate frame canvas buffer in PSRAM!");
        vTaskDelete(NULL);
    }
    lv_disp_draw_buf_init(&_draw_buf, _disp_draw_buf, NULL, buf_sz);

    // 3. Instantiate the LVGL Canvas widget container
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res   = _width;
    disp_drv.ver_res   = _height;
    disp_drv.flush_cb  = my_disp_flush_cb;
    disp_drv.draw_buf  = &_draw_buf;
    // Crucial: Pass the _display pointer into user_data so the callback can access it safely
    disp_drv.user_data = (void*)_display; 
    lv_disp_drv_register(&disp_drv);

    // Deep Dark Industrial Styling Matrix
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_make(10, 12, 16), 0);
    
    // ==================== PANEL 1: SYSTEM METRICS CHART (Top) ====================
    _chart = lv_chart_create(lv_scr_act());
    lv_obj_set_size(_chart, _width - 20, 150);
    lv_obj_align(_chart, LV_ALIGN_TOP_MID, 0, 10);
    lv_chart_set_type(_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(_chart, 30);
    lv_chart_set_div_line_count(_chart, 4, 6);
    
    // Style the visualization grid
    lv_obj_set_style_bg_color(_chart, lv_color_make(18, 20, 26), 0);
    lv_obj_set_style_border_color(_chart, lv_color_make(35, 40, 50), 0);
    
    _cpu_series = lv_chart_add_series(_chart, lv_color_make(0, 255, 120), LV_CHART_AXIS_PRIMARY_Y);  // Emerald Cyan
    _ram_series = lv_chart_add_series(_chart, lv_color_make(255, 50, 100), LV_CHART_AXIS_PRIMARY_Y); // Magenta

    // Pre-populate chart values
    for(int i = 0; i < 30; i++) {
        lv_chart_set_next_value(_chart, _cpu_series, 20);
        lv_chart_set_next_value(_chart, _ram_series, 45);
    }
    
    // ==================== PANEL 2: SCROLLING TERMINAL CANVAS (Bottom) ====================
    _term_log = lv_textarea_create(lv_scr_act());
    lv_obj_set_size(_term_log, _width - 20, 290);
    lv_obj_align(_term_log, LV_ALIGN_BOTTOM_MID, 0, -10);
    
    // Force a classic retro-monospaced terminal color layout
    lv_obj_set_style_bg_color(_term_log, lv_color_make(5, 6, 8), 0);
    lv_obj_set_style_text_color(_term_log, lv_color_make(50, 255, 100), 0); // Terminal Green
    lv_obj_set_style_border_color(_term_log, lv_color_make(35, 40, 50), 0);
    
    // Hide standard cursor adjustments to prevent user selection interference
    lv_textarea_set_cursor_click_pos(_term_log, false);
    
    // Add Boot Greetings Text String
    lv_textarea_set_text(_term_log, "xeForth Initialized.\n\n");
    
    Serial.println("core1 XGL> active.");
}



