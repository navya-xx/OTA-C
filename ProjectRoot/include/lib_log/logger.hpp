#ifndef LOG_HPP
#define LOG_HPP

#pragma once
#include <iostream>
#include <fstream>
#include <iostream>
#include <string>
#include <mutex>
#include <iomanip>
#include <boost/format.hpp>

#include "log_levels.hpp"
#include "log_exception.hpp"

class Logger
{
public:
    static Logger &getInstance();
    void initialize(const std::string &filename);
    void setLogLevel(LogLevel level);

    void log(LogLevel level, const std::string &message);

    template <typename... Args>
    void log(LogLevel level, const std::string &fmt, Args &&...args);

    void logIntoBuffer(const std::string &message);

    template <typename... Args>
    void logIntoBuffer(const std::string &fmt, Args &&...args);

    void flushBuffer(LogLevel level);

    std::string LogLevelsToString(LogLevel LogLevels);
    std::string getLogLevelColor(LogLevel LogLevels);
    std::string resetLogLevelColor();

private:
    Logger() : logLevel(LogLevel::INFO) {}
    ~Logger();
    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;

    std::ofstream logFile;
    LogLevel logLevel;
    std::mutex logMutex;

    void handleError(const std::string &message);
    std::ostringstream buffer;

    std::string currentDateTime();
};

template <typename... Args>
void Logger::log(LogLevel level, const std::string &message, Args &&...args)
{
    // Create boost::format object with the message
    boost::format formatter(message);

    // Apply arguments to the formatter
    (formatter % ... % std::forward<Args>(args));

    // Convert formatted string to std::string
    std::string formattedMessage = boost::str(formatter);

    log(level, formattedMessage);
}

template <typename... Args>
void Logger::logIntoBuffer(const std::string &message, Args &&...args)
{
    // Create boost::format object with the message
    boost::format formatter(message);

    // Apply arguments to the formatter
    (formatter % ... % std::forward<Args>(args));

    // Convert formatted string to std::string
    std::string formattedMessage = boost::str(formatter);

    buffer << formattedMessage;
}

#endif // LOG_HPP