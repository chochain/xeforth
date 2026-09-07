\page 1 - Platform Specific Code directory
will be included at the end of ~/xeforth.ino

## MCU proxy for Forth MCU extended words
+ mcu     - MCU proxy for forth MCU extended words and the following

### ESP32 specific
+ xbridge - message definition
+ xque    - queue class (wrapper)
+ xserver - web server task (on core0)
+ xforth  - forth vm task (on core0)
+ xgl     - display and touch screen task (on core1)
