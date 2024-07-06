#include "logger.hpp"

Logger::~Logger()
{
    if (logFile.is_open())
    {
        logFile.close();
    }
}

Logger &Logger::getInstance()
{
    static Logger instance;
    return instance;
}

void Logger::initialize(const std::string &fileName)
{
    std::lock_guard<std::mutex> guard(logMutex);
    if (logFile.is_open())
    {
        logFile.close();
    }
    logFile.open(fileName, std::ios::out | std::ios::app);
    if (!logFile)
    {
        throw LoggerException("Unable to open log file: " + fileName);
    }
}

void Logger::setLogLevel(LogLevel level)
{
    std::lock_guard<std::mutex> guard(logMutex);
    logLevel = level;
}

void Logger::log(LogLevel level, const std::string &message)
{
    std::lock_guard<std::mutex> guard(logMutex);
    std::string logMessage = currentDateTime() + " [" + LogLevelsToString(level) + "] " + message;
    if (level >= logLevel)
    {
        if (logFile.is_open())
        {
            logFile << logMessage << std::endl;
        }
        std::cout << getLogLevelColor(level) << logMessage << resetLogLevelColor() << std::endl; // Also print to console

        if (level == LogLevel::ERROR)
        {
            handleError(message);
        }
    }
}

void Logger::logIntoBuffer(const std::string &message)
{
    std::lock_guard<std::mutex> guard(logMutex);
    buffer << message;
}

std::string Logger::currentDateTime()
{
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto now_tm = *std::localtime(&time_t_now);

    std::ostringstream oss;
    oss << std::put_time(&now_tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

void Logger::handleError(const std::string &message)
{
    std::lock_guard<std::mutex> guard(logMutex);
    try
    {
        throw LoggerException(message);
    }
    catch (const LoggerException &e)
    {
        std::string errorMsg = "Caught LoggerException: " + std::string(e.what());
        if (logFile.is_open())
        {
            logFile << errorMsg << std::endl;
        }
        std::cerr << errorMsg << std::endl; // Also print to console
        std::exit(EXIT_FAILURE);
    }
}

void Logger::flushBuffer(LogLevel level)
{
    std::string message = buffer.str();
    log(level, message);
    buffer.str(""); // Clear the buffer
}

std::string Logger::LogLevelsToString(LogLevel LogLevels)
{
    switch (LogLevels)
    {
    case LogLevel::ERROR:
        return "ERROR";
    case LogLevel::WARN:
        return "WARN";
    case LogLevel::INFO:
        return "INFO";
    case LogLevel::DEBUG:
        return "DEBUG";
    default:
        return "UNKNOWN";
    }
}

std::string Logger::getLogLevelColor(LogLevel LogLevels)
{
    switch (LogLevels)
    {
    case LogLevel::ERROR:
        return "\033[31m"; // Red
    case LogLevel::WARN:
        return "\033[33m"; // Yellow
    case LogLevel::INFO:
        return "\033[32m"; // Green
    case LogLevel::DEBUG:
        return "\033[34m"; // Blue
    default:
        return "\033[0m"; // Reset
    }
}

std::string Logger::resetLogLevelColor()
{
    return "\033[0m"; // Reset color
}