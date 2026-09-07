#ifndef _XQUE_H
#define _XQUE_H
#include "xbridge.h"

#if (ARDUINO || ESP32)
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// ==========================================
// ESP32 NATIVE FREERTOS IMPLEMENTATION
// ==========================================
template <typename T>
class XQueue {
private:
    QueueHandle_t _queue;
    size_t        _qsz;

public:
    XQueue(size_t sz = 10) : _qsz(sz) {
        // FreeRTOS requires item size explicitly
        _queue = xQueueCreate(_qsz, sizeof(T)); 
    }
    ~XQueue() {
        if (_queue != NULL) vQueueDelete(_queue);
    }

    bool send_non_blocking(const T &item) {
        return xQueueSend(_queue, &item, 0) == pdPASS;    /// 0 wait ticks => immediately return
    }
    bool receive_non_blocking(T &item) {
        return xQueueReceive(_queue, &item, 0) == pdPASS; // non-blocking pool
    }
    void receive_blocking(T &item) {
        xQueueReceive(_queue, &item, portMAX_DELAY);      /// MAX_DELAY => blocking
    }
    /* ISR context methods */
    bool send_from_isr(const T &item, BaseType_t *isr_priority) {
        // Safe to call inside an interrupt routine
        return xQueueSendFromISR(_queue, &item, isr_priority) == pdPASS;
    }
    bool receive_from_isr(T &item, BaseType_t *isr_priority) {
        // Safe to read inside an interrupt routine if necessary
        return xQueueReceiveFromISR(_queue, &item, isr_priority) == pdPASS;
    }
};

#else // !(ARDUINO || ESP32)
// ==========================================
// DESKTOP / SIMULATOR STL IMPLEMENTATION
// ==========================================
#include <queue>
#include <mutex>
#include <condition_variable>
typedef uint32_t UBaseType_t;
typedef int32_t  BaseType_t;

template <typename T>
class XQueue {
private:
    std::queue<T>           _queue;
    std::mutex              _mutex;
    std::condition_variable _cond_var;
    size_t                  _qsz;

public:
    XQueue(size_t sz = 10) : _qsz(sz) {}

    bool send_non_blocking(const T &item) {
        std::unique_lock<std::mutex> lock(_mutex);
        if (_queue.size() >= _qsz) return false;

        _queue.push(item);
        _cond_var.notify_one();
        return true;
    }
    bool receive_non_blocking(T &item) {
        std::unique_lock<std::mutex> lock(_mutex);
        if (_queue.empty()) return false;
        item = _queue.front();
        _queue.pop();
        return true;
    }
    void receive_blocking(T &item) {
        std::unique_lock<std::mutex> lock(_mutex);
        _cond_var.wait(lock, [this]() { return !_queue.empty(); });
        item = _queue.front();
        _queue.pop();
    }
    /* Host/Simulator Fallbacks (Map directly to non-blocking) */
    bool send_from_isr(const T &item, BaseType_t *isr_priority) {
        return send_non_blocking(item);
    }
    bool receive_from_isr(T &item, BaseType_t *isr_priority) {
        return receive_non_blocking(item);
    }
};

#endif // (ARDUINO || ESP32)

// ==========================================
// SHARED CLASS & TYPEDEFS (Works on Both)
// ==========================================
template <typename ReqT, typename RspT>
class MBox {
private:
    XQueue<ReqT> _req_q;
    XQueue<RspT> _rsp_q;

public:
    MBox(size_t req_qsz=10, size_t rsp_qsz=0) : _req_q(req_qsz), _rsp_q(rsp_qsz ? rsp_qsz : req_qsz) {}

    bool put_req(const ReqT &item) { return _req_q.send_non_blocking(item);    }
    bool put_rsp(const RspT &item) { return _rsp_q.send_non_blocking(item);    }
    bool get_req(ReqT &item)       { return _req_q.receive_non_blocking(item); }
    bool get_rsp(RspT &item)       { return _rsp_q.receive_non_blocking(item); }
    void wait_for_req(ReqT &item)   { _req_q.receive_blocking(item);           }
    void wait_for_rsp(RspT &item)   { _rsp_q.receive_blocking(item);           }
    /* ISR Context API */
    bool isr_put_req(const ReqT &item, BaseType_t *isr_priority) { 
        return _req_q.send_from_isr(item, isr_priority); 
    }
    bool isr_get_req(ReqT &item, BaseType_t *isr_priority) { 
        return _req_q.receive_from_isr(item, isr_priority); 
    }
};

typedef MBox<msg_raw_t, msg_raw_t> xQueWeb;
typedef MBox<msg_gui_t, msg_gui_t> xQueUI;

#endif // _XQUE_H

