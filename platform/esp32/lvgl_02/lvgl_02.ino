/** -*- mode: c++ -*-
 * Guition 4848S040 v0.2 GT911 Touch Example
 * Required Libraries:
 *   - Arduino_GFX_Library
 *   - TAMC_GT911
 *   - LVGL
 */
#include <Arduino_GFX_Library.h>
#include <TAMC_GT911.h>
#include <Wire.h>
#include <lvgl.h>
//#include "esp_log.h"

#define SCREEN_WIDTH  480
#define SCREEN_HEIGHT 480

// 1. HARDWARE SPI CONTROL BUS
Arduino_DataBus *bus = new Arduino_SWSPI(GFX_NOT_DEFINED, 39, 48, 47, GFX_NOT_DEFINED);

// 2. HARDWARE RGB PANEL CONFIGURATION WITH SLOWED-DOWN CLOCK SPEED (Arg 21)
Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    18 /* DE */, 17 /* VSYNC */, 16 /* HSYNC */, 21 /* PCLK */,
    11 /* R0 */, 12 /* R1 */, 13 /* R2 */, 14 /* R3 */, 0  /* R4 */,
    8  /* G0 */, 20 /* G1 */, 3  /* G2 */, 46 /* G3 */, 9  /* G4 */, 10 /* G5 */,
    4  /* B0 */, 5  /* B1 */, 6  /* B2 */, 7  /* B3 */, 15 /* B4 */,
    1  /* hsync_polarity */, 10 /* hsync_front_porch */, 8  /* hsync_pulse_width */, 50 /* hsync_back_porch */,
    1  /* vsync_polarity */, 10 /* vsync_front_porch */, 8  /* vsync_pulse_width */, 20 /* vsync_back_porch */,
    1  /* pclk_active_neg */,   // read from falling edge, a little more breathing room
    9000000 /* pixel clock */   // <-- CRUCIAL FIX: Forcibly drops the clock to 9MHz (from 18MHz) to free up PSRAM bus bandwidth!
);

// auto_flush is true; we let GFX handle the physical transmission timing loops natively
Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
    SCREEN_WIDTH, SCREEN_HEIGHT, rgbpanel, 0, true, bus, -1, 
    st7701_type9_init_operations, sizeof(st7701_type9_init_operations)
);

#define TOUCH_SDA  19
#define TOUCH_SCL  45  
#define TOUCH_INT  4     // B0, might blip on start up
#define TOUCH_RST  5     // B1
TAMC_GT911 ts = TAMC_GT911(TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST, SCREEN_WIDTH, SCREEN_HEIGHT);

static lv_disp_draw_buf_t draw_buf;
static lv_color_t *disp_draw_buf = NULL;

static lv_obj_t *chart;
static lv_chart_series_t *ser1;
static lv_chart_series_t *ser2;
static lv_obj_t *arc_label;

// 3. CLEAN DIRECT RENDER CONSOLE GATEWAY
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    
    // Copy the single rendering slice to display memory
    gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
    lv_disp_flush_ready(disp);
}

// Stable Debounced Touchpad Read Callback
void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
    static int last_x = 0;
    static int last_y = 0;
    static uint32_t last_touch_time = 0;
    
    ts.read();

    if (ts.isTouched) {
        int touchX = SCREEN_WIDTH - ts.points[0].x; 
        int touchY = SCREEN_HEIGHT - ts.points[0].y;

        if (touchX >= 0 && touchX < SCREEN_WIDTH && touchY >= 0 && touchY < SCREEN_HEIGHT) {
            if (millis() - last_touch_time < 30 && abs(touchX - last_x) < 3 && abs(touchY - last_y) < 3) {
                data->state = LV_INDEV_STATE_PR;
                data->point.x = last_x;
                data->point.y = last_y;
                return;
            }
            last_x = touchX;
            last_y = touchY;
            last_touch_time = millis();

            data->state = LV_INDEV_STATE_PR;
            data->point.x = touchX;
            data->point.y = touchY;
        }
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

static void arc_event_cb(lv_event_t * e) {
    lv_obj_t * arc = lv_event_get_target(e);
    int32_t value = lv_arc_get_value(arc);
    lv_label_set_text_fmt(arc_label, "%d%%", value);
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("--- 9MHZ SLOW-CLOCK LOW-BANDWIDTH BOOT ---");

    pinMode(38, OUTPUT);
    digitalWrite(38, HIGH); // Backlight ON

    gfx->begin();
    gfx->fillScreen(BLACK);

    Wire.begin(TOUCH_SDA, TOUCH_SCL); 
    ts.begin();
    ts.setRotation(ROTATION_NORMAL);

    lv_init();
    
    // Allocate a high-speed 40-line rendering slice block inside internal PSRAM memory
    uint32_t buffer_pixel_size = SCREEN_WIDTH * 40;
    disp_draw_buf = (lv_color_t *)ps_malloc(buffer_pixel_size * sizeof(lv_color_t));
    if (disp_draw_buf == NULL) while(1);
    
    lv_disp_draw_buf_init(&draw_buf, disp_draw_buf, NULL, buffer_pixel_size);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = SCREEN_WIDTH;
    disp_drv.ver_res = SCREEN_HEIGHT;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);

    // ==================== BUILD UI SHOWCASE MATRIX ====================
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_make(15, 15, 22), 0);

    lv_obj_t *tab_view = lv_tabview_create(lv_scr_act(), LV_DIR_TOP, 50);
    lv_obj_set_style_bg_color(tab_view, lv_color_make(20, 20, 30), LV_PART_MAIN);
    
    lv_obj_t *tab_btns = lv_tabview_get_tab_btns(tab_view);
    lv_obj_set_style_bg_color(tab_btns, lv_color_make(30, 30, 45), LV_PART_MAIN);
    lv_obj_set_style_text_color(tab_btns, lv_color_make(200, 200, 255), LV_PART_ITEMS);

    lv_obj_t *tab1 = lv_tabview_add_tab(tab_view, "Instruments");
    lv_obj_t *tab2 = lv_tabview_add_tab(tab_view, "Waveforms");
    lv_obj_t *tab3 = lv_tabview_add_tab(tab_view, "Controls");

    // TAB 1: INSTRUMENTS
    lv_obj_t *arc = lv_arc_create(tab1);
    lv_obj_set_size(arc, 220, 220);
    lv_obj_align(arc, LV_ALIGN_CENTER, 0, -20);
    lv_arc_set_rotation(arc, 135);
    lv_arc_set_bg_angles(arc, 0, 270);
    lv_arc_set_value(arc, 65);
    
    lv_obj_set_style_arc_color(arc, lv_color_make(0, 220, 255), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, 16, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, 16, LV_PART_MAIN);
    lv_obj_add_event_cb(arc, arc_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    arc_label = lv_label_create(tab1);
    lv_label_set_text(arc_label, "65%");
    lv_obj_align(arc_label, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_text_font(arc_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(arc_label, lv_color_white(), 0);

    lv_obj_t *sub_label = lv_label_create(tab1);
    lv_label_set_text(sub_label, "System Core Engine");
    lv_obj_align(sub_label, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_text_color(sub_label, lv_color_make(120, 120, 160), 0);

    // TAB 2: WAVEFORMS
    chart = lv_chart_create(tab2);
    lv_obj_set_size(chart, 400, 260);
    lv_obj_align(chart, LV_ALIGN_CENTER, 0, 0);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, 24);
    
    lv_chart_set_div_line_count(chart, 5, 7);
    lv_obj_set_style_bg_color(chart, lv_color_make(10, 10, 15), 0);
    lv_obj_set_style_border_color(chart, lv_color_make(50, 50, 70), 0);

    ser1 = lv_chart_add_series(chart, lv_color_make(255, 0, 128), LV_CHART_AXIS_PRIMARY_Y);
    ser2 = lv_chart_add_series(chart, lv_color_make(0, 255, 128), LV_CHART_AXIS_PRIMARY_Y);

    for(int i=0; i<24; i++) {
        lv_chart_set_next_value(chart, ser1, sin(i * 0.5) * 40 + 50);
        lv_chart_set_next_value(chart, ser2, cos(i * 0.4) * 30 + 50);
    }

    // TAB 3: CONTROLS
    lv_obj_t *spinner = lv_spinner_create(tab3, 1000, 60);
    lv_obj_set_size(spinner, 80, 80);
    lv_obj_align(spinner, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_set_style_arc_color(spinner, lv_color_make(255, 165, 0), LV_PART_INDICATOR);

    lv_obj_t *slider = lv_slider_create(tab3);
    lv_obj_set_size(slider, 300, 15);
    lv_obj_align(slider, LV_ALIGN_BOTTOM_MID, 0, -50);
    lv_slider_set_value(slider, 40, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_make(255, 0, 128), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_make(255, 255, 255), LV_PART_KNOB);

    Serial.println("Showcase UI structures successfully deployed.");
}

void loop() {
    static uint32_t last_anim_tick = 0;
    static float time_offset = 0;

    lv_timer_handler();
    
    if (millis() - last_anim_tick > 50) { 
        last_anim_tick = millis();
        time_offset += 0.1;

        for(int i = 0; i < 23; i++) {
            ser1->y_points[i] = ser1->y_points[i+1];
            ser2->y_points[i] = ser2->y_points[i+1];
        }
        
        int32_t val1 = (int32_t)(sin(time_offset) * 35 + 50);
        int32_t val2 = (int32_t)(cos(time_offset * 1.5) * 25 + 50);

        lv_chart_set_value_by_id(chart, ser1, 23, val1);
        lv_chart_set_value_by_id(chart, ser2, 23, val2);
        
        lv_chart_refresh(chart);
    }
    delay(4); 
}

#if 0
#include <Arduino_GFX_Library.h>
#include <TAMC_GT911.h>
#include <Wire.h>
#include <lvgl.h>
#include "esp_log.h"

#define SCREEN_WIDTH  480
#define SCREEN_HEIGHT 480

// 1. HARDWARE STORAGE MAPPINGS (Verified Stable)
Arduino_DataBus *bus = new Arduino_SWSPI(GFX_NOT_DEFINED, 39, 48, 47, GFX_NOT_DEFINED);
Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    18, 17, 16, 21, 11, 12, 13, 14, 0, 8, 20, 3, 46, 9, 10, 4, 5, 6, 7, 15,
    1, 10, 8, 50, 1, 10, 8, 20
);
Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
    SCREEN_WIDTH, SCREEN_HEIGHT, rgbpanel, 0, true, bus, -1, 
    st7701_type9_init_operations, sizeof(st7701_type9_init_operations)
);

#define TOUCH_SDA       19
#define TOUCH_SCL       45
#define TOUCH_INT       4        // dummy GPIO
#define TOUCH_RST       5        // dummy GPIO
TAMC_GT911 ts = TAMC_GT911(TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST, SCREEN_WIDTH, SCREEN_HEIGHT);

static lv_disp_draw_buf_t draw_buf;
static lv_color_t *disp_draw_buf = NULL;

// Global Forth UI Object References
static lv_obj_t *ta_console;
static lv_obj_t *kb_matrix;

// Display buffer flush gateway
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
    lv_disp_flush_ready(disp);
}

// 2. FIXED DEBOUNCED TOUCHPAD READ CALLBACK (Removes Jitter)
void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
    static int last_x = 0;
    static int last_y = 0;
    static uint32_t last_touch_time = 0;
    
    ts.read();

    if (ts.isTouched) {
        int touchX = SCREEN_WIDTH - ts.points[0].x; 
        int touchY = SCREEN_HEIGHT - ts.points[0].y;

        // Apply visual boundary constraints to ignore wild out-of-screen noise spikes
        if (touchX >= 0 && touchX < SCREEN_WIDTH && touchY >= 0 && touchY < SCREEN_HEIGHT) {
            // Software Debouncer: Check if contact shifts drastically within a tiny time block
            if (millis() - last_touch_time < 30 && abs(touchX - last_x) < 3 && abs(touchY - last_y) < 3) {
                // Keep the previous stable state to suppress micro-vibrations
                data->state = LV_INDEV_STATE_PR;
                data->point.x = last_x;
                data->point.y = last_y;
                return;
            }

            last_x = touchX;
            last_y = touchY;
            last_touch_time = millis();

            data->state = LV_INDEV_STATE_PR;
            data->point.x = touchX;
            data->point.y = touchY;
        }
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

// 3. FORTH INTERPRETER CONNECTOR CORE
void execute_forth_command(const char *cmd) {
    // Echo the command back onto the terminal canvas window cleanly
    lv_textarea_add_text(ta_console, "> ");
    lv_textarea_add_text(ta_console, cmd);
    lv_textarea_add_text(ta_console, "\n");

    // --- YOUR FORTH C ENGINE INJECTION NODE ---
    // This is where you pass the text string straight to your dictionary interpreter parser loop!
    if (strcmp(cmd, "1 2 + .") == 0) {
        lv_textarea_add_text(ta_console, "3 ok\n");
    } else if (strcmp(cmd, "WORDS") == 0 || strcmp(cmd, "words") == 0) {
        lv_textarea_add_text(ta_console, "DUP DROP SWAP + - * / . EMIT CR WORDS\n");
    } else {
        lv_textarea_add_text(ta_console, " ok\n");
    }
    // ------------------------------------------
}

// Keyboard matrix callback events router
static void kb_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * kb = lv_event_get_target(e);
    
    if(code == LV_EVENT_VALUE_CHANGED) {
        uint16_t btn_id = lv_btnmatrix_get_selected_btn(kb);
        const char * txt = lv_btnmatrix_get_btn_text(kb, btn_id);
        
        if(strcmp(txt, "EXE") == 0) {
            // Pull text from the input tracking layer and send it to our interpreter node
            // For now, we simulate executing a sample statement
            execute_forth_command("1 2 + ."); 
        } 
        else if(strcmp(txt, "CLR") == 0) {
            lv_textarea_set_text(ta_console, ""); // Wipe terminal logs cleanly
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("--- FORTH TERMINAL BASELINE ENGINE ONLINE ---");

    pinMode(38, OUTPUT);
    digitalWrite(38, HIGH);

    gfx->begin();
    gfx->fillScreen(BLACK);

    Wire.begin(TOUCH_SDA, TOUCH_SCL); 
    esp_log_level_set("gpio", ESP_LOG_NONE); 
    ts.begin();
    ts.setRotation(ROTATION_NORMAL);
    esp_log_level_set("gpio", ESP_LOG_ERROR); 

    // Initialize Core LVGL Context Structures
    lv_init();
    disp_draw_buf = (lv_color_t *)ps_malloc(SCREEN_WIDTH * 40 * sizeof(lv_color_t));
    if (disp_draw_buf == NULL) while(1);
    lv_disp_draw_buf_init(&draw_buf, disp_draw_buf, NULL, SCREEN_WIDTH * 40);

    // Register Output Pipelines
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = SCREEN_WIDTH;
    disp_drv.ver_res = SCREEN_HEIGHT;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    // Register Input Pipelines
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);

    // ==================== BUILD FORTH UX GRAPHICS MATRIX ====================
    
    // Panel A: Top Scrolling Terminal Window Console Layout
    ta_console = lv_textarea_create(lv_scr_act());
    lv_obj_set_size(ta_console, SCREEN_WIDTH, 240);
    lv_obj_align(ta_console, LV_ALIGN_TOP_MID, 0, 0);
    lv_textarea_set_cursor_click_pos(ta_console, false); // Keep manual keyboard hidden
    lv_textarea_set_text(ta_console, "Guition 4848S040 Forth OS v0.1\nType WORDS to list dictionary.\n\n");
    lv_obj_set_style_text_font(ta_console, &lv_font_montserrat_14, 0);

    // Panel B: Keyboard Interaction Matrix Array Map
    static const char * kb_map[] = {
        "1", "2", "3", "DUP", "\n",
        "4", "5", "6", "DROP", "\n",
        "7", "8", "9", "SWAP", "\n",
        "0", "+", "-", "WORDS", "\n",
        ".", "CLR", "EXE", ""
    };

    kb_matrix = lv_btnmatrix_create(lv_scr_act());
    lv_obj_set_size(kb_matrix, SCREEN_WIDTH, 240);
    lv_obj_align(kb_matrix, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_btnmatrix_set_map(kb_matrix, kb_map);
    lv_obj_add_event_cb(kb_matrix, kb_event_cb, LV_EVENT_ALL, NULL);

    // Apply crisp, dark theme styling options to the terminal interface keys
    lv_obj_set_style_bg_color(kb_matrix, lv_color_make(30, 30, 30), 0);
    
    Serial.println("Forth interactive shell booted and waiting for inputs.");
}

void loop() {
    lv_timer_handler();
    delay(5);
}
#endif
