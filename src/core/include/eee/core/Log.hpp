#pragma once

#include <string_view>

namespace eee::core {

enum class LogLevel { Debug, Info, Warning, Error };

void log(LogLevel level, std::string_view message);
void logf(LogLevel level, const char* format, ...);

inline void logInfo(std::string_view message) { log(LogLevel::Info, message); }
inline void logWarning(std::string_view message) { log(LogLevel::Warning, message); }
inline void logError(std::string_view message) { log(LogLevel::Error, message); }

} // namespace eee::core
