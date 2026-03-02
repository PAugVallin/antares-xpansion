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
#include "antares-xpansion/core/ProblemFormat.h"
#include "antares-xpansion/helpers/ArchiveReader.h"
#include "antares-xpansion/lpnamer/helper/ProblemGenerationLogger.h"
#include "antares-xpansion/lpnamer/input_reader/MpsTxtWriter.h"
#include "antares-xpansion/lpnamer/main/ProblemGenerationExeOptions.h"
#include "antares-xpansion/lpnamer/model/Problem.h"
#include "antares-xpansion/lpnamer/model/SimulationInputMode.h"
#include "antares-xpansion/multisolver_interface/SolverAbstract.h"
#include "antares-xpansion/multisolver_interface/SolverConfig.h"

/// @brief Class to generate and modify problems in memory
class ProblemGenerationOptimSimu
{
public:
    explicit ProblemGenerationOptimSimu(ConfigurationManager::ConfigDirectories directories,
                                        Logger logger,
                                        const std::string& solverName = "xpress",
                                        unsigned int startWeek = 1,
                                        unsigned int endWeek = 52,
                                        bool savePbFiles = false,
                                        const std::string& problemFormat = "OPTIMIZED");
    virtual ~ProblemGenerationOptimSimu() = default;
    ConfigurationManager::ConfigDirectories
      directories; /// Directories, used for the original problems generation
    Logger logger; /// Logger used
    std::map<Antares::Solver::WeeklyProblemId, std::shared_ptr<Problem>>
      problems;                   /// Problems before any modification
    unsigned int startWeek;       /// Start week of the problems to take into account
    unsigned int endWeek;         /// End week of the problems to take into account
    bool writePbFiles;            /// Flag to writePbFiles to memory
    ProblemsFormat problemFormat; /// Problem format to be saved
};
