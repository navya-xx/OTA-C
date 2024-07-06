#ifndef LOG_MACROS
#define LOG_MACROS

#include "logger.hpp"

#define LOG_DEBUG(message) Logger::getInstance().log(LogLevel::DEBUG, message)
#define LOG_INFO(message) Logger::getInstance().log(LogLevel::INFO, message)
#define LOG_WARN(message) Logger::getInstance().log(LogLevel::WARN, message)
#define LOG_ERROR(message) Logger::getInstance().log(LogLevel::ERROR, message)

#define LOG_DEBUG_FMT(message, ...) Logger::getInstance().log(LogLevel::DEBUG, message, ##__VA_ARGS__)
#define LOG_INFO_FMT(message, ...) Logger::getInstance().log(LogLevel::INFO, message, ##__VA_ARGS__)
#define LOG_WARN_FMT(message, ...) Logger::getInstance().log(LogLevel::WARN, message, ##__VA_ARGS__)
#define LOG_ERROR_FMT(message, ...) Logger::getInstance().log(LogLevel::ERROR, message, ##__VA_ARGS__)

#define LOG_INTO_BUFFER(message) Logger::getInstance().logIntoBuffer(message)
#define LOG_INTO_BUFFER_FMT(message, ...) Logger::getInstance().logIntoBuffer(message, ##__VA_ARGS__)

#define LOG_FLUSH_DEBUG() Logger::getInstance().flushBuffer(LogLevel::DEBUG)
#define LOG_FLUSH_INFO() Logger::getInstance().flushBuffer(LogLevel::INFO)
#define LOG_FLUSH_WARN() Logger::getInstance().flushBuffer(LogLevel::WARN)
#define LOG_FLUSH_ERROR() Logger::getInstance().flushBuffer(LogLevel::ERROR)

#endif // LOG_MACROS