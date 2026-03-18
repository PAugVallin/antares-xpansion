#include "antares-xpansion/exe_options/CommonExeOptions.h"

namespace po = boost::program_options;

CommonExeOptions::CommonExeOptions():
    CommonExeOptions("Common options for Antares Xpansion executables")
{
}

CommonExeOptions::CommonExeOptions(const std::string& description):
    OptionsParser(description)
{
    AddOptions()("help,h",
                 "produce help message")("study",
                                         po::value<std::filesystem::path>(&studyPath_)->required(),
                                         "Path to archive (required)")(
      "solver",
      po::value<std::string>(&solverName_)->default_value("xpress"),
      "Solver to use (optional, default is xpress). Possible values are: xpress, coin")(
      "threads",
      po::value<int>(&nbThreads_)->default_value(1),
      "Number of threads to use (optional, default is 1)")(
      "start-week",
      po::value<int>(&startWeek_)->default_value(1),
      "Start week (optional, default is 1)")("end-week",
                                             po::value<int>(&endWeek_)->default_value(52),
                                             "End week (optional, default is 52)")(
      "antares-format",
      po::value<bool>(&antaresFormat_)->default_value(false),
      "Output in Antares format (optional, default is false)")(
      "keepMps",
      po::value<bool>(&writePbFiles_)->default_value(false),
      "Write MPS or SVF files to disk (optional, default is false)")(
      "problem-format",
      po::value<std::string>(&problemFormat_)->default_value("OPTIMIZED"),
      "Format to save problem files to (optional, default is OPTIMIZED). Possible values are: MPS, "
      "OPTIMIZED")("verbosity",
                   po::value<std::string>(&verbosity_)->default_value("INFO"),
                   "Specify the desired verbosity for logging in the console or log file "
                   "(optional, default is INFO). "
                   "Possible values are: NONE, TRACE, DEBUG, INFO, WARNING, ERR, FATAL")(
      "cache-problems",
      po::value<bool>(&cacheProblems_)->default_value(false),
      "Write and read problems from disk to reduce memory use (will increase computation time) "
      "(optional, default is false)");
}
