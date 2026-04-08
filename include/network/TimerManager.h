#pragma once
#include <functional>
#include <map>
#include <uv.h>
struct TimerManager {
private:
    uv_loop_t* loop_;
    std::map<uint32_t, uv_timer_t*> timers_;
public:
    explicit TimerManager(uv_loop_t* loop) : loop_(loop) {}

    TimerManager(const TimerManager&) = delete;
    TimerManager& operator=(const TimerManager&) = delete;

    ~TimerManager();

    // start timer
    void start_timer(uint32_t seq_num, uint64_t timeout_ms, std::function<void()> callback);

    // stop timer
    void stop_timer(uint32_t seq_num);

};