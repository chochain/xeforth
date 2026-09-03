/** -*- mode: c++ -*-
 * Guition 4848S040 v0.2 GT911 Touch Example
 * Required Libraries:
 *   - GFX Library of Arduino v1.5.0
 *   - TAMC GT911 v1.0.2
 *   - lvgl v8.4
 */
#include <Arduino_GFX_Library.h>
#include <TAMC_GT911.h>
#include <Wire.h>
#include <lvgl.h>
//#include "esp_log.h"

#define SCREEN_WIDTH  480
#define SCREEN_HEIGHT 480

// 1. HARDWARE STORAGE MAPPINGS 
Arduino_DataBus *bus = new Arduino_SWSPI(GFX_NOT_DEFINED, 39, 48, 47, GFX_NOT_DEFINED);
Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    18, 17, 16, 21, 11, 12, 13, 14, 0, 8, 20, 3, 46, 9, 10, 4, 5, 6, 7, 15,
    1, 10, 8, 50, 1, 10, 8, 20, 1, 9000000
);
Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
    SCREEN_WIDTH, SCREEN_HEIGHT, rgbpanel, 0, true, bus, -1, 
    st7701_type9_init_operations, sizeof(st7701_type9_init_operations)
);

#define TOUCH_SDA  19
#define TOUCH_SCL  45
#define TOUCH_INT  4   // B0, might blip at start
#define TOUCH_RST  5   // B1 
TAMC_GT911 ts = TAMC_GT911(TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST, SCREEN_WIDTH, SCREEN_HEIGHT);

static lv_disp_draw_buf_t draw_buf;
static lv_color_t *disp_draw_buf = NULL;

// Global UI Handles
static lv_obj_t *terminal_log;
static lv_obj_t *chart;
static lv_chart_series_t *cpu_series;
static lv_chart_series_t *ram_series;

// Serial Command Parsing Buffer
String inputBuffer = "";

// Display buffer flush gateway
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
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

// Thread-safe terminal stream printer
void printToTerminal(const char *text, lv_color_t textColor) {
    // Append text to terminal object canvas
    lv_textarea_add_text(terminal_log, text);
    
    // Auto-scroll logic: lock view frame to bottom lines
    uint32_t txt_len = strlen(lv_textarea_get_text(terminal_log));
    lv_textarea_set_cursor_pos(terminal_log, txt_len);
}

// Core Execution Gateway for Inbound Command Text Strings
void parseInboundStream(String command) {
    command.trim(); // Strip formatting whitespace
    if (command.length() == 0) return;

    // Echo command back to the screen terminal using standard shell formatting
    String echoStr = "> " + command + "\n";
    printToTerminal(echoStr.c_str(), lv_color_make(0, 255, 255));

    // --- YOUR PROCESSING HUB (Serial / Forth / MQTT Interface) ---
    if (command.equalsIgnoreCase("help")) {
        printToTerminal("Guition Shell Commands:\n - CLEAR : Wipe Log Screen\n - STATS : Dump Device Telemetry\n", lv_color_white());
    } 
    else if (command.equalsIgnoreCase("clear")) {
        lv_textarea_set_text(terminal_log, "");
    } 
    else if (command.equalsIgnoreCase("stats")) {
        char stats_buf[64];
        sprintf(stats_buf, "Free Heap: %d KB | Free PSRAM: %d KB\n", 
                ESP.getFreeHeap() / 1024, ESP.getFreePsram() / 1024);
        printToTerminal(stats_buf, lv_color_make(0, 255, 0));
    } 
    else {
        // Mock output template for your Forth engine
        printToTerminal("Executing... ok\n", lv_color_make(150, 150, 150));
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("--- STREAMING TERMINAL CONSOLE ENGINE ONLINE ---");

    pinMode(38, OUTPUT);
    digitalWrite(38, HIGH); // Backlight ON

    gfx->begin();
    gfx->fillScreen(BLACK);

    Wire.begin(TOUCH_SDA, TOUCH_SCL); 
    ts.begin();
    ts.setRotation(ROTATION_NORMAL);

    lv_init();
    
    // 2. Allocate the 480x40 true-color frame buffer strictly in External PSRAM
    uint32_t buffer_pixel_size = SCREEN_WIDTH * 40;
    disp_draw_buf = (lv_color_t *)ps_malloc(buffer_pixel_size * sizeof(lv_color_t));
    if (disp_draw_buf == NULL) while(1);
    lv_disp_draw_buf_init(&draw_buf, disp_draw_buf, NULL, buffer_pixel_size);

    // 3. Instantiate the LVGL Canvas widget container
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = SCREEN_WIDTH;
    disp_drv.ver_res = SCREEN_HEIGHT;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    // Deep Dark Industrial Styling Matrix
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_make(10, 12, 16), 0);

    // ==================== PANEL 1: SYSTEM METRICS CHART (Top) ====================
    chart = lv_chart_create(lv_scr_act());
    lv_obj_set_size(chart, SCREEN_WIDTH - 20, 150);
    lv_obj_align(chart, LV_ALIGN_TOP_MID, 0, 10);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, 30);
    lv_chart_set_div_line_count(chart, 4, 6);
    
    // Style the visualization grid
    lv_obj_set_style_bg_color(chart, lv_color_make(18, 20, 26), 0);
    lv_obj_set_style_border_color(chart, lv_color_make(35, 40, 50), 0);
    
    cpu_series = lv_chart_add_series(chart, lv_color_make(0, 255, 120), LV_CHART_AXIS_PRIMARY_Y); // Emerald Cyan
    ram_series = lv_chart_add_series(chart, lv_color_make(255, 50, 100), LV_CHART_AXIS_PRIMARY_Y); // Magenta

    // Pre-populate chart values
    for(int i = 0; i < 30; i++) {
        lv_chart_set_next_value(chart, cpu_series, 20);
        lv_chart_set_next_value(chart, ram_series, 45);
    }

    // ==================== PANEL 2: SCROLLING TERMINAL CANVAS (Bottom) ====================
    terminal_log = lv_textarea_create(lv_scr_act());
    lv_obj_set_size(terminal_log, SCREEN_WIDTH - 20, 290);
    lv_obj_align(terminal_log, LV_ALIGN_BOTTOM_MID, 0, -10);
    
    // Force a classic retro-monospaced terminal color layout
    lv_obj_set_style_bg_color(terminal_log, lv_color_make(5, 6, 8), 0);
    lv_obj_set_style_text_color(terminal_log, lv_color_make(50, 255, 100), 0); // Terminal Green
    lv_obj_set_style_border_color(terminal_log, lv_color_make(35, 40, 50), 0);
    
    // Hide standard cursor adjustments to prevent user selection interference
    lv_textarea_set_cursor_click_pos(terminal_log, false);
    
    // Add Boot Greetings Text String
    lv_textarea_set_text(terminal_log, "Guition OS Kernel Initialized.\nListening for Stream Packets...\nType 'help' via Serial line.\n\n");

    Serial.println("Dashboard initialized. Feed commands via Serial terminal.");
}

void loop() {
    static uint32_t last_telemetry_tick = 0;
    static uint32_t live_cpu_sim = random(15, 65); // Replace with your real runtime metrics
    static uint32_t live_ram_metrics = random(5, 95); // map(ESP.getFreeHeap(), 0, 280000, 100, 0); // Inverse map to get usage percentage

    // Core task loop updates
    lv_timer_handler();

    // 1. ASYNCHRONOUS EXTERNAL STREAM INTERCEPTOR (Serial Line Input Handler)
    while (Serial.available() > 0) {
        char inChar = (char)Serial.read();
        if (inChar == '\n' || inChar == '\r') {
            if (inputBuffer.length() > 0) {
                parseInboundStream(inputBuffer);
                inputBuffer = ""; // Reset internal storage pointer array
            }
        } else {
            inputBuffer += inChar;
        }
        Serial.print(inChar);
    }

    // 2. LIVE TELEMETRY LOG DATA MODULATION (Updates every 500ms)
    if (millis() - last_telemetry_tick > 50) {
        last_telemetry_tick = millis();

        // Shift existing values backward
        for (int i = 0; i < 29; i++) {
            cpu_series->y_points[i] = cpu_series->y_points[i + 1];
            ram_series->y_points[i] = ram_series->y_points[i + 1];
        }


        // Fetch actual hardware configurations dynamically
        live_cpu_sim     = (uint32_t)(0.8 * live_cpu_sim + 0.2 * random(15, 65)); // Replace with your real runtime metrics
        live_ram_metrics = (uint32_t)(0.8 * live_ram_metrics + 0.2 * random(5, 95)); // map(ESP.getFreeHeap(), 0, 280000, 100, 0); // Inverse map to get usage percentage

        lv_chart_set_value_by_id(chart, cpu_series, 29, live_cpu_sim);
        lv_chart_set_value_by_id(chart, ram_series, 29, live_ram_metrics);
        lv_chart_refresh(chart);
    }

    delay(4);
}
