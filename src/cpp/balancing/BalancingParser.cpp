#include "include/antares-xpansion/balancing/BalancingParser.h"

#include <stdexcept>

/// @brief Check if investment is possible for the area
/// @return true if investment is possible, false otherwise
bool AreaInvestment::isInvestmentPossible() const
{
    return std::ranges::any_of(investmentCandidates,
                               [](const auto& entry)
                               {
                                   return entry.second.currentDispatchableProductionValue
                                          < entry.second.candidateParams->expansionPotential;
                               });
}

bool AreaInvestment::isDecommissioningPossible() const
{
    return std::ranges::any_of(decommissioningCandidates,
                               [](const auto& entry)
                               {
                                   return entry.second.currentDispatchableProductionValue
                                          < entry.second.candidateParams->expansionPotential;
                               });
}

/// @brief Check if disinvestment is possible for the area
/// @return true if disinvestment is possible, false otherwise
bool AreaInvestment::isDisinvestmentPossible() const
{
    return std::ranges::any_of(investmentCandidates,
                               [](const auto& entry)
                               { return entry.second.currentDispatchableProductionValue > 0; });
}

/// @brief Check if recommissioning is possible for the area
/// @return true if recommissioning is possible, false otherwise
bool AreaInvestment::isRecommissioningPossible() const
{
    return std::ranges::any_of(decommissioningCandidates,
                               [](const auto& entry)
                               { return entry.second.currentDispatchableProductionValue > 0; });
}

/// @brief Constructor of the BalancingParser class
/// @param pathToYamlConfigFile The path to the YAML configuration file
BalancingParser::BalancingParser(const std::filesystem::path& pathToYamlConfigFile):
    pathToYamlConfigFile(pathToYamlConfigFile),
    reliabilityStandardDeadBandUp(0.0),
    reliabilityStandardDeadBandDown(0.0)
{
    if (!pathToYamlConfigFile.empty())
    {
        if (!std::filesystem::exists(pathToYamlConfigFile))
        {
            throw std::runtime_error("YAML config file does not exist: "
                                     + pathToYamlConfigFile.string());
        }
        parse();
    }
}

/// @brief Parse the YAML configuration file and fill the BalancingParser attributes with the parsed
/// data
void BalancingParser::parse()
{
    try
    {
        config = YAML::LoadFile(pathToYamlConfigFile.string());

        parseGlobalSettings();
        parseDecommissioningCandidatesTypes();
        parseInvestmentCandidatesTypes();
        parseAreas();
    }
    catch (const YAML::Exception& e)
    {
        throw std::runtime_error("YAML parsing error: " + std::string(e.what()));
    }
}

/// @brief Parse the global settings from the YAML configuration file : reliability standard dead
/// bands and indicator
void BalancingParser::parseGlobalSettings()
{
    if (config["reliability_standard_dead_band_up"])
    {
        reliabilityStandardDeadBandUp = config["reliability_standard_dead_band_up"].as<double>();
    }

    if (config["reliability_standard_dead_band_down"])
    {
        reliabilityStandardDeadBandDown = config["reliability_standard_dead_band_down"]
                                            .as<double>();
    }

    if (config["reliability_standard_indicator"])
    {
        auto criterionStr = config["reliability_standard_indicator"].as<std::string>();
        if (criterionStr == "LOLE")
        {
            criterion = Benders::Criterion::Type::PositiveUnsuppliedEnergy;
        }
        else if (criterionStr == "NPCAP_HOURS")
        {
            criterion = Benders::Criterion::Type::NearPriceCapHours;
        }
        else
        {
            throw std::runtime_error("YAML parsing error: " + criterionStr
                                     + " is not a correct criterion (LOLE or NPCAP_HOURS)");
        }
    }
}

/// @brief Parse the decommissioning candidates types from the YAML configuration file : fixed O&M
/// costs
void BalancingParser::parseDecommissioningCandidatesTypes()
{
    if (!config["decommissioning_candidates_types"])
    {
        return;
    }

    for (const auto& typeNode: config["decommissioning_candidates_types"])
    {
        std::string typeName = typeNode.first.as<std::string>();

        if (!typeNode.second["fixed_om_costs"])
        {
            throw std::runtime_error("Missing 'fixed_om_costs' for decommissioning type: "
                                     + typeName);
        }

        auto type = std::make_shared<Decommissioning>();
        type->fixedOmCosts = typeNode.second["fixed_om_costs"].as<double>();

        decommissioningCandidatesTypes[typeName] = std::move(type);
    }
}

/// @brief Parse the investment candidates types from the YAML configuration file : derating,
/// expansion potential, investment cost and fixed O&M costs
void BalancingParser::parseInvestmentCandidatesTypes()
{
    if (!config["investment_candidates_types"])
    {
        return;
    }

    for (const auto& typeNode: config["investment_candidates_types"])
    {
        std::string typeName = typeNode.first.as<std::string>();
        YAML::Node typeData = typeNode.second;

        auto type = std::make_shared<Investment>();

        if (!typeData["derating"])
        {
            throw std::runtime_error("Missing 'derating' for investment type: " + typeName);
        }
        type->derating = typeData["derating"].as<double>();

        if (!typeData["expansion_potential"])
        {
            throw std::runtime_error("Missing 'expansion_potential' for investment type: "
                                     + typeName);
        }
        type->expansionPotential = typeData["expansion_potential"].as<double>();

        if (!typeData["investment_cost"])
        {
            throw std::runtime_error("Missing 'investment_cost' for investment type: " + typeName);
        }
        type->investmentCost = typeData["investment_cost"].as<double>();

        if (!typeData["fixed_om_costs"])
        {
            throw std::runtime_error("Missing 'fixed_om_costs' for investment type: " + typeName);
        }
        type->fixedOmCosts = typeData["fixed_om_costs"].as<double>();

        investmentCandidatesTypes[typeName] = std::move(type);
    }
}

/// @brief Parse the areas from the YAML configuration file : reliability standard, decommissioning
/// and investment increments, candidates to type mapping
void BalancingParser::parseAreas()
{
    if (!config["areas"])
    {
        return;
    }

    for (const auto& areaNode: config["areas"])
    {
        std::string areaName = areaNode.first.as<std::string>();
        YAML::Node areaData = areaNode.second;

        AreaInvestment area;

        if (!areaData["reliability_standard"])
        {
            throw std::runtime_error("Missing 'reliability_standard' for area: " + areaName);
        }
        area.reliabilityStandard = areaData["reliability_standard"].as<double>();

        if (!areaData["decommissioning_increment"])
        {
            throw std::runtime_error("Missing 'decommissioning_increment' for area: " + areaName);
        }
        area.decommissioningIncrement = areaData["decommissioning_increment"].as<double>();
        area.currentDecommissioningIncrement = area.decommissioningIncrement;

        if (!areaData["investment_increment"])
        {
            throw std::runtime_error("Missing 'investment_increment' for area: " + areaName);
        }
        area.investmentIncrement = areaData["investment_increment"].as<double>();
        area.currentInvestmentIncrement = area.investmentIncrement;

        area.reliabilityStandardDeadBandUp = areaData["reliability_standard_dead_band_up"]
                                               ? areaData["reliability_standard_dead_band_up"]
                                                   .as<double>()
                                               : reliabilityStandardDeadBandUp;

        area.reliabilityStandardDeadBandDown = areaData["reliability_standard_dead_band_down"]
                                                 ? areaData["reliability_standard_dead_band_down"]
                                                     .as<double>()
                                                 : reliabilityStandardDeadBandDown;

        if (areaData["decommissioning_candidates_to_type"])
        {
            for (const auto& candidate: areaData["decommissioning_candidates_to_type"])
            {
                const std::string candidate_name = candidate.first.as<std::string>();
                const std::string typeName = candidate.second.as<std::string>();

                auto it = decommissioningCandidatesTypes.find(typeName);
                if (it == decommissioningCandidatesTypes.end())
                {
                    throw std::runtime_error("Unknown decommissioning type '" + typeName
                                             + "' for candidate '" + candidate_name + "' in area '"
                                             + areaName + "'");
                }

                area.decommissioningCandidates[candidate_name].candidateParams = it->second;
            }
        }

        if (areaData["investment_candidates_to_type"])
        {
            for (const auto& candidate: areaData["investment_candidates_to_type"])
            {
                const std::string candidate_name = candidate.first.as<std::string>();
                const std::string typeName = candidate.second.as<std::string>();

                auto it = investmentCandidatesTypes.find(typeName);
                if (it == investmentCandidatesTypes.end())
                {
                    throw std::runtime_error("Unknown investment type '" + typeName
                                             + "' for candidate '" + candidate_name + "' in area '"
                                             + areaName + "'");
                }

                area.investmentCandidates[candidate_name] = {it->second, 0};
            }
        }

        areaInvestments[areaName] = std::move(area);
    }
}

/// @brief Get the reliability standard dead band up value
/// @return The reliability standard dead band up value
double BalancingParser::getReliabilityStandardDeadBandUp() const
{
    return reliabilityStandardDeadBandUp;
}

/// @brief Get the reliability standard dead band down value
/// @return The reliability standard dead band down value
double BalancingParser::getReliabilityStandardDeadBandDown() const
{
    return reliabilityStandardDeadBandDown;
}

/// @brief Get the reliability standard indicator
/// @return The reliability standard indicator
Benders::Criterion::Type BalancingParser::getReliabilityStandardIndicator() const
{
    return criterion;
}

/// @brief Get the areas with their investment parameters
/// @return The areas with their investment parameters
const std::map<std::string, AreaInvestment>& BalancingParser::getAreas() const
{
    return areaInvestments;
}

/// @brief Check if the area with the given name is defined in the configuration file
/// @param areaName The name of the area to check
/// @return true if the area is defined in the configuration file, false otherwise
bool BalancingParser::hasArea(const std::string& areaName) const
{
    return areaInvestments.find(areaName) != areaInvestments.end();
}

/// @brief Get the area investment parameters for the area with the given name
/// @param areaName The name of the area to get the parameters for
/// @return A pointer to the area investment parameters if the area is defined in the configuration
const AreaInvestment* BalancingParser::getArea(const std::string& areaName) const
{
    auto it = areaInvestments.find(areaName);
    if (it != areaInvestments.end())
    {
        return &(it->second);
    }
    return nullptr;
}
