#pragma once

#include <functional>

#include <antares/solver/lps/LpsFromAntares.h>

#include "antares-xpansion/benders/benders_core/CriterionComputation.h"
#include "antares-xpansion/benders/benders_core/CriterionLOL.h"
#include "antares-xpansion/benders/benders_core/CriterionNPCAP.h"
#include "antares-xpansion/benders/benders_core/SubproblemWorker.h"
#include "antares-xpansion/benders/output/JsonWriter.h"
#include "antares-xpansion/lpnamer/model/Problem.h"
#include "antares-xpansion/xpansion_interfaces/ILogger.h"

constexpr char EVALUATOR_LOGGER_CONTEXT[] = "Evaluator";

using namespace PlainData;

/// @brief Class to compute Stock levels variation
class Evaluator
{
public:
    Evaluator(Logger logger,
              std::map<Antares::Solver::WeeklyProblemId, std::shared_ptr<Problem>> problems,
              std::string solverName,
              int nbThreads = 1);

    void setCriterionComputationInputs(
      const Benders::Criterion::CriterionInputData& criterion_input_data);

protected:
    void Run();
    virtual void ProcessSubproblem(const Antares::Solver::WeeklyProblemId,
                                   std::shared_ptr<Problem> subProblem)
      = 0;
    void SetConstraintsRHSValues(const std::map<std::string, double>& rhsValues,
                                 std::shared_ptr<Problem> subProblem);
    SubProblemData SolveSubproblem(std::shared_ptr<Problem> subProblem);
    std::string GetConstraintName(const Antares::Solver::WeeklyProblemId id,
                                  const std::string& area,
                                  const std::string& constraint) const;

protected:
    Logger logger;
    std::map<Antares::Solver::WeeklyProblemId, std::shared_ptr<Problem>>
      problems;             ///< map of subproblems
    std::string solverName; ///< Solver name

    std::unique_ptr<Benders::Criterion::CriterionComputation> criterion_computation_;

    int nbThreads; ///< Number of threads to use

    SolverLogManager solver_log_manager;

public:
    std::atomic<int> totalSimplexIter = 0;     ///< Total number of simplex iterations
    std::atomic<double> totalSubPbTimer = 0;   ///< Total time spent solving subproblems
    std::atomic<double> totalPbModifTimer = 0; ///< Total time spent modifying subproblems

    friend class BellmanValues;
};
