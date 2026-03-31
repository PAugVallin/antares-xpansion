
#include <chrono>
#include <iostream>

#include "antares-xpansion/balancing/BalancingParser.h"
#include "antares-xpansion/bellman_values/ProblemManager.h"
#include "antares-xpansion/benders/factories/LoggerFactories.h"
#include "antares-xpansion/evaluator/GreedyBalancingFinder.h"
#include "antares-xpansion/exe_options/CommonExeOptions.h"
#include "antares-xpansion/lpnamer/main/ProblemGenerationForBalancing.h"

using namespace PlainData;

std::string formatTime(const std::chrono::system_clock::time_point& timePoint)
{
    // the <format> STL seems to not be available on all used compilers yet
    std::time_t tt = std::chrono::system_clock::to_time_t(timePoint);
    std::tm tm = *std::localtime(&tt); // Locale time-zone, usually UTC by default.
    return (std::stringstream() << std::put_time(&tm, "%T")).str();
}

template<typename T>
std::string formatDuration(std::chrono::duration<T> duration)
{
    // the <format> STL seems to not be available on all used compilers yet
    auto h = std::chrono::duration_cast<std::chrono::hours>(duration);
    duration -= h;
    auto m = std::chrono::duration_cast<std::chrono::minutes>(duration);
    duration -= m;
    auto s = std::chrono::duration_cast<std::chrono::seconds>(duration);
    return (std::stringstream() << h.count() << "h, " << m.count() << "m, " << s.count() << "s")
      .str();
}

int main(int argc, char** argv)
{
    try
    {
        auto optionsParser = CommonExeOptions();
        optionsParser.Parse(argc, argv);
        auto studyPath = optionsParser.StudyPath();
        auto solverName = optionsParser.SolverName();
        int nbThreads = optionsParser.NbThreads();
        const auto areaFile = studyPath / "area.txt";

        ConfigurationManager::ConfigDirectories directories{
          .study_dir = studyPath,
          .simulation_dir = ConfigurationManager::generateOutputName(studyPath),
        };

        const std::filesystem::path balancingConfigFilePath(studyPath
                                                            / "user/balancing/input_balancing.yml");

        BalancingParser balParser(balancingConfigFilePath);

        if (!std::filesystem::exists(directories.simulation_dir))
        {
            std::filesystem::create_directories(directories.simulation_dir);
        }
        std::filesystem::path logPath = directories.simulation_dir / "balancing_log.txt";
        auto loggerFactory = FileAndStdoutLoggerFactory(logPath, false);
        Logger logger = loggerFactory.get_logger();

        auto startProblemGeneration = std::chrono::system_clock::now();
        logger->display_message(
          "Generating problems (starting time: " + formatTime(startProblemGeneration) + ")");
        auto problemManager = std::make_shared<ProblemManager>(solverName);
        ProblemGenerationForBalancing pbg(directories,
                                          balParser.areaSettings,
                                          logger,
                                          problemManager);
        auto endProblemGeneration = std::chrono::system_clock::now();
        logger->display_message("Problems generated");
        std::chrono::duration<double> elapsed_seconds = endProblemGeneration
                                                        - startProblemGeneration;
        logger->display_message("Elapsed time for problem generation: "
                                + formatDuration(elapsed_seconds));

        std::map<Antares::Solver::WeeklyProblemId, PbOutput> res;
        constexpr int MAX_ITERATIONS = 30;
        int iteration = 0;
        auto startBalancingProcess = std::chrono::system_clock::now();
        logger->display_message("Starting balancing process");
        while (!pbg.isBalanced(res) && iteration < MAX_ITERATIONS)
        {
            auto startIteration = std::chrono::system_clock::now();
            logger->display_message("Iteration " + std::to_string(iteration));
            iteration++;
            auto problems = pbg.updateProblems(res);

            res = GreedyBalancingFinder(logger,
                                        balParser.areaSettings,
                                        balParser.getReliabilityStandardIndicator(),
                                        problems,
                                        solverName,
                                        directories.simulation_dir,
                                        nbThreads)
                    .ComputeCriterionAndPrice();
            auto endIteration = std::chrono::system_clock::now();
            std::chrono::duration<double> elapsed_iteration_seconds = endIteration - startIteration;
            logger->display_message("Elapsed time for iteration " + std::to_string(iteration) + ": "
                                    + formatDuration(elapsed_iteration_seconds));
        };
        pbg.logCriterionAndAreaSettings(res);
        auto endProblemUpdate = std::chrono::system_clock::now();
        std::chrono::duration<double> elapsed_update_seconds = endProblemUpdate
                                                               - startBalancingProcess;
        logger->display_message("Balancing process ended after " + std::to_string(iteration)
                                + " iterations. In " + formatDuration(elapsed_update_seconds));
        logger->display_message(pbg.isBalanced(res) ? "The system is balanced."
                                                    : "The system is not balanced.");

        return 0;
    }
    catch (std::exception& e)
    {
        std::cerr << "error: " << e.what() << std::endl;
        return 1;
    }
    catch (...)
    {
        std::cerr << "Exception of unknown type!" << std::endl;
        return 1;
    }

    return 0;
}
