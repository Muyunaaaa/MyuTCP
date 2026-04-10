#pragma once
#include <functional>
#include <map>
#include <uv.h>

struct TimerManager {
private:
    uv_loop_t *loop_;
    std::map<uint32_t, uv_timer_t *> timers_;

public:
    explicit TimerManager(uv_loop_t *loop) : loop_(loop) {
    }

    TimerManager(const TimerManager &) = delete;

    ~TimerManager();

    // start timer
    void start_timer(uint32_t seq_num, uint64_t timeout_ms, std::function<void()> callback);

    // stop timer
    void stop_timer(uint32_t seq_num);

    // stop all timers whose seq number is less than or equal to the given seq_num
    void stop_timers_up_to(uint32_t ack_num);
};
