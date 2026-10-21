#include "eee/core/Log.hpp"

#include <cstdarg>
#include <cstdio>

namespace eee::core {

void log(LogLevel level, std::string_view message) {
    const char* tag = "INFO";
    switch (level) {
    case LogLevel::Debug: tag = "DEBUG"; break;
    case LogLevel::Info: tag = "INFO"; break;
    case LogLevel::Warning: tag = "WARN"; break;
    case LogLevel::Error: tag = "ERROR"; break;
    }
    std::fprintf(level == LogLevel::Error ? stderr : stdout, "[eee][%s] %.*s\n", tag,
                 static_cast<int>(message.size()), message.data());
    std::fflush(level == LogLevel::Error ? stderr : stdout);
}

void logf(LogLevel level, const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    log(level, buffer);
}

} // namespace eee::core
