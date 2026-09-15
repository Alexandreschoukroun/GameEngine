#include "core/log.h"

#include <cstdio>
#include <ctime>

namespace core {
namespace {

const char* levelLabel(LogLevel level) {
    switch (level) {
        case LogLevel::Info: return "INFO ";
        case LogLevel::Warn: return "WARN ";
        case LogLevel::Error: return "ERROR";
    }
    return "?????";
}

std::tm localTimeNow() {
    const std::time_t now = std::time(nullptr);
    std::tm result{};
#if defined(_WIN32)
    localtime_s(&result, &now);
#else
    localtime_r(&now, &result);
#endif
    return result;
}

} // namespace

void log(LogLevel level, std::string_view message) {
    const std::tm t = localTimeNow();
    char timeBuf[16];
    std::snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);

    std::FILE* stream = (level == LogLevel::Error) ? stderr : stdout;
    std::fprintf(stream, "[%s] %s | %.*s\n", timeBuf, levelLabel(level),
                  static_cast<int>(message.size()), message.data());
}

} // namespace core
