#ifndef _XBRIDGE_H
#define _XBRIDGE_H
#include <stdint.h>
#include <string.h>

/* Queue A: Text input boundaries passing from Web Interface -> Forth Processor */
#define QUE_BUF_SZ 128
typedef struct {
    uint32_t id;
    uint8_t  buf[QUE_BUF_SZ];
    bool     eos;
} msg_web_t;

/* EXEC 8-style workload classification (UNIVAC 1108's three mixed job types).
 * Shared vocabulary between the web front-end and the Forth core - both sides
 * need to agree on what these mean, even though only REALTIME currently
 * changes queue routing (see XServer::_web / _web_rt). BATCH vs DEMAND is,
 * for now, purely a session/display distinction on the web side. */
typedef enum {
    JOB_BATCH    = 0,  /// submit-and-collect: queued, no live interaction expected
    JOB_DEMAND   = 1,  /// interactive/time-sharing: low-latency, session held open
    JOB_REALTIME = 2   /// preemptive: serviced ahead of BATCH/DEMAND, not FIFO order
} job_class_t;

/* Queue B: Abstract drawing operations passing from Forth -> LVGL Renderer */
typedef enum {
    VECTOR_CLEAR = 0,
    VECTOR_LINE  = 1,
    VECTOR_CMD   = 2
} vector_op_t;

typedef struct {
    uint8_t  op_code;
    int16_t  x1;
    int16_t  y1;
    int16_t  x2;
    int16_t  y2;
    uint8_t  buf[QUE_BUF_SZ];
} msg_gui_t;

#endif // _XBRIDGE_H
