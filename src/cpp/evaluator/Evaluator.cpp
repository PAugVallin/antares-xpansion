
#include "antares-xpansion/evaluator/Evaluator.h"

#include <fmt/core.h>
#include <regex>
#include <sstream>
#include <tbb/global_control.h>
#include <tbb/parallel_for_each.h>
#include <utility>

#include "antares-xpansion/benders/benders_core/BendersProblemFromFile.h"
#include "antares-xpansion/helpers/Timer.h"

using namespace PlainData;

/// @brief Constructor
/// @param logger Logger
/// @param problems map of subproblems to evaluate
/// @param solverName Name of the solver to use
/// @param nbThreads Number of threads to use
Evaluator::Evaluator(Logger logger,
                     std::map<Antares::Solver::WeeklyProblemId, std::shared_ptr<Problem>> problems,
                     std::string solverName,
                     int nbThreads):
    logger{std::move(logger)},
    problems(problems),
    solverName(solverName),
    nbThreads(nbThreads)
{
}

void Evaluator::setCriterionComputationInputs(
  const Benders::Criterion::CriterionInputData& criterion_input_data)
{
    using enum Benders::Criterion::Type;
    if (problems.empty())
    {
        throw std::runtime_error("No problems available");
    }
    auto& [id, problem] = *problems.begin();

    switch (criterion_input_data.criterion)
    {
    case PositiveUnsuppliedEnergy:
        criterion_computation_ = std::make_unique<Benders::Criterion::CriterionLOL>(
          criterion_input_data,
          problem);
        break;
    case NearPriceCapHours:
        criterion_computation_ = std::make_unique<Benders::Criterion::CriterionNPCAP>(
          criterion_input_data,
          problem);
        break;
    default:
        criterion_computation_.reset();
        break;
    }
}

/// @brief Set the constraints RHS values for a given subproblem
/// @param rhsValues The RHS values to set
/// @param subProblem The subproblem
void Evaluator::SetConstraintsRHSValues(const std::map<std::string, double>& rhsValues,
                                        std::shared_ptr<Problem> subProblem)
{
    for (const auto& [constraintName, value]: rhsValues)
    {
        subProblem->fix_rhs_to(constraintName, value);
    }
}

/// @brief Get the name of the constraint in the mps file
/// @param id ID of the subproblem
/// @param area The name of the area
/// @param constraint The name of the constraint
/// @return The name of the constraint in the mps file
std::string Evaluator::GetConstraintName(const Antares::Solver::WeeklyProblemId id,
                                         const std::string& area,
                                         const std::string& constraint) const
{
    return fmt::format("{}::area<{}>::week<{}>", constraint, area, id.week - 1);
}

/// @brief Runs the ProcessSubproblem method in parallel for each subproblem
void Evaluator::Run()
{
    // Limiter TBB au nombre de cœurs physiques
    tbb::global_control limit(tbb::global_control::max_allowed_parallelism, nbThreads);

    tbb::parallel_for_each(problems.begin(),
                           problems.end(),
                           [this](auto& kv)
                           {
                               auto& [yearWeekId, subPb] = kv;
                               logger->display_message((std::stringstream()
                                                        << "Processing subproblem : year "
                                                        << yearWeekId.year << " week "
                                                        << yearWeekId.week)
                                                         .str(),
                                                       LogUtils::LOGLEVEL::INFO,
                                                       EVALUATOR_LOGGER_CONTEXT);
                               ProcessSubproblem(yearWeekId, subPb);
                           });
}

/// @brief Solve the subproblem and return the cost
/// @param problem The subproblem to solve
/// @return The data of the solved subproblem : cost and dualValues
SubProblemData Evaluator::SolveSubproblem(std::shared_ptr<Problem> problem, Point subPbCombo)
{
    SubProblemData subPbData;
    Timer subproblem_timer;
    problem->solve_lp();

    std::vector<double> dualValuesCst(problem->get_nrows());
    problem->get_lp_sol(NULL, dualValuesCst.data(), NULL);

    subPbData.subproblem_cost = problem->get_lp_value();

    subPbData.subproblem_timer = subproblem_timer.elapsed();
    int nbSimplexIter = problem->get_splex_num_of_ite_last();

    for (const auto& [constraintName, value]: subPbCombo)
    {
        subPbData.dual.emplace(constraintName,
                               dualValuesCst[problem->get_row_index(constraintName)]);
    }

    if (criterion_computation_)
    {
        criterion_computation_->ComputeCriterion(problem,
                                                 1,
                                                 subPbData.criteria,
                                                 subPbData.patterns_values);
    }
    totalSimplexIter += nbSimplexIter;
    totalSubPbTimer += subPbData.subproblem_timer;

    return subPbData;
}
