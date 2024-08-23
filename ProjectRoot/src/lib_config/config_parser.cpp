#include "config_parser.hpp"

ConfigParser::ConfigParser(const std::string &filename)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        LOG_ERROR_FMT("Unable to open config file %1%.", filename);
    }

    std::string line;
    while (std::getline(file, line))
    {
        // Trim whitespace from the start and end of the line
        trim(line);
        // Ignore empty lines and comments
        if (line.empty() || line[0] == '#')
        {
            continue;
        }

        std::istringstream iss(line);
        std::string varName, varValue, varType, description;

        if (iss >> varName >> varValue >> varType)
        {
            if (varType == "int")
                int_data[varName] = static_cast<size_t>(std::stoi(varValue));
            else if (varType == "float")
                float_data[varName] = static_cast<float>(std::stof(varValue));
            else if (varType == "str" || varType == "string")
                string_data[varName] = varValue;
            else
            {
                LOG_INFO_FMT("Unable to determine the type of variable %1%. Continue as string.", varName);
                string_data[varName] = varValue;
            }
            std::getline(iss, description); // Read the rest of the line as description
            trim(description);
            desc_data[varName] = description;
        }
        else
        {
            continue;
        }
    }

    file.close();
}

// Utility function to trim whitespace from both ends of a string
void ConfigParser::trim(std::string &str)
{
    const char *whitespace = " \t\n\r\f\v";
    str.erase(str.find_last_not_of(whitespace) + 1);
    str.erase(0, str.find_first_not_of(whitespace));
}

std::string ConfigParser::getValue_str(const std::string &varName)
{
    auto it = string_data.find(varName);
    if (it != string_data.end())
    {
        return it->second;
    }
    else
    {
        LOG_ERROR_FMT("Variable '%1%' not found in config.", varName);
    }
    return std::string();
}

size_t ConfigParser::getValue_int(const std::string &varName)
{
    auto it = int_data.find(varName);
    if (it != int_data.end())
    {
        return it->second;
    }
    else
    {
        LOG_ERROR_FMT("Variable '%1%' not found in config.", varName);
    }
    return size_t();
}

float ConfigParser::getValue_float(const std::string &varName)
{
    auto it = float_data.find(varName);
    if (it != float_data.end())
    {
        return it->second;
    }
    else
    {
        LOG_ERROR_FMT("Variable '%1%' not found in config.", varName);
    }
    return float();
}

void ConfigParser::set_value(const std::string &varname, const std::string &varval, const std::string &vartype, const std::string &desc)
{
    if (vartype == "str" or vartype == "string")
        string_data[varname] = varval;
    else if (vartype == "int")
    {
        try
        {
            int_data[varname] = static_cast<size_t>(std::stoi(varval));
        }
        catch (const std::exception &e)
        {
            LOG_ERROR_FMT("Fail to convert value '%1%' to size_t.", varval);
        }
    }
    else if (vartype == "float")
    {
        try
        {
            float_data[varname] = std::stof(varval);
        }
        catch (const std::exception &e)
        {
            LOG_ERROR_FMT("Fail to convert value '%1%' to float.", varval);
        }
    }
    else
    {
        LOG_ERROR_FMT("Invalid vartype '%1%', only allowed (str, int, float).", vartype);
    }

    desc_data[varname] = desc;
}

void ConfigParser::print_values()
{
    LOG_INFO("Config Values:");
    for (const auto &pair : string_data)
        LOG_INFO(boost::str(boost::format("%1% %2% %3%") % boost::io::group(std::left, std::setw(30), pair.first) % boost::io::group(std::left, std::setw(10), pair.second) % boost::io::group(std::left, std::setw(80), desc_data[pair.first])));

    for (const auto &pair : int_data)
        LOG_INFO(boost::str(boost::format("%1% %2% %3%") % boost::io::group(std::left, std::setw(30), pair.first) % boost::io::group(std::left, std::setw(10), pair.second) % boost::io::group(std::left, std::setw(80), desc_data[pair.first])));

    for (const auto &pair : float_data)
        LOG_INFO(boost::str(boost::format("%1% %2% %3%") % boost::io::group(std::left, std::setw(30), pair.first) % boost::io::group(std::left, std::setw(10), pair.second) % boost::io::group(std::left, std::setw(80), desc_data[pair.first])));
}

std::string ConfigParser::print_json()
{
    json json_data = json::array();

    for (const auto &pair : string_data)
    {
        json_data.push_back({{"name", pair.first}, {"value", pair.second}, {"desc", desc_data[pair.first]}});
    }

    for (const auto &pair : int_data)
    {
        json_data.push_back({{"name", pair.first}, {"value", pair.second}, {"desc", desc_data[pair.first]}});
    }

    for (const auto &pair : float_data)
    {
        json_data.push_back({{"name", pair.first}, {"value", pair.second}, {"desc", desc_data[pair.first]}});
    }

    json final_config;
    final_config["config"] = json_data;

    std::string jsonString = final_config.dump(4);
    return jsonString;
}