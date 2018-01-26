#include "Settings.h"

#include "Config.h"

#include <string>

namespace Settings {

    std::string Get_Internal(std::string settingNameAndSection) {
        auto delimiter = settingNameAndSection.find_first_of(':');
        std::string settingNameStr     = settingNameAndSection.substr(0, delimiter);
        std::string settingSectionStr  = settingNameAndSection.substr(delimiter + 1);

        char value[1024];
        GetPrivateProfileString(settingSectionStr.c_str(), settingNameStr.c_str(), NULL, value, sizeof(value), INI_LOCATION_USER);

        if (strlen(value) == 0) {
            GetPrivateProfileString(settingSectionStr.c_str(), settingNameStr.c_str(), NULL, value, sizeof(value), INI_LOCATION_DEFAULTS);
        }

        return std::string(value);
    }

    int GetInt(std::string settingName, int defaultValue) {
        auto value = Get_Internal(settingName);
        return (value != "") ? std::stoi(value) : defaultValue;
    }

    bool GetBool(std::string settingName, bool defaultValue) {
        auto value = Get_Internal(settingName);
        return (value != "") ? (std::stoi(value) != 0) : defaultValue;
    }

    float GetFloat(std::string settingName, float defaultValue) {
        auto value = Get_Internal(settingName);
        return (value != "") ? std::stof(value) : defaultValue;
    }

    std::string GetString(std::string settingName, std::string defaultValue) {
        auto value = Get_Internal(settingName);
        return (value != "") ? value : defaultValue;
    }
}