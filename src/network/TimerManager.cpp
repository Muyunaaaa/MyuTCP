#include "network/TimerManager.h"

#include "spdlog/spdlog.h"

void stop_timer_without_erase(uint32_t seq_num) {
}

void TimerManager::stop_timer(uint32_t seq_num) {
    auto it = timers_.find(seq_num);
    if (it != timers_.end()) {
        uv_timer_t *timer = it->second;
        // use uv close function
        uv_timer_stop(timer);
        uv_close(reinterpret_cast<uv_handle_t *>(timer), [](uv_handle_t *handle) {
            auto *t = reinterpret_cast<uv_timer_t *>(handle);

            if (t->data) {
                delete static_cast<std::function<void()> *>(t->data);
                t->data = nullptr;
            }

            delete t;
        });

        timers_.erase(it);
        spdlog::info("stop timer whose seq = {} ", seq_num);
    }
}

void on_timer_uv_tick(uv_timer_t *handle) {
    auto *func = static_cast<std::function<void()> *>(handle->data);

    if (func && *func) {
        (*func)();
    }
}

void TimerManager::start_timer(uint32_t seq_num, uint64_t timeout_ms, std::function<void()> callback) {
    // if the seq has timer already, stop it
    if (timers_.contains(seq_num)) stop_timer(seq_num);
    uv_timer_t *timer = new uv_timer_t();
    uv_timer_init(this->loop_, timer);
    // if the timer is timeout, however the timer would not be stopped
    // so we stop it in the timeout callback function
    auto wrapped_callback = [this, seq_num, u_cb = std::move(callback)]() {
        u_cb();
        stop_timer(seq_num);
    };
    timer->data = new std::function<void()>(std::move(wrapped_callback));
    uv_timer_start(timer, on_timer_uv_tick, timeout_ms, 0);
    timers_[seq_num] = timer;
    spdlog::info("start timer whose seq = {} ", seq_num);
}

TimerManager::~TimerManager() {
    for (auto &[seq, timer]: timers_) {
        uv_timer_stop(timer);
        uv_close(reinterpret_cast<uv_handle_t *>(timer), [](uv_handle_t *handle) {
            auto *t = reinterpret_cast<uv_timer_t *>(handle);

            if (t->data) {
                delete static_cast<std::function<void()> *>(t->data);
                t->data = nullptr;
            }
            delete t;
        });
    }
    spdlog::warn("TimeManager destroyed, all timers cancelled.");
}
