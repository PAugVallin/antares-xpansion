#pragma once

#include <functional>

#include <antares/solver/lps/LpsFromAntares.h>

#include "antares-xpansion/benders/benders_core/CriterionComputation.h"
#include "antares-xpansion/benders/benders_core/CriterionLOL.h"
#include "antares-xpansion/benders/benders_core/CriterionNPCAP.h"
#include "antares-xpansion/benders/benders_core/SubproblemWorker.h"
#include "antares-xpansion/benders/output/JsonWriter.h"
#include "antares-xpansion/grid_evaluator/GridCollection.h"
#include "antares-xpansion/lpnamer/model/Problem.h"
#include "antares-xpansion/xpansion_interfaces/ILogger.h"

constexpr char GRID_EVALUATOR_LOGGER_CONTEXT[] = "GridEvaluator";

/// @brief vector of maps (key constraint name, value rhs value)
using ConstraintCombos = std::vector<std::map<std::string, double>>;
using namespace PlainData;

/// @brief Class to compute Stock levels variation
class GridEvaluator
{
public:
    GridEvaluator(Logger logger,
                  std::map<Antares::Solver::WeeklyProblemId, std::shared_ptr<Problem>> problems,
                  GridDefinition& grid_definition,
                  std::string solverName,
                  int nbThreads = 1);
    // virtual function to be overridable for the tests
    virtual std::map<Output::PointWeekScenarioKey, SubProblemData> ComputeCostsAndDuals();

    void setCriterionComputationInputs(
      const Benders::Criterion::CriterionInputData& criterion_input_data)
    {
        using enum Benders::Criterion::Type;
        switch (criterion_input_data.criterion)
        {
        case PositiveUnsuppliedEnergy:
            criterion_computation_ = std::make_unique<Benders::Criterion::CriterionLOL>(
              criterion_input_data);
            break;
        case NearPriceCapHours:
            criterion_computation_ = std::make_unique<Benders::Criterion::CriterionNPCAP>(
              criterion_input_data);
            break;
        default:
            criterion_computation_.reset();
            break;
        }
    }

private:
    Output::ConcurrentInsertionMap<Output::PointWeekScenarioKey, SubProblemData>
      variationDeNiveauxDeStockResults;

protected:
    void Run();
    void ProcessSubproblem(const Antares::Solver::WeeklyProblemId,
                           std::shared_ptr<Problem> subProblem);
    void SetConstraintsRHSValues(const std::map<std::string, double>& rhsValues,
                                 std::shared_ptr<Problem> subProblem);
    SubProblemData SolveSubproblem(std::shared_ptr<Problem> subProblem, Point subPbCombo);
    std::string GetConstraintName(const Antares::Solver::WeeklyProblemId id,
                                  const std::string& area,
                                  const std::string& constraint) const;
    ConstraintCombos GenerateConstraintProduct(const ConstraintMap& constraints);
    ConstraintCombos GenerateSubPbCombos(const Antares::Solver::WeeklyProblemId subProblemId,
                                         const AreaConstraintMaps& areas);

protected:
    Logger logger;
    std::map<Antares::Solver::WeeklyProblemId, std::shared_ptr<Problem>>
      problems;                     ///< map of subproblems
    GridDefinition& gridDefinition; ///< Grid definition
    std::string solverName;         ///< Solver name

    std::unique_ptr<Benders::Criterion::CriterionComputation> criterion_computation_;

    int nbThreads; ///< Number of threads to use

    SolverLogManager solver_log_manager;

    friend class BellmanValues;
};
