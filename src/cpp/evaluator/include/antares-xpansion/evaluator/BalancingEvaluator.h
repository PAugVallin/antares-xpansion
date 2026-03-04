#pragma once

#include <functional>

#include <antares/solver/lps/LpsFromAntares.h>

#include "antares-xpansion/balancing/BalancingParser.h"
#include "antares-xpansion/benders/benders_core/CriterionComputation.h"
#include "antares-xpansion/benders/benders_core/CriterionLOL.h"
#include "antares-xpansion/benders/benders_core/CriterionNPCAP.h"
#include "antares-xpansion/benders/benders_core/SubproblemWorker.h"
#include "antares-xpansion/benders/output/JsonWriter.h"
#include "antares-xpansion/evaluator/Evaluator.h"
#include "antares-xpansion/lpnamer/model/Problem.h"
#include "antares-xpansion/xpansion_interfaces/ILogger.h"

constexpr char BALANCING_EVALUATOR_LOGGER_CONTEXT[] = "BalancingEvaluator";
constexpr int NUMBER_OF_HOURS_PER_WEEK = 168;

using namespace PlainData;

struct PbOutput
{
    std::map<std::string, int> areaCriterionValues{};
    std::map<std::string, std::array<double, NUMBER_OF_HOURS_PER_WEEK>>
      areaPrices{}; // Dual value of AreaBalance Constraint
};

class BalancingEvaluator: public Evaluator
{
public:
    BalancingEvaluator(
      Logger logger,
      const std::map<std::string, AreaInvestment>& areaInvestments,
      Benders::Criterion::Type criterion,
      std::map<Antares::Solver::WeeklyProblemId, std::shared_ptr<Problem>> problems,
      std::string solverName,
      int nbThreads = 1);

    std::map<Antares::Solver::WeeklyProblemId, PbOutput> ComputeCriterionAndPrice();

private:
    Benders::Criterion::CriterionInputData buildPatterns(
      Benders::Criterion::Type criterion,
      const std::map<std::string, AreaInvestment>& areaInvestments);
    std::vector<size_t> getAreaBalanceIndices(std::shared_ptr<Problem> subProblem);
    Output::ConcurrentInsertionMap<Antares::Solver::WeeklyProblemId, PbOutput> balancingResults;
    const std::map<std::string, AreaInvestment>& areaInvestments;
    void fillAreaCriterionValuesAndPrices(const std::vector<double>& criteria,
                                          const std::vector<double>& dualValuesCst,
                                          const std::vector<size_t>& cstIndices,
                                          PbOutput& output);

protected:
    void ProcessSubproblem(const Antares::Solver::WeeklyProblemId,
                           std::shared_ptr<Problem> subProblem) override;
};
