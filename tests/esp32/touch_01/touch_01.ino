/** -*- mode: c++ -*-
 * Guition 4848S040 v0.2 GT911 Touch Example
 * Required Libraries:
 *   - GFX Library for Arduino v1.5.0
 *   - TAMC GT911 v1.0.2
 */
#include <Arduino_GFX_Library.h>
#include <TAMC_GT911.h>
#include <Wire.h>

#define SCREEN_WIDTH  480
#define SCREEN_HEIGHT 480

// 1. HARDWARE SPI CONTROL BUS
Arduino_DataBus *bus = new Arduino_SWSPI(
    GFX_NOT_DEFINED /* DC */, 
    39              /* CS */, 
    48              /* SCK */, 
    47              /* MOSI / SDA */, 
    GFX_NOT_DEFINED /* MISO */
);

// 2. ST7701S RGB INTERFACE PHYSICAL PIN ASSIGNMENTS
Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    18 /* DE */, 17 /* VSYNC */, 16 /* HSYNC */, 21 /* PCLK */,
    11 /* R0 */, 12 /* R1 */, 13 /* R2 */, 14 /* R3 */, 0  /* R4 */,
    8  /* G0 */, 20 /* G1 */, 3  /* G2 */, 46 /* G3 */, 9  /* G4 */, 10 /* G5 */,
    4  /* B0 */, 5  /* B1 */, 6  /* B2 */, 7  /* B3 */, 15 /* B4 */,
    1  /* hsync_polarity */, 10 /* hsync_front_porch */, 8 /* hsync_pulse_width */, 50 /* hsync_back_porch */,
    1  /* vsync_polarity */, 10 /* vsync_front_porch */, 8 /* vsync_pulse_width */, 20 /* vsync_back_porch */
);

Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
    SCREEN_WIDTH, SCREEN_HEIGHT, rgbpanel, 
    0 /* Rotation */, true /* auto_flush */, 
    bus, -1 /* RST Pin */, 
    st7701_type9_init_operations, sizeof(st7701_type9_init_operations)
);

// 3. CORRECTED TOUCH DEFINITIONS (Using GFX_NOT_DEFINED silences internal log errors)
#define TOUCH_SDA  19
#define TOUCH_SCL  45
#define TOUCH_INT  4   // dummy GPIO for v0.2, I2C=>relay
#define TOUCH_RST  5   // dummy GPIO for v0.2
TAMC_GT911 ts = TAMC_GT911(TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST, SCREEN_WIDTH, SCREEN_HEIGHT);

int click_counter = 0;
bool button_pressed = false;

const int btnX = 140;
const int btnY = 160;
const int btnW = 200;
const int btnH = 60;

void drawButton(bool pressed) {
    uint16_t btnColor = pressed ? RED : BLUE;
    gfx->fillRoundRect(btnX, btnY, btnW, btnH, 10, btnColor);
    gfx->drawRoundRect(btnX, btnY, btnW, btnH, 10, WHITE);
    
    gfx->setCursor(btnX + 55, btnY + 22);
    gfx->setTextColor(WHITE);
    gfx->setTextSize(2);
    gfx->print("TAP ME!");
}

void updateCounterLabel() {
    gfx->fillRect(100, 260, 280, 40, BLACK);
    gfx->setCursor(160, 270);
    gfx->setTextColor(GREEN);
    gfx->setTextSize(3);
    gfx->printf("Clicks: %d", click_counter);
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("--- ARDUINO RGB DISPLAY BOOT ---");

    // Turn on the Backlight (GPIO 38 on Guition v0.2)
    pinMode(38, OUTPUT);
    digitalWrite(38, HIGH);

    gfx->begin();
    gfx->fillScreen(BLACK);

    Serial.println("Starting Touch I2C Line Bus...");
    Wire.begin(TOUCH_SDA, TOUCH_SCL); 

    ts.begin();
    ts.setRotation(ROTATION_NORMAL);

    gfx->setCursor(50, 60);
    gfx->setTextColor(WHITE);
    gfx->setTextSize(3);
    gfx->print("Guition 4848S040 GT911");

    drawButton(false);
    updateCounterLabel();
    
    Serial.println("System initialized successfully under generic driver framework!");
}

void loop() {
    ts.read();

    if (ts.isTouched) {
        int touchX = SCREEN_WIDTH - ts.points[0].x; 
        int touchY = SCREEN_HEIGHT - ts.points[0].y;

        if (touchX >= btnX && touchX <= (btnX + btnW) && touchY >= btnY && touchY <= (btnY + btnH)) {
            if (!button_pressed) { 
                button_pressed = true;
                click_counter++;
                
                // CLEAN REFACTOR: Only prints once per touch event instead of 6 times!
                Serial.printf("New Tap Handled -> X: %d, Y: %d | Total Clicks: %d\n", touchX, touchY, click_counter);
                
                drawButton(true);       
                updateCounterLabel();   
            }
        }
    } else {
        if (button_pressed) {
            button_pressed = false;
            drawButton(false); 
        }
    }
    delay(20); 
}
