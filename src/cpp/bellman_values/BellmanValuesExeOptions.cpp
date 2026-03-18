#include "antares-xpansion/bellman_values/BellmanValuesExeOptions.h"

namespace po = boost::program_options;

BellmanValuesExeOptions::BellmanValuesExeOptions():
    CommonExeOptions("Bellman Values computation exe")
{
    AddOptions()("nb-levels",
                 po::value<int>(&nbLevels_)->default_value(10),
                 "Number of levels (optional, default is 10)")(
      "use-optimal-trajectory",
      po::value<bool>(&useOptimalTrajectory_)->default_value(false),
      "Specify whether the optimal trajectory must be used in the case of "
      "multistock water values (optional, default is false)")(
      "start-week",
      po::value<int>(&startWeek_)->default_value(1),
      "Start week (optional, default is 1)")("end-week",
                                             po::value<int>(&endWeek_)->default_value(52),
                                             "End week (optional, default is 52)")(
      "antares-format",
      po::value<bool>(&antaresFormat_)->default_value(false),
      "Output in Antares format (optional, default is false)");
}
