#pragma once

#include "core/types.h"

#include <chrono>

namespace core {

class Clock {
public:
    Clock();

    f64 restart();
    f64 elapsed() const;

private:
    std::chrono::steady_clock::time_point m_last;
};

class FixedTimestepAccumulator {
public:
    explicit FixedTimestepAccumulator(f64 fixedDeltaSeconds, f64 maxFrameTime = 0.25);

    void addFrameTime(f64 frameSeconds);
    bool consumeStep();

    f64 fixedDeltaSeconds() const { return m_fixedDelta; }
    f64 accumulator() const { return m_accumulator; }

private:
    f64 m_fixedDelta;
    f64 m_maxFrameTime;
    f64 m_accumulator = 0.0;
};

} // namespace core
