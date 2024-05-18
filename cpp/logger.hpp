#ifndef LOG_HPP
#define LOG_HPP
#pragma once
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <mutex>

class Logger
{
public:
    enum class Level
    {
        ERROR,
        WARNING,
        INFO,
        DEBUG
    };

    Logger(const std::string &filename, Level level = Level::INFO, bool toConsole = false)
        : logLevel(level), logFile(filename, std::ios_base::app), logToConsole(toConsole)
    {
        if (!logFile.is_open())
        {
            throw std::runtime_error("Unable to open log file");
        }
    }

    ~Logger()
    {
        if (logFile.is_open())
        {
            logFile.close();
        }
    }

    void setLevel(Level level)
    {
        logLevel = level;
    }

    void setLogToConsole(bool toConsole)
    {
        logToConsole = toConsole;
    }

    void log(Level level, const std::string &message)
    {
        if (level <= logLevel)
        {
            std::lock_guard<std::mutex> lock(logMutex);
            std::string logMessage = currentDateTime() + " [" + levelToString(level) + "] " + message;
            logFile << logMessage << std::endl;
            if (logToConsole)
            {
                std::cout << getColor(level) << logMessage << resetColor() << std::endl;
            }
        }
    }

private:
    Level logLevel;
    std::ofstream logFile;
    bool logToConsole;
    std::mutex logMutex;

    std::string currentDateTime()
    {
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        auto now_tm = *std::localtime(&time_t_now);

        std::ostringstream oss;
        oss << std::put_time(&now_tm, "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }

    std::string levelToString(Level level)
    {
        switch (level)
        {
        case Level::ERROR:
            return "ERROR";
        case Level::WARNING:
            return "WARNING";
        case Level::INFO:
            return "INFO";
        case Level::DEBUG:
            return "DEBUG";
        default:
            return "UNKNOWN";
        }
    }

    std::string getColor(Level level)
    {
        switch (level)
        {
        case Level::ERROR:
            return "\033[31m"; // Red
        case Level::WARNING:
            return "\033[33m"; // Yellow
        case Level::INFO:
            return "\033[32m"; // Green
        case Level::DEBUG:
            return "\033[34m"; // Blue
        default:
            return "\033[0m"; // Reset
        }
    }

    std::string resetColor()
    {
        return "\033[0m"; // Reset color
    }
};

#endif // LOG_HPP
