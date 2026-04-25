#pragma once
#include <functional>
#include <map>
#include <memory>
#include <uv.h>

struct TimerManager {
private:
    std::map<uint32_t, uv_timer_t *> timers_;
    // todo: we need to resolve zero window problem

public:
    explicit TimerManager(uv_loop_t *loop) : loop_(loop) {
        _2msl_timer = std::make_unique<uv_timer_t>();
        uv_timer_init(loop_, _2msl_timer.get());
        _2msl_timer->data = nullptr;
    }

    TimerManager(const TimerManager &) = delete;

    ~TimerManager();

    // start timer
    void start_timer(uint32_t seq_num, uint64_t timeout_ms, std::function<void()> callback);

    // stop timer
    void stop_timer(uint32_t seq_num);

    // stop all timers whose seq number is less than or equal to the given seq_num
    void stop_timers_up_to(uint32_t ack_num);

    // stop all timers
    void stop_all_timers();

    // drop timer
    void drop_timer(uint32_t seq_num);

    void drop_timer_up_to(uint32_t ack_num);

    // drop all timers
    void drop_all_timers();

    std::unique_ptr<uv_timer_t> _2msl_timer;
    uv_loop_t *loop_;

};
