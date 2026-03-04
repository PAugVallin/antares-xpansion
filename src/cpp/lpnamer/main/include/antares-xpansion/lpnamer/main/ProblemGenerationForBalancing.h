//
// Created by marechaljas on 27/10/23.
//

#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <antares/solver/lps/LpsFromAntares.h>

#include "ConfigurationManager.h"
#include "ProblemGenerationOptions.h"
#include "antares-xpansion/balancing/BalancingParser.h"
#include "antares-xpansion/core/ProblemFormat.h"
#include "antares-xpansion/evaluator/BalancingEvaluator.h"
#include "antares-xpansion/helpers/ArchiveReader.h"
#include "antares-xpansion/lpnamer/helper/ProblemGenerationLogger.h"
#include "antares-xpansion/lpnamer/input_reader/MpsTxtWriter.h"
#include "antares-xpansion/lpnamer/main/ProblemGenerationExeOptions.h"
#include "antares-xpansion/lpnamer/main/ProblemGenerationOptimSimu.h"
#include "antares-xpansion/lpnamer/model/Problem.h"
#include "antares-xpansion/lpnamer/model/SimulationInputMode.h"
#include "antares-xpansion/multisolver_interface/SolverAbstract.h"
#include "antares-xpansion/multisolver_interface/SolverConfig.h"

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
    double currentBound;
};

/// @brief Class to generate and modify problems in memory
class ProblemGenerationForBalancing: public ProblemGenerationOptimSimu
{
public:
    explicit ProblemGenerationForBalancing(ConfigurationManager::ConfigDirectories directories,
                                           std::map<std::string, AreaInvestment>& areaInvestments,
                                           Logger logger,
                                           std::shared_ptr<ProblemManager> problemManager,
                                           unsigned int startWeek = 1,
                                           unsigned int endWeek = 52);
    virtual ~ProblemGenerationForBalancing() = default;
    std::map<Antares::Solver::WeeklyProblemId, std::shared_ptr<Problem>> updateProblems(
      const std::map<Antares::Solver::WeeklyProblemId, PbOutput>& simuValues);
    bool isBalanced() const;

private:
    std::map<std::string, AreaInvestment>& areaInvestments;
    std::map<AreaCluster, BalancingData> balancingData;

    void fillDispProdVarIndicesAndMarginalCosts();

    std::map<AreaCluster, CapacityAction> findAreaClustersToModify(
      const std::map<Antares::Solver::WeeklyProblemId, PbOutput>& simuValues);
    CriterionState criterionState(AreaInvestment& areaInvestment, double value);
    std::map<std::string, CriterionState> areaCriteriaState(
      const std::map<Antares::Solver::WeeklyProblemId, PbOutput>& simuValues);
    void updateAreaInvestmentIncrement(const std::map<std::string, CriterionState>& areaCritState);
    void applyActionToCluster(const AreaCluster& areaCluster, CapacityAction action);
    std::string getBestCluster(
      const std::map<Antares::Solver::WeeklyProblemId, PbOutput>& simuValues,
      const std::string& areaName,
      const AreaInvestment& areaInvestment,
      CapacityAction action);
    void updateOldCriterionState(const std::map<std::string, CriterionState>& areaCritState);
    CapacityAction determineCapacityAction(CriterionState current,
                                           CriterionState previous,
                                           const AreaInvestment& areaInvestment);
    void logCriterionAndAreaInvestments(const std::map<std::string, CriterionState>& areaCritState);
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
                                             AreaInvestment& areaInvestment,
                                             const std::string& clusterName);
};
