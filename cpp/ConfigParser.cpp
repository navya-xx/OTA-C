#include "ConfigParser.hpp"

ConfigParser::ConfigParser(const std::string &filename)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "Error: Unable to open file " << filename << std::endl;
        return;
    }

    std::string line;
    while (std::getline(file, line))
    {
        std::istringstream iss(line);
        std::string varName, varValue, varType;

        if (iss >> varName >> varValue >> varType)
        {
            if (varType == "int")
                int_data[varName] = static_cast<size_t>(std::stoi(varValue));
            else if (varType == "float")
                float_data[varName] = static_cast<float>(std::stof(varValue));
            else if (varType == "str" || varType == "string")
                string_data[varName] = varValue;
            else
                std::cerr << "Error : Unable to determine the type of variable " << varName << "." << std::endl;
        }
    }

    file.close();
}

bool ConfigParser::is_save_buffer()
{
    const std::string save_ref_rx = getValue_str("save-ref-rx");
    if (save_ref_rx == "NO")
    {
        return false;
    }
    else
    {
        std::string device_id = getValue_str("args");
        for (char &ch : device_id)
        {
            if (ch == '=')
            {
                ch = '_'; // Replace '=' with '_'
            }
        }

        save_buffer_filename = "/OTA-C/cpp/storage/save_ref_rx_" + device_id + ".dat";
        return true;
    }
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
        std::cerr << "Error: Variable '" << varName << "' not found in config" << std::endl;
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
        std::cerr << "Error: Variable '" << varName << "' not found in config" << std::endl;
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
        std::cerr << "Error: Variable '" << varName << "' not found in config" << std::endl;
    }
    return float();
}

void ConfigParser::set_value(const std::string &varname, const std::string &varval, const std::string &vartype)
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
            std::string errMsg = "Fail to convert value" + varval + " to size_t.";
            throw std::runtime_error(errMsg);
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
            std::string errMsg = "Fail to convert value" + varval + " to float.";
            throw std::runtime_error(errMsg);
        }
    }
    else
    {
        std::string errMsg = "Invalid vartype : '" + vartype + "', only allowed (str, int, float).";
        throw std::invalid_argument(errMsg);
    }
}

void ConfigParser::print_values()
{
    std::cout << "Config values:" << std::endl;
    for (const auto &pair : string_data)
    {
        std::cout << pair.first << " = " << pair.second << std::endl;
    }

    for (const auto &pair : int_data)
    {
        std::cout << pair.first << " = " << pair.second << std::endl;
    }

    for (const auto &pair : float_data)
    {
        std::cout << pair.first << " = " << pair.second << std::endl;
    }
}