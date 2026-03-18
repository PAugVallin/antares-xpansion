#pragma once
#include "antares-xpansion/exe_options/CommonExeOptions.h"

class BellmanValuesExeOptions: public CommonExeOptions
{
private:
    int nbLevels_;
    int startWeek_;
    int endWeek_;
    bool antaresFormat_;
    bool useOptimalTrajectory_;

public:
    BellmanValuesExeOptions();
    virtual ~BellmanValuesExeOptions() = default;

    int NbLevels() const
    {
        return nbLevels_;
    }

    int StartWeek() const
    {
        return startWeek_;
    }

    int EndWeek() const
    {
        return endWeek_;
    }

    bool AntaresFormat() const
    {
        return antaresFormat_;
    }

    bool UseOptimalTrajectory() const
    {
        return useOptimalTrajectory_;
    }
};
