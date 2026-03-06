#pragma once

#include "CriterionComputation.h"

namespace Benders::Criterion
{

class CriterionLOL final: public CriterionComputation
{
public:
    explicit CriterionLOL() = default;

    explicit CriterionLOL(const CriterionInputData& criterion_input_data);

    void ComputeCriterion(std::shared_ptr<SolverAbstract> problem,
                          double subproblem_weight,
                          std::vector<double>& criteria,
                          std::vector<double>& patterns_values) override;
    ~CriterionLOL() override = default;
};
} // namespace Benders::Criterion
