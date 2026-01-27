
#include <chrono>
#include <iostream>
#include <ranges>

#include "antares-xpansion/bellman_values/BellmanValuesExeOptions.h"
#include "antares-xpansion/bellman_values/PenaltiesConfigReader.h"
#include "antares-xpansion/benders/factories/LoggerFactories.h"
#include "antares-xpansion/evaluator/GridEvaluator.h"
#include "antares-xpansion/helpers/AreaParser.h"
#include "antares-xpansion/lpnamer/main/ProblemGenerationForWaterValueCalculation.h"
#include "antares-xpansion/lpnamer/problem_modifier/XpansionProblemsFromAntaresProvider.h"
#include "malloc.h"

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

std::set<std::string> readAreaFile(const std::filesystem::path& areaFile)
{
    const auto area_file_data = AreaParser::ReadAreaFile(areaFile);
    if (const auto& msg = area_file_data.error_message; !msg.empty())
    {
        throw std::runtime_error("File " + areaFile.string() + " has not been found");
        return {};
    }
    return {area_file_data.areas.begin(), area_file_data.areas.end()};
}

Benders::Criterion::CriterionInputData buildPatterns(const Benders::Criterion::Type criterion,
                                                     const std::filesystem::path& areaFile)
{
    std::set<std::string> unique_areas = readAreaFile(areaFile);

    Benders::Criterion::CriterionInputData ret{false, criterion};
    for (const auto& area: unique_areas)
    {
        Benders::Criterion::CriterionSingleInputData singleInputData(getPrefix(criterion), area, 1);
        ret.AddSingleData(singleInputData);
    }

    return ret;
}

int main(int argc, char** argv)
{
    try
    {
        auto optionsParser = BellmanValuesExeOptions();
        optionsParser.Parse(argc, argv);
        auto studyPath = optionsParser.StudyPath();
        auto solverName = optionsParser.SolverName();
        int nbThreads = optionsParser.NbThreads();
        int startWeek = optionsParser.StartWeek();
        int endWeek = optionsParser.EndWeek();
        bool antaresFormat = optionsParser.AntaresFormat();
        bool writePbFiles = optionsParser.WritePbFiles();
        Benders::Criterion::Type critType = optionsParser.CriterionType();
        const std::string problemFormat = optionsParser.ProblemFormat();
        const auto areaFile = studyPath / "area.txt";

        ConfigurationManager::ConfigDirectories directories{
          .study_dir = studyPath,
          .simulation_dir = ConfigurationManager::generateOutputName(studyPath),
        };

        // at this point, the simulation folder is already needed for logs (normally created when
        // updating problems)
        if (!std::filesystem::exists(directories.simulation_dir))
        {
            std::filesystem::create_directories(directories.simulation_dir);
        }
        std::filesystem::path logPath = directories.simulation_dir / "balancing_log.txt";
        std::ofstream{logPath}; // creates log file, since the FileLoggerFactory doesn't
        auto loggerFactory = FileAndStdoutLoggerFactory(logPath, false);
        Logger logger = loggerFactory.get_logger();

        auto startProblemGeneration = std::chrono::system_clock::now();
        logger->display_message(
          "Generating problems (starting time: " + formatTime(startProblemGeneration) + ")");
        // ProblemGenerationForWaterValueCalculation pbg(directories,
        //                                               reservoirManagement,
        //                                               logger,
        //                                               solverName,
        //                                               startWeek,
        //                                               endWeek,
        //                                               writePbFiles,
        //                                               problemFormat);
        auto endProblemGeneration = std::chrono::system_clock::now();
        logger->display_message("Problems generated (end time: " + formatTime(endProblemGeneration)
                                + ")");
        std::chrono::duration<double> elapsed_seconds = endProblemGeneration
                                                        - startProblemGeneration;
        logger->display_message("Elapsed time for problem generation: "
                                + formatDuration(elapsed_seconds));

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
