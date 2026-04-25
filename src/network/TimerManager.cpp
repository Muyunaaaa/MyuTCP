#include "network/TimerManager.h"

#include "spdlog/spdlog.h"


void TimerManager::stop_timer(uint32_t seq_num) {
    auto it = timers_.find(seq_num);
    if (it != timers_.end()) {
        uv_timer_t *timer = it->second;
        // use uv close function
        uv_timer_stop(timer);
        spdlog::info("stop timer whose seq = {} ", seq_num);
    }
}

void TimerManager::drop_timer(uint32_t seq_num) {
    auto it = timers_.find(seq_num);
    if (it != timers_.end()) {
        uv_timer_t *timer = it->second;
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
        spdlog::info("drop timer whose seq = {} ", seq_num);
    }
}

void TimerManager::drop_all_timers() {
    for (auto it = timers_.begin(); it != timers_.end();) {
        uint32_t seq = it->first;
        uv_timer_t *timer = it->second;

        uv_timer_stop(timer);
        uv_close(reinterpret_cast<uv_handle_t *>(timer), [](uv_handle_t *handle) {
            auto *t = reinterpret_cast<uv_timer_t *>(handle);

            if (t->data) {
                delete static_cast<std::function<void()> *>(t->data);
                t->data = nullptr;
            }

            delete t;
        });

        it = timers_.erase(it);
        spdlog::info("drop timer whose seq = {} ", seq);
    }
}

void TimerManager::drop_timer_up_to(uint32_t ack_num) {
    auto it_end = timers_.lower_bound(ack_num);
    for (auto it = timers_.begin(); it != it_end; ++it) {
        uint32_t seq = it->first;

        drop_timer(seq);
    }
}

// ack number, it means the packet to be sent, so its timer may has been started but not timeout yet
// if seq = ack_num, we may stop the timer we should not stop, so we just stop the timer whose seq < ack_num
void TimerManager::stop_timers_up_to(uint32_t ack_num) {
    // because the map is ordered by key, we can use lower_bound to find the first timer whose seq is greater than or equal to ack_num
    auto it_end = timers_.lower_bound(ack_num);

    for (auto it = timers_.begin(); it != it_end; ++it) {
        uint32_t seq = it->first;
        uv_timer_t *timer = it->second;

        uv_timer_stop(timer);

        spdlog::info("stop timer whose seq = {} (ACKed by {})", seq, ack_num);
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
    };
    timer->data = new std::function<void()>(std::move(wrapped_callback));
    uv_timer_start(timer, on_timer_uv_tick, timeout_ms, 0);
    timers_[seq_num] = timer;
    spdlog::info("start timer whose seq = {} ", seq_num);
}

void TimerManager::stop_all_timers() {
    for (auto it = timers_.begin(); it != timers_.end(); ++it) {
        uint32_t seq = it->first;
        uv_timer_t *timer = it->second;

        uv_timer_stop(timer);
    }
    uv_timer_stop(_2msl_timer.get());
    spdlog::debug("All timers stopped");
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
