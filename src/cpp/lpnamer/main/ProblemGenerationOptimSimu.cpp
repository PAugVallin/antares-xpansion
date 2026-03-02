
#include "antares-xpansion/lpnamer/main/ProblemGenerationOptimSimu.h"

#include <execution>
#include <iostream>
#include <tbb/parallel_for_each.h>
#include <utility>

#include <antares/api/solver.h>

#include "antares-xpansion/benders/output/OutputWriter.h"
#include "antares-xpansion/helpers/solver_utils.h"
#include "antares-xpansion/lpnamer/problem_modifier/XpansionProblemsFromAntaresProvider.h"
#include "malloc.h"

/// @brief Launch the simulation and save the problems satisfying startWeek <= week <= endweek
ProblemGenerationOptimSimu::ProblemGenerationOptimSimu(
  ConfigurationManager::ConfigDirectories directories,
  Logger logger,
  const std::string& solverName,
  unsigned int startWeek,
  unsigned int endWeek,
  bool writePbFiles,
  const std::string& problemFormat):
    directories(directories),
    logger(std::move(logger)),
    startWeek(startWeek),
    endWeek(endWeek),
    writePbFiles(writePbFiles),
    problemFormat(problemsFormatFromString(problemFormat))
{
    Antares::Solver::Optimization::OptimizationOptions optOptions;
    optOptions.firstOptimOptions.solverName = solverName;
    optOptions.secondOptimOptions.solverName = solverName;

    if (solverName == SolverConfig("xpress"))
    {
        optOptions.firstOptimOptions.solverParameters = "PRESOLVE 1";
        optOptions.secondOptimOptions.solverParameters = "PRESOLVE 1";
    }

    auto [results, error] = Antares::API::PerformSimulation(directories.study_dir,
                                                            directories.simulation_dir,
                                                            optOptions);

#ifndef _WIN32
    malloc_trim(0);
#endif

    // Handle errors
    if (error)
    {
        throw LogUtils::XpansionError<std::runtime_error>("Antares simulation failed:\n\t"
                                                            + error->reason,
                                                          LOGLOCATION);
    }

    XpansionProblemsFromAntaresProvider adapter(results);
    auto solver_log_manager = SolverLogManager(directories.simulation_dir / "solver.log");
    for (const auto& [pbId, _]: results.weeklyProblems)
    {
        if (startWeek <= pbId.week && pbId.week <= endWeek)
        {
            auto problem = adapter.provideProblem(solverName == SolverConfig("xpress") ? "xpress"
                                                                                       : "CBC",
                                                  solver_log_manager,
                                                  pbId);
            problems[pbId] = problem;
        }
    }

    if (!problems.empty())
    {
        this->startWeek = std::max(startWeek, problems.begin()->first.week);
        this->endWeek = std::min(endWeek, problems.rbegin()->first.week);
    }
}
