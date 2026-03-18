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
                                           std::shared_ptr<ProblemManager> problemManager,
                                           unsigned int startWeek = 1,
                                           unsigned int endWeek = 52);
    virtual ~ProblemGenerationForBalancing() = default;
    std::map<Antares::Solver::WeeklyProblemId, std::shared_ptr<Problem>> updateProblems(
      const std::map<Antares::Solver::WeeklyProblemId, PbOutput>& simuValues);
    bool isBalanced() const;

private:
    std::map<std::string, AreaSettings>& areasSettings;
    std::map<AreaCluster, BalancingData> balancingData;
    std::map<std::string, CapacityAction> lastActionForArea;

    void fillDispProdVarIndicesAndMarginalCosts();
    void getInitialCapacitiesForDecommissioningCandidates();

    std::map<AreaCluster, CapacityAction> findAreaClustersToModify(
      const std::map<Antares::Solver::WeeklyProblemId, PbOutput>& simuValues);
    CriterionState criterionState(AreaSettings& areaSettings, double value);
    std::map<std::string, CriterionState> areaCriteriaState(
      const std::map<Antares::Solver::WeeklyProblemId, PbOutput>& simuValues);
    void updateAreaSettingsIncrement(const std::map<std::string, CriterionState>& areaCritState);
    void applyActionToCluster(const AreaCluster& areaCluster, CapacityAction action);
    std::string getBestCluster(
      const std::map<Antares::Solver::WeeklyProblemId, PbOutput>& simuValues,
      const std::string& areaName,
      const AreaSettings& areaSettings,
      CapacityAction action);
    void updateOldCriterionState(const std::map<std::string, CriterionState>& areaCritState);
    CapacityAction determineCapacityAction(const std::string& areaName,
                                           CriterionState currentState,
                                           const AreaSettings& areaSettings);
    void logCriterionAndAreaSettingss(const std::map<std::string, CriterionState>& areaCritState);
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
                                             const std::string& clusterName);
};
