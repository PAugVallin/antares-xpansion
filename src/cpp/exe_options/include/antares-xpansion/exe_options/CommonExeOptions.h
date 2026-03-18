#pragma once
#include <filesystem>

#include "antares-xpansion/helpers/OptionsParser.h"

class CommonExeOptions: public OptionsParser
{
protected:
    std::filesystem::path studyPath_;
    std::string solverName_;
    int nbThreads_;
    bool writePbFiles_;
    std::string problemFormat_;
    std::string verbosity_;
    bool cacheProblems_;

public:
    explicit CommonExeOptions();
    explicit CommonExeOptions(const std::string& description);
    virtual ~CommonExeOptions() = default;

    std::filesystem::path StudyPath() const
    {
        return studyPath_;
    }

    std::string SolverName() const
    {
        return solverName_;
    }

    int NbThreads() const
    {
        return nbThreads_;
    }

    bool WritePbFiles() const
    {
        return writePbFiles_;
    }

    std::string ProblemFormat() const
    {
        return problemFormat_;
    }

    std::string Verbosity() const
    {
        return verbosity_;
    }

    bool CacheProblems() const
    {
        return cacheProblems_;
    }
};
