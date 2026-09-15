#include "core/time.h"

namespace core {

Clock::Clock() : m_last(std::chrono::steady_clock::now()) {}

f64 Clock::elapsed() const {
    const auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<f64>(now - m_last).count();
}

f64 Clock::restart() {
    const auto now = std::chrono::steady_clock::now();
    const f64 dt = std::chrono::duration<f64>(now - m_last).count();
    m_last = now;
    return dt;
}

FixedTimestepAccumulator::FixedTimestepAccumulator(f64 fixedDeltaSeconds, f64 maxFrameTime)
    : m_fixedDelta(fixedDeltaSeconds), m_maxFrameTime(maxFrameTime) {}

void FixedTimestepAccumulator::addFrameTime(f64 frameSeconds) {
    if (frameSeconds > m_maxFrameTime) {
        frameSeconds = m_maxFrameTime;
    }
    m_accumulator += frameSeconds;
}

bool FixedTimestepAccumulator::consumeStep() {
    if (m_accumulator >= m_fixedDelta) {
        m_accumulator -= m_fixedDelta;
        return true;
    }
    return false;
}

} // namespace core
