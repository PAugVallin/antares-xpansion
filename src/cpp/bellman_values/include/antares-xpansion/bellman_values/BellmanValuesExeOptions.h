#pragma once
#include "antares-xpansion/exe_options/CommonExeOptions.h"

class BellmanValuesExeOptions: public CommonExeOptions
{
private:
    int nbLevels_;
    bool useOptimalTrajectory_;

public:
    BellmanValuesExeOptions();
    virtual ~BellmanValuesExeOptions() = default;

    int NbLevels() const
    {
        return nbLevels_;
    }

    bool UseOptimalTrajectory() const
    {
        return useOptimalTrajectory_;
    }
};
