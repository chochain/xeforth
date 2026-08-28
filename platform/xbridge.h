#ifndef _XBRIDGE_H
#define _XBRIDGE_H
#include <stdint.h>
#include <string.h>

/* Queue A: Text input boundaries passing from Web Interface -> Forth Processor */
#define QUE_BUF_SZ 128
typedef struct {
    char buf[QUE_BUF_SZ];
} que_msg_t;

/* Queue B: Abstract drawing operations passing from Forth -> LVGL Renderer */
typedef enum {
    VECTOR_CLEAR = 0,
    VECTOR_LINE  = 1
} vector_op_t;

typedef struct {
    uint8_t  op_code;
    int16_t  x1;
    int16_t  y1;
    int16_t  x2;
    int16_t  y2;
} draw_vec_t;

#endif // _XBRIDGE_H
