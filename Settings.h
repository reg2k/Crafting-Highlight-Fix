#pragma once

namespace Settings
{
    using std::string;

    int     GetInt(std::string settingName, int defaultValue = 0);
    bool    GetBool(std::string settingName, bool defaultValue = false);
    float   GetFloat(std::string settingName, float defaultValue = 0.0f);
    string  GetString(std::string settingName, string defaultValue = "");
}