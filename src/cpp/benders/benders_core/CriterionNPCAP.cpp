#include "antares-xpansion/benders/benders_core/CriterionNPCAP.h"

#include "antares-xpansion/xpansion_interfaces/StringManip.h"

namespace Benders::Criterion
{
CriterionNPCAP::CriterionNPCAP(const CriterionInputData& criterion_input_data,
                               std::shared_ptr<SolverAbstract> problem):
    CriterionComputation(criterion_input_data)
{
    const auto row_names = problem->get_row_names();
    SearchConstraints(row_names);

    auto col_names = problem->get_col_names();
    for (size_t index = 0; index < col_names.size(); ++index)
    {
        double unspEnergyObj{0};
        // remove end spaces
        const auto& name = StringManip::removeTrailingSpacesInPlace(col_names[index]);

        if (name.starts_with("PositiveUnsuppliedEnergy::area<") && name.ends_with(">::hour<0>"))
        {
            std::string area = StringManip::split(StringManip::split(name, "area<")[1],
                                                  ">::hour")[0];
            problem->get_obj(&unspEnergyObj, index, index);
            unspEnergyObj -= 5;
            criterionThreasholdByArea[area] = unspEnergyObj;
        }
    }
}

void CriterionNPCAP::ComputeCriterion(std::shared_ptr<SolverAbstract> problem,
                                      double subproblem_weight,
                                      std::vector<double>& criteria,
                                      std::vector<double>& patterns_values)
{
    auto criteria_input_size = static_cast<int>(indices_.size()); // num of patterns
    criteria.resize(criteria_input_size, 0.);
    patterns_values.resize(criteria_input_size, 0.);

    std::vector<double> dualValuesCst(problem->get_nrows());
    problem->get_lp_sol(NULL, dualValuesCst.data(), NULL);

    for (int pattern_index(0); pattern_index < criteria_input_size; ++pattern_index)
    {
        auto pattern_indices = indices_[pattern_index];
        double pattern_value = patterns_values[pattern_index];
        double criteria_value = criteria[pattern_index];
        for (auto index: pattern_indices)
        {
            const auto solution = -dualValuesCst[index];
            pattern_value += solution;
            if (solution > criterionThreasholdByArea.at(std::string(
                  criterion_input_data_.Criteria()[pattern_index].Pattern().GetBody())))
            {
                // 1h were criterion is satisfied
                criteria_value += subproblem_weight;
            }
        }
        patterns_values[pattern_index] = pattern_value;
        criteria[pattern_index] = criteria_value;
    }
}
} // namespace Benders::Criterion
