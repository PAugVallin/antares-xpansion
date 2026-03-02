
#include "antares-xpansion/evaluator/BalancingEvaluator.h"

#include <fmt/core.h>
#include <regex>
#include <sstream>
#include <tbb/global_control.h>
#include <tbb/parallel_for_each.h>
#include <unordered_set>
#include <utility>

#include "antares-xpansion/benders/benders_core/BendersProblemFromFile.h"
#include "antares-xpansion/helpers/Timer.h"

using namespace PlainData;

/// @brief Constructor of the BalancingEvaluator class
/// @param logger The logger to use for the evaluation
/// @param areaInvestments The area investments to use for the evaluation
/// @param criterion The criterion to evaluate
/// @param problems The problems to evaluate on
/// @param solverName The name of the solver to use for the evaluation
/// @param nbThreads The number of threads to use for the evaluation
BalancingEvaluator::BalancingEvaluator(
  Logger logger,
  const std::map<std::string, AreaInvestment>& areaInvestments,
  Benders::Criterion::Type criterion,
  std::map<Antares::Solver::WeeklyProblemId, std::shared_ptr<Problem>> problems,
  std::string solverName,
  int nbThreads):
    Evaluator(logger, problems, solverName, nbThreads),
    areaInvestments(areaInvestments)
{
    auto criterionInputData = buildPatterns(criterion, areaInvestments);
    setCriterionComputationInputs(criterionInputData);
}

/// @brief Build the patterns to use for the criterion computation
/// @param criterion The criterion to evaluate
/// @param areaInvestments The area investments to use for the evaluation
/// @return The criterion input data containing the patterns to use for the criterion computation
Benders::Criterion::CriterionInputData BalancingEvaluator::buildPatterns(
  Benders::Criterion::Type criterion,
  const std::map<std::string, AreaInvestment>& areaInvestments)
{
    Benders::Criterion::CriterionInputData ret{false, criterion};
    for (const auto& area: areaInvestments | std::views::keys)
    {
        Benders::Criterion::CriterionSingleInputData singleInputData(getPrefix(criterion), area, 1);
        ret.AddSingleData(singleInputData);
    }

    return ret;
}

/// @brief Get the indices of the area balance constraints in the problem
/// @param subProblem The problem to get the indices from
/// @return The indices of the area balance constraints in the problem
std::vector<size_t> BalancingEvaluator::getAreaBalanceIndices(std::shared_ptr<Problem> subProblem)
{
    const auto& constraints = subProblem->get_row_names();

    constexpr std::string_view prefix = "AreaBalance::area";
    constexpr std::string_view hourTag = "::hour";

    std::unordered_set<std::string_view> areas;
    for (const auto& [name, _]: areaInvestments)
    {
        areas.insert(name);
    }

    std::vector<size_t> indices;

    for (std::size_t i = 0; i < constraints.size(); ++i)
    {
        std::string_view v = constraints[i];
        if (!v.starts_with(prefix))
        {
            continue;
        }

        v.remove_prefix(prefix.size());
        auto area = v.substr(1, v.find(hourTag) - 2); // remove also the angled brackets from <area>

        if (areas.contains(area))
        {
            indices.push_back(i);
        }
    }

    return indices;
}

/// @brief Process a single subproblem
/// @param subProblemId the id of the problem to treat
/// @param subProblem the problem to treat
void BalancingEvaluator::ProcessSubproblem(const Antares::Solver::WeeklyProblemId subProblemId,
                                           std::shared_ptr<Problem> subProblem)
{
    Timer timer;
    totalPbModifTimer += timer.elapsed();
    SubProblemData res = SolveSubproblem(subProblem);

    criterion_computation_->ComputeCriterion(subProblem, 1, res.criteria, res.patterns_values);

    std::vector<double> dualValuesCst(subProblem->get_nrows());
    subProblem->get_lp_sol(NULL, dualValuesCst.data(), NULL);
    const auto& cstIndices = getAreaBalanceIndices(subProblem);

    PbOutput output{};
    // For each area / for each hour
    for (std::size_t i = 0; i < res.criteria.size(); ++i)
    {
        double value = res.criteria[i];
        std::string area = criterion_computation_->getCriterionInputData().PatternBodies()[i];
        output.areaCriterionValues[area] += value;

        for (size_t j = 0; j < NUMBER_OF_HOURS_PER_WEEK; j++)
        {
            output.areaPrices[area][j] = dualValuesCst[cstIndices[i * NUMBER_OF_HOURS_PER_WEEK
                                                                  + j]];
        }
    }

    balancingResults.insert(subProblemId, output);
    logger->display_message((std::stringstream() << "Cost: " << res.subproblem_cost).str(),
                            LogUtils::LOGLEVEL::DEBUG,
                            BALANCING_EVALUATOR_LOGGER_CONTEXT);
}

/// @brief Compute the criterion and the price for each subproblem
/// @return A map associating each subproblem id to the computed criterion and price
std::map<Antares::Solver::WeeklyProblemId, PbOutput> BalancingEvaluator::ComputeCriterionAndPrice()
{
    logger->display_message(
      (std::stringstream() << "Launching criterion and price evaluation").str(),
      LogUtils::LOGLEVEL::INFO,
      BALANCING_EVALUATOR_LOGGER_CONTEXT);

    Timer run_timer;

    Run();

    auto run_time = run_timer.elapsed();
    logger->display_message(
      (std::stringstream() << "Evaluation done in " << run_time << " seconds").str(),
      LogUtils::LOGLEVEL::INFO,
      BALANCING_EVALUATOR_LOGGER_CONTEXT);
    logger->display_message((std::stringstream()
                             << "Time solving subproblems (accumulated by each thread) : "
                             << totalSubPbTimer << " seconds")
                              .str(),
                            LogUtils::LOGLEVEL::INFO,
                            BALANCING_EVALUATOR_LOGGER_CONTEXT);

    return balancingResults.get();
}
