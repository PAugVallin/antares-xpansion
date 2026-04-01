#pragma once

#include <string>

#include "ConfigurationManager.h"
#include "antares-xpansion/balancing/BalancingParser.h"
#include "antares-xpansion/evaluator/GreedyBalancingFinder.h"
#include "antares-xpansion/lpnamer/main/ProblemGenerationOptimSimu.h"
#include "antares-xpansion/lpnamer/model/Problem.h"

using AreaCluster = std::pair<std::string, std::string>;

enum class CapacityAction
{
    INVESTMENT,
    DISINVESTMENT,
    DECOMMISSIONING,
    RECOMMISSIONING
};

constexpr std::string_view to_string(CapacityAction action)
{
    switch (action)
    {
    case CapacityAction::INVESTMENT:
        return "INVESTMENT";
    case CapacityAction::DISINVESTMENT:
        return "DISINVESTMENT";
    case CapacityAction::DECOMMISSIONING:
        return "DECOMMISSIONING";
    case CapacityAction::RECOMMISSIONING:
        return "RECOMMISSIONING";
    }
}

struct BalancingData
{
    double marginalCost;
    std::array<size_t, NUMBER_OF_HOURS_PER_WEEK> dispProdVarIndices;
};

/// @brief Class to generate and modify problems in memory
class ProblemGenerationForBalancing: public ProblemGenerationOptimSimu
{
public:
    explicit ProblemGenerationForBalancing(ConfigurationManager::ConfigDirectories directories,
                                           std::map<std::string, AreaSettings>& areasSettings,
                                           Logger logger,
                                           std::shared_ptr<ProblemManager> problemManager);
    virtual ~ProblemGenerationForBalancing() = default;
    std::map<Antares::Solver::WeeklyProblemId, std::shared_ptr<Problem>> updateProblems(
      const std::map<Antares::Solver::WeeklyProblemId, PbOutput>& simuValues);
    bool isBalanced(const std::map<Antares::Solver::WeeklyProblemId, PbOutput>& simuValues) const;
    void logCriterionAndAreaSettings(
      const std::map<Antares::Solver::WeeklyProblemId, PbOutput>& simuValues) const;
    void saveCriterionAndAreaSettingsToCSV(
      const std::map<Antares::Solver::WeeklyProblemId, PbOutput>& simuValues,
      const std::filesystem::path& outputPath) const;

private:
    std::map<std::string, AreaSettings>& areasSettings;
    std::map<AreaCluster, BalancingData> balancingData;
    std::map<std::string, CapacityAction> lastActionForArea;

    void fillDispProdVarIndicesAndMarginalCosts();
    void getInitialCapacitiesForCandidates();

    std::map<AreaCluster, CapacityAction> findAreaClustersToModify(
      const std::map<Antares::Solver::WeeklyProblemId, PbOutput>& simuValues);
    CriterionState criterionState(AreaSettings& areaSettings, double value) const;
    std::map<std::string, CriterionState> areaCriteriaState(
      const std::map<Antares::Solver::WeeklyProblemId, PbOutput>& simuValues) const;
    void updateAreaSettingsIncrement(const std::map<std::string, CriterionState>& areaCritState);
    void applyActionToCluster(const AreaCluster& areaCluster, CapacityAction action);
    std::string getBestCluster(
      const std::map<Antares::Solver::WeeklyProblemId, PbOutput>& simuValues,
      const std::string& areaName,
      const AreaSettings& areaSettings,
      CapacityAction action) const;
    void updateOldCriterionState(const std::map<std::string, CriterionState>& areaCritState);
    CapacityAction determineCapacityAction(const std::string& areaName,
                                           CriterionState currentState,
                                           const AreaSettings& areaSettings) const;
    template<typename CandidateType>
    std::map<std::string, double> computeRentabilityForCandidates(
      const std::string& areaName,
      const std::map<std::string, Candidate<CandidateType>>& candidates,
      const std::map<Antares::Solver::WeeklyProblemId, PbOutput>& simuValues,
      CapacityAction action) const;
    void fillDispProdVarIndicesAndMarginalCostsForArea(
      const std::string& areaName,
      const std::string& clusterName,
      const std::unordered_map<std::string_view, size_t>& varToIndex,
      const std::vector<double>& objCoeffs);
    double computeNewBoundAndUpdateCandidate(const std::shared_ptr<Problem>& problem,
                                             size_t varIndex,
                                             CapacityAction action,
                                             AreaSettings& areaSettings,
                                             const std::string& clusterName) const;
};
