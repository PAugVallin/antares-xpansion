#pragma once
#include <filesystem>
#include <string>

#include "antares-xpansion/config_reader/ConfigReader.h"

class SettingsConfigReader: ConfigReader
{
public:
    SettingsConfigReader(const std::filesystem::path& pathToYamlConfigFile = "")
    {
        emplaceAllElements(); // this method must be defined in the derived class
        initializeAllElements(pathToYamlConfigFile); // this method comes from the base class
    }

    std::string getSolver() const
    {
        return getValueFromKey<std::string>(solverKey);
    }

    std::string getVerbosity() const
    {
        return getValueFromKey<std::string>(verbosityKey);
    }

    bool getCacheProblems() const
    {
        return getValueFromKey<bool>(cacheProblemsKey);
    }

private:
    void emplaceAllElements() override;

    // keys from YAML file:
    inline static const std::string solverKey = "solver", verbosityKey = "verbosity",
                                    cacheProblemsKey = "cache_problems";
};
