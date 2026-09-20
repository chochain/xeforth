///
/// @file
/// @brief ESP32-S3 4848S040 (ST7701S) LVGL Renderer
///
#include "xgl_actor.h"
#include <esp_heap_caps.h>

// 1. Declare the compiled C-array font file asset
LV_FONT_DECLARE(jetbrains_mono_14);

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

#if 0
// Declare a global or static pointer to your LVGL terminal/label widget
extern lv_obj_t* my_lvgl_console_label; 

void my_lv_ui_updater_cb(void * user_data) {
    // 1. Cast the raw pointer back to our fixed structure
    auto* payload = static_cast<lv_ui_update_t*>(user_data);
    
    if (payload && my_lvgl_console_label) {
        // 2. Perform the UI update safely on the main thread
        // For example, appending the Forth output text straight to an LVGL text area or label
        lv_label_ins_text(my_lvgl_console_label, LV_LABEL_POS_LAST, payload->message);
    }
    
    // 3. CRITICAL: Free the structural wrapper block!
    // Since this memory was allocated dynamically just for the trip between threads,
    // we must delete it right here once the UI update completes.
    delete payload; 
}
#endif

// Stable Debounced Touchpad Read Callback
void my_touchpad_read(lv_indev_drv_t *touch_drv, lv_indev_data_t *data) {
    static int last_x = 0;
    static int last_y = 0;
    TAMC_GT911 *ts = (TAMC_GT911*)touch_drv->user_data;
    
    ts->read();
    if (ts->isTouched) {
        int touchX = 480 - ts->points[0].x; 
        int touchY = 480 - ts->points[0].y;

        if (touchX >= 0 && touchX < 480 && touchY >= 0 && touchY < 480) {
            data->state   = LV_INDEV_STATE_PR;
            data->point.x = touchX;
            data->point.y = touchY;

            static uint32_t timer = millis() + 100;
            if (millis() > timer) { 
                ActorMsg touch_msg { MSG_GUI_TOUCH_TRIGGER, FORTH_ACTOR_GLOBAL_ID };
                touch_msg.touch.x = touchX;
                touch_msg.touch.y = touchY;
                touch_msg.touch.state = 1;
                Sys.send(touch_msg);
                timer += 100;
            }
        }
    }
    else {
        data->state = LV_INDEV_STATE_REL;
    }
}

void XGL::receive(const ActorMsg &msg) {
    // ️Apply Backpressure: Block the calling Core 0 worker task if Core 1 is saturated
    if (xQueueSend(_mailbox, &msg, 0) != pdTRUE) {
        ERR("[SYSTEM WARNING] XGL Mailbox full, msg dropped.");
    }
}

bool XGL::begin(int priority) {
    BaseType_t xReturned = xTaskCreatePinnedToCore(
        [](void *pv) { static_cast<XGL*>(pv)->run(); },
        "LVGL_Render_Task",
        8192,
        (void*)this,
        priority,
        &_task,
        1                  // Pinned strictly to Core 1
    );
    return (xReturned == pdPASS);
}

// Thread-safe terminal stream printer
void XGL::term_print(const char *txt, lv_color_t textColor) {
    // Append text to terminal object canvas
    lv_textarea_add_text(_term_log, txt);
    
    // Auto-scroll logic: lock view frame to bottom lines
    uint32_t txt_len = strlen(lv_textarea_get_text(_term_log));
    lv_textarea_set_cursor_pos(_term_log, txt_len);
}

void XGL::process_mailbox() {
    ActorMsg req;
    while (xQueueReceive(_mailbox, &req, 0) == pdTRUE) {
        DEBUG("    xgl << type=%d, '%s'\n", req.type, req.buf ? (char*)req.buf : (char*)"NA");
        switch (req.type) {
        case MSG_GUI_DRAW_CMD:
            term_print(req.buf, lv_color_make(0, 255, 255));
            break;
        case MSG_SYS_TELEMETRY: {
#if 0            
            char fmt_buf[32];
            if (_sram_label) {
                snprintf(fmt_buf, sizeof(fmt_buf), "SRAM: %d KB", req.memory.free_heap_kb);
                lv_label_set_text(_sram_label, fmt_buf);
            }
            if (_psram_label) {
                snprintf(fmt_buf, sizeof(fmt_buf), "PSRAM: %d KB", req.memory.free_psram_kb);
                lv_label_set_text(_psram_label, fmt_buf);
            }
#endif
        } break;
        default:
            LOG("unknown req.type=%d\n", req.type);
            break;
        }
    }
}

void XGL::update_chart() {
    static uint32_t last_tick = 0;
    static uint32_t live_cpu  = random(15, 65); // Replace with your real runtime metrics
    static uint32_t live_ram  = random(5, 95);  // map(ESP.getFreeHeap(), 0, 280000, 100, 0); // Inverse map to get usage percentage

    // 2. LIVE TELEMETRY LOG DATA MODULATION (Updates every 50ms)
    if ((millis() - last_tick) < 50) return;   // 100ms=33%, 50ms=>55%, 20ms=>75% CPU (core1)
    
    last_tick = millis();
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

void XGL::run() {
    // 1. Fire up your working v8.4 physical panel display driver code
    init_hardware();

    while (1) {
        // 6. Force LVGL to run layout ticks, handle touch states, and pump DMA pixels
        lv_timer_handler();
        
        process_mailbox();
        update_chart();
        
        // 7. Yield to feed the Core 1 FreeRTOS hardware watchdog timers
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void XGL::init_hardware() {
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
    size_t     raw_sz = buf_sz * sizeof(lv_color_t);
    _disp_draw_buf    = (lv_color_t*)ps_malloc(raw_sz);
    
    if (_disp_draw_buf == NULL) {
        ERR("Fatal: Failed to allocate frame canvas buffer in PSRAM!");
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
    lv_disp_t *reg_disp = lv_disp_drv_register(&disp_drv);

    // 👇 FIX #2 ADDED: Formally instantiate and register the Touch Input Driver into LVGL
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    indev_drv.user_data = (void*)_ts; // Pass touch object straight to the callback
    lv_indev_drv_register(&indev_drv);
    
    // capture active screen (when multi-threading)
    lv_obj_t  *act_scr  = lv_disp_get_scr_act(reg_disp); 
    lv_obj_set_style_bg_color(act_scr, lv_color_make(10, 12, 16), 0);  // Deep Dark Industrial Styling Matrix
    
    // ==================== PANEL 1: SYSTEM METRICS CHART (Top) ====================
    _chart = lv_chart_create(act_scr);
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
    _term_log = lv_textarea_create(act_scr);
    lv_obj_set_size(_term_log, _width - 20, 290);
    lv_obj_align(_term_log, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_text_font(_term_log, &jetbrains_mono_14, 0);           /// set monospace font
    
    // Force a classic retro-monospaced terminal color layout
    lv_obj_set_style_bg_color(_term_log, lv_color_make(5, 6, 8), 0);
    lv_obj_set_style_text_color(_term_log, lv_color_make(50, 255, 100), 0); // Terminal Green
    lv_obj_set_style_border_color(_term_log, lv_color_make(35, 40, 50), 0);
    
    // Hide standard cursor adjustments to prevent user selection interference
    lv_textarea_set_cursor_click_pos(_term_log, false);
    
    // Add Boot Greetings Text String
    lv_textarea_set_text(_term_log, "xeForth Initialized.\n\n");
    
    LOG("core1 XGL> active buf_sz=%d (bytes).\n", raw_sz);
}



