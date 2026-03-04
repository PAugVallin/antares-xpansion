#pragma once
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "antares-xpansion/benders/benders_core/CriterionInputDataReader.h"
#include "yaml-cpp/yaml.h"
enum class CriterionState
{
    LOWER,
    VALID,
    HIGHER,
    UNINITIALIZED,
};

constexpr std::string_view to_string(CriterionState state)
{
    switch (state)
    {
    case CriterionState::LOWER:
        return "LOWER";
    case CriterionState::VALID:
        return "VALID";
    case CriterionState::HIGHER:
        return "HIGHER";
    case CriterionState::UNINITIALIZED:
        return "UNINITIALIZED";
    }
}

struct Investment
{
    double derating;
    double expansionPotential;
    double investmentCost;
    double fixedOmCosts;
};

struct Decommissioning
{
    double fixedOmCosts;
};

template<typename Type>
struct Candidate
{
    std::shared_ptr<Type> candidateParams;
    double currentDispatchableProductionValue;
};

struct AreaInvestment
{
    double reliabilityStandard;
    double reliabilityStandardDeadBandUp;
    double reliabilityStandardDeadBandDown;
    double decommissioningIncrement;
    double currentDecommissioningIncrement;
    double investmentIncrement;
    double currentInvestmentIncrement;
    std::map<std::string, Candidate<Decommissioning>> decommissioningCandidates;
    std::map<std::string, Candidate<Investment>> investmentCandidates;
    CriterionState oldCriterionState{CriterionState::UNINITIALIZED};

    bool isDisinvestmentPossible() const;
    bool isRecommissioningPossible() const;
};

class BalancingParser
{
public:
    BalancingParser(const std::filesystem::path& pathToYamlConfigFile = "");

    void parse();

    double getReliabilityStandardDeadBandUp() const;
    double getReliabilityStandardDeadBandDown() const;
    Benders::Criterion::Type getReliabilityStandardIndicator() const;

    const std::map<std::string, AreaInvestment>& getAreas() const;
    bool hasArea(const std::string& areaName) const;
    const AreaInvestment* getArea(const std::string& areaName) const;

    std::map<std::string, AreaInvestment> areaInvestments;

private:
    std::filesystem::path pathToYamlConfigFile;
    YAML::Node config;

    double reliabilityStandardDeadBandUp;
    double reliabilityStandardDeadBandDown;
    Benders::Criterion::Type criterion;

    std::map<std::string, std::shared_ptr<Decommissioning>> decommissioningCandidatesTypes;
    std::map<std::string, std::shared_ptr<Investment>> investmentCandidatesTypes;

    void parseGlobalSettings();
    void parseAreas();
    void parseDecommissioningCandidatesTypes();
    void parseInvestmentCandidatesTypes();
};
