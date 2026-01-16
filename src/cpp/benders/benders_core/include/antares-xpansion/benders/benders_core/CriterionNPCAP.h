#pragma once

#include "CriterionComputation.h"

namespace Benders::Criterion
{

class CriterionNPCAP final: public CriterionComputation
{
public:
    explicit CriterionNPCAP() = default;

    explicit CriterionNPCAP(const CriterionInputData& criterion_input_data):
        CriterionComputation(criterion_input_data)
    {
    }

    void ComputeCriterion(double subproblem_weight,
                          const std::vector<double>& sub_problem_solution,
                          std::vector<double>& criteria,
                          std::vector<double>& patterns_values) override;

    ~CriterionNPCAP() override = default;
};
} // namespace Benders::Criterion
