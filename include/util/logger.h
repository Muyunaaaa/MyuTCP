#pragma once
#include "spdlog/spdlog.h"

#ifdef MYU_ENABLE_DEBUG
    #define MYU_LOG_INFO(...)  spdlog::info(__VA_ARGS__)
    #define MYU_LOG_WARN(...)  spdlog::warn(__VA_ARGS__)
    #define MYU_LOG_ERROR(...) spdlog::error(__VA_ARGS__)
#else
    #define MYU_LOG_INFO(...)  (void)0
    #define MYU_LOG_WARN(...)  (void)0
    #define MYU_LOG_ERROR(...) (void)0
#endif