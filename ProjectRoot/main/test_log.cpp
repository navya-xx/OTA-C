#include "pch.hpp"

#include "log_macros.hpp"

int main()
{
    try
    {
        Logger::getInstance().initialize("log.txt");
        Logger::getInstance().setLogLevel(LogLevel::DEBUG);

        double pi = 3.14159265358979323846;
        int count = 10;

        // Example usage of LOG_INFO_FMT macro
        LOG_INFO_FMT("Pi value is %.4f, count is %d", pi, count);
    }
    catch (const LoggerException &e)
    {
        std::cerr << "LoggerException caught: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Standard exception caught: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    catch (...)
    {
        std::cerr << "Unknown exception caught" << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}