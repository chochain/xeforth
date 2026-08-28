#ifndef _XQUE_H
#define _XQUE_H
#include "xbridge.h"

#if !(ARDUINO || ESP32)
#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>

template <typename T>
class XQueue {
private:
    std::queue<T>           _queue;
    std::mutex              _mutex;
    std::condition_variable _cond_var;
    size_t                  _max_capacity;

public:
    XQueue(size_t capacity = 10) : _max_capacity(capacity) {}

    /* Emulates: xQueueSend(queue, &item, 0) -- Non-blocking push */
    bool send_non_blocking(const T &item) {
        std::unique_lock<std::mutex> lock(_mutex);
        if (_queue.size() >= _max_capacity) {
            return false; /* Buffer overflow protection - drop packet safely */
        }
        _queue.push(item);
        _cond_var.notify_one(); /* Wake up the thread waiting on an empty queue */
        return true;
    }

    /* Emulates: xQueueReceive(queue, &item, portMAX_DELAY) -- Indefinite blocking pull */
    void receive_blocking(T &item) {
        std::unique_lock<std::mutex> lock(_mutex);
        _cond_var.wait(lock, [this]() { return !_queue.empty(); });
        item = _queue.front();
        _queue.pop();
    }

    /* Emulates: xQueueReceive(queue, &item, 0) -- Non-blocking poll hook */
    bool receive_non_blocking(T &item) {
        std::unique_lock<std::mutex> lock(_mutex);
        if (_queue.empty()) {
            return false;
        }
        item = _queue.front();
        _queue.pop();
        return true;
    }
};

typedef XQueue<que_msg_t>  *QueueHandle_t;
typedef XQueue<draw_vec_t> *GLQueueHandle_t;

#endif // !(ARDUINO || ESP32)
#endif // _XQUE_H
