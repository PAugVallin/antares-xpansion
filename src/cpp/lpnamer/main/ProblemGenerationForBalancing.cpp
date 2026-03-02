
#include "antares-xpansion/lpnamer/main/ProblemGenerationForBalancing.h"

#include <execution>
#include <iostream>
#include <tbb/parallel_for_each.h>
#include <utility>

#include <antares/api/solver.h>

#include "antares-xpansion/benders/output/OutputWriter.h"
#include "antares-xpansion/helpers/solver_utils.h"
#include "antares-xpansion/lpnamer/problem_modifier/XpansionProblemsFromAntaresProvider.h"
#include "malloc.h"

/// @brief Launch the simulation and save the problems satisfying startWeek <= week <= endweek
/// @param directories The directories to use for the problems generation
/// @param areaInvestments The area investments to use for the problems modification
/// @param logger The logger to use
/// @param solverName The name of the solver to use
/// @param startWeek The start week of the problems to take into account
/// @param endWeek The end week of the problems to take into account
ProblemGenerationForBalancing::ProblemGenerationForBalancing(
  ConfigurationManager::ConfigDirectories directories,
  std::map<std::string, AreaInvestment>& areaInvestments,
  Logger logger,
  const std::string& solverName,
  unsigned int startWeek,
  unsigned int endWeek,
  bool writePbFiles,
  const std::string& problemFormat):
    ProblemGenerationOptimSimu(directories,
                               logger,
                               solverName,
                               startWeek,
                               endWeek,
                               writePbFiles,
                               problemFormat),
    areaInvestments(areaInvestments)
{
    fillDispProdVarIndicesAndMarginalCosts();
}

/// @brief Update the problems for the balancing calculation
/// @param simuValues The simulation values to use for the problems modification
void ProblemGenerationForBalancing::fillDispProdVarIndicesAndMarginalCosts()
{
    const auto& firstProblem = problems.begin()->second;
    auto vars = firstProblem->get_col_names();
    for (auto& s: vars)
    {
        s.erase(s.find_last_not_of(" \t\n\r\f\v") + 1); // remove whitespaces
    }

    size_t nbVars = vars.size();
    std::vector<double> objCoeffs(nbVars);
    firstProblem->get_obj(objCoeffs.data(), 0, nbVars - 1);

    const std::unordered_map<std::string_view, size_t> varToIndex(
      [&]()
      {
          std::unordered_map<std::string_view, size_t> index;
          index.reserve(vars.size());
          for (size_t i = 0; i < nbVars; ++i)
          {
              index.emplace(vars[i], i);
          }
          return index;
      }());

    for (const auto& [areaName, areaInvestment]: areaInvestments)
    {
        const auto processCluster = [&](const std::string& clusterName)
        {
            const AreaCluster key{areaName, clusterName};

            for (size_t hour = 0; hour < NUMBER_OF_HOURS_PER_WEEK; ++hour)
            {
                const std::string varName = "DispatchableProduction::area<" + areaName
                                            + ">::ThermalCluster<" + clusterName + ">::hour<"
                                            + std::to_string(hour) + ">";

                const auto it = varToIndex.find(varName);
                if (it != varToIndex.end())
                {
                    balancingData[key].dispProdVarIndices[hour] = it->second;

                    if (hour == 0)
                    {
                        balancingData[key].marginalCost = objCoeffs[it->second];
                    }
                }
            }
        };

        for (const auto& clusterName: areaInvestment.investmentCandidates | std::views::keys)
        {
            processCluster(clusterName);
        }
        for (const auto& clusterName: areaInvestment.decommissioningCandidates | std::views::keys)
        {
            processCluster(clusterName);
        }
    }
}

/// @brief Find the action to apply for each area cluster
/// @param simuValues The simulation values to use for the problems modification
/// @return A map associating each area cluster to the action to apply
std::map<AreaCluster, CapacityAction> ProblemGenerationForBalancing::findAreaClustersToModify(
  const std::map<Antares::Solver::WeeklyProblemId, PbOutput>& simuValues)
{
    std::map<AreaCluster, CapacityAction> areaClusterToModify;
    const auto areaCritState = areaCriteriaState(simuValues);
    updateAreaInvestmentIncrement(areaCritState);

    for (const auto& [areaName, areaInvestment]: areaInvestments)
    {
        const CriterionState current = areaCritState.at(areaName);
        const CriterionState previous = areaInvestment.oldCriterionState.value_or(current);

        if (current == CriterionState::VALID)
        {
            continue;
        }

        CapacityAction action = determineCapacityAction(current, previous, areaInvestment);
        const std::string clusterName = getBestCluster(simuValues,
                                                       areaName,
                                                       areaInvestment,
                                                       action);
        areaClusterToModify[{areaName, clusterName}] = action;
    }

    updateOldCriterionState(areaCritState);
    return areaClusterToModify;
}

/// @brief Find the action to apply from the criterion states and area investment parameters
/// @param current The current criterion state
/// @param previous The previous criterion state
/// @param areaInvestment The area investment parameters
/// @return The action to apply
CapacityAction ProblemGenerationForBalancing::determineCapacityAction(
  CriterionState current,
  CriterionState previous,
  const AreaInvestment& areaInvestment)
{
    CapacityAction action;
    if (current == CriterionState::HIGHER && previous == CriterionState::HIGHER)
    {
        action = CapacityAction::INVESTMENT;
    }
    else if (current == CriterionState::LOWER && previous == CriterionState::LOWER)
    {
        action = CapacityAction::DECOMMISSIONING;
    }
    else
    {
        if (current == CriterionState::LOWER && previous == CriterionState::HIGHER)
        {
            action = areaInvestment.isDevestmentPossible() ? CapacityAction::DIVESTMENT
                                                           : CapacityAction::DECOMMISSIONING;
        }
        else
        {
            action = areaInvestment.isRecommissioningPossible() ? CapacityAction::RECOMMISSIONING
                                                                : CapacityAction::INVESTMENT;
        }
    }
    return action;
}

/// @brief Find the best cluster for a given area
/// @param simuValues The simulation values to look for the cluster selection
/// @param areaName The name of the area to find the best cluster for
/// @param areaInvestment The area investment parameters
/// @param action The action to apply for which the best cluster is looked for
/// @return The name of the best cluster for the given area and action
std::string ProblemGenerationForBalancing::getBestCluster(
  const std::map<Antares::Solver::WeeklyProblemId, PbOutput>& simuValues,
  const std::string& areaName,
  const AreaInvestment& areaInvestment,
  CapacityAction action)
{
    const auto computeRentability = [&](const auto& candidates, const auto& getExtraCost)
    {
        std::map<std::string, double> rentability;
        for (const auto& [clusterName, candidate]: candidates)
        {
            rentability[clusterName] = 0;
            for (const auto& [pbId, pbOutput]: simuValues)
            {
                rentability[clusterName] += std::accumulate(
                                              pbOutput.areaPrices.at(areaName).begin(),
                                              pbOutput.areaPrices.at(areaName).end(),
                                              0.0)
                                            - balancingData.at({areaName, clusterName})
                                                .marginalCost;
            }
            rentability[clusterName] -= candidate.candidateParams->fixedOmCosts
                                        + getExtraCost(candidate);
        }
        return rentability;
    };

    const std::map<std::string, double>
      rentability = action == CapacityAction::INVESTMENT || action == CapacityAction::DIVESTMENT
                      ? computeRentability(areaInvestment.investmentCandidates,
                                           [action](const auto& c) {
                                               return action == CapacityAction::INVESTMENT
                                                        ? c.candidateParams->investmentCost
                                                        : 0.0;
                                           })
                      : computeRentability(areaInvestment.decommissioningCandidates,
                                           [](const auto&) { return 0.0; });

    const bool selectMax = action == CapacityAction::INVESTMENT
                           || action == CapacityAction::RECOMMISSIONING;
    const auto valueOf = [](const auto& entry) { return entry.second; };
    const auto best = selectMax ? std::ranges::max_element(rentability, {}, valueOf)
                                : std::ranges::min_element(rentability, {}, valueOf);
    return best->first;
}

/// @brief Update the the area investment increments based on the criterion states
/// @param areaCritState The criterion states to use for the update
void ProblemGenerationForBalancing::updateAreaInvestmentIncrement(
  const std::map<std::string, CriterionState>& areaCritState)
{
    for (auto& [area, areaInvestment]: areaInvestments)
    {
        if (areaInvestment.oldCriterionState != areaCritState.at(area)
            && areaCritState.at(area) != CriterionState::VALID)
        {
            areaInvestment.currentInvestmentIncrement = std::max(
              areaInvestment.investmentIncrement * 0.1,
              areaInvestment.currentInvestmentIncrement - 0.1 * areaInvestment.investmentIncrement);
            areaInvestment.currentDecommissioningIncrement = std::max(
              areaInvestment.decommissioningIncrement * 0.1,
              areaInvestment.currentDecommissioningIncrement
                - 0.1 * areaInvestment.decommissioningIncrement);
        }
    }
}

/// @brief Update the old criterion states with the current ones
/// @param areaCritState The current criterion states to set as old criterion states
void ProblemGenerationForBalancing::updateOldCriterionState(
  const std::map<std::string, CriterionState>& areaCritState)
{
    for (auto& [area, areaInvestment]: areaInvestments)
    {
        areaInvestment.oldCriterionState = areaCritState.at(area);
    }
}

/// @brief Compute the criterion state from the area investment parameters and the criterion value
/// @param areaInvestment The area investment parameters to use for the computation
/// @param value The criterion value to use for the computation
/// @return The criterion state computed
CriterionState ProblemGenerationForBalancing::criterionState(AreaInvestment& areaInvestment,
                                                             double value)
{
    if (value < areaInvestment.reliabilityStandard - areaInvestment.reliabilityStandardDeadBandDown)
    {
        return CriterionState::LOWER;
    }
    else if (value
             > areaInvestment.reliabilityStandard + areaInvestment.reliabilityStandardDeadBandUp)
    {
        return CriterionState::HIGHER;
    }
    else
    {
        return CriterionState::VALID;
    }
}

/// @brief Compute the average area criteria values from the simulation values
/// @param simuValues The simulation values to compute the average from
/// @return The average area criteria values
std::map<std::string, double> computeAverageAreaCriteriaValues(
  const std::map<Antares::Solver::WeeklyProblemId, PbOutput>& simuValues)
{
    std::set<unsigned int> years;
    std::map<std::string, double> areaCriteria;

    for (const auto& [id, output]: simuValues)
    {
        years.insert(id.year);
        for (const auto& [area, value]: output.areaCriterionValues)
        {
            areaCriteria[area] += value;
        }
    }

    const double numYears = static_cast<double>(years.size());
    for (auto& sum: areaCriteria | std::views::values)
    {
        sum /= numYears;
    }

    return areaCriteria;
}

/// @brief Compute the criterion states for each area
/// @param simuValues The simulation values to use for the computation
/// @return The criterion state for each area
std::map<std::string, CriterionState> ProblemGenerationForBalancing::areaCriteriaState(
  const std::map<Antares::Solver::WeeklyProblemId, PbOutput>& simuValues)
{
    std::map<std::string, CriterionState> areaCriteriaState;
    const auto avgAreaCriteria = computeAverageAreaCriteriaValues(simuValues);
    for (const auto& [area, value]: avgAreaCriteria)
    {
        areaCriteriaState[area] = criterionState(areaInvestments[area], value);
    }
    return areaCriteriaState;
}

/// @brief Apply the action for each area cluster to the problems
/// @param areaCluster The area cluster to apply the action to
/// @param action The action to apply
void ProblemGenerationForBalancing::applyActionToCluster(const AreaCluster& areaCluster,
                                                         CapacityAction action)
{
    tbb::parallel_for_each(
      problems | std::views::values,
      [&](const std::shared_ptr<Problem>& problem)
      {
          const auto& varIndices = balancingData.at(areaCluster).dispProdVarIndices;
          std::vector<int> vecIndices(varIndices.begin(), varIndices.end());
          std::vector<double> varValues(varIndices.size());
          std::vector<char> boundTypes(varIndices.size());

          for (size_t hour = 0; hour < NUMBER_OF_HOURS_PER_WEEK; ++hour)
          {
              const size_t varIndex = varIndices[hour];
              double newBound;
              switch (action)
              {
              case CapacityAction::INVESTMENT:
                  boundTypes[hour] = 'U';
                  problem->get_ub(&newBound, varIndex, varIndex);
                  newBound += areaInvestments.at(areaCluster.first).currentInvestmentIncrement;
                  break;
              case CapacityAction::DIVESTMENT:
                  boundTypes[hour] = 'U';
                  problem->get_lb(&newBound, varIndex, varIndex);
                  newBound -= areaInvestments.at(areaCluster.first).currentInvestmentIncrement;
                  break;
              case CapacityAction::DECOMMISSIONING:
                  boundTypes[hour] = 'L';
                  problem->get_ub(&newBound, varIndex, varIndex);
                  newBound -= areaInvestments.at(areaCluster.first).currentDecommissioningIncrement;
                  break;
              case CapacityAction::RECOMMISSIONING:
                  boundTypes[hour] = 'L';
                  problem->get_lb(&newBound, varIndex, varIndex);
                  newBound += areaInvestments.at(areaCluster.first).currentDecommissioningIncrement;
                  break;
              }
              varValues[hour] = newBound;
          }
          problem->chg_bounds(vecIndices, boundTypes, varValues);
      });
}

/// @brief Update the problems using the balancing algorithm
/// @param simuValues The simulation values to use for the problems modification
/// @return The updated problems
std::map<Antares::Solver::WeeklyProblemId, std::shared_ptr<Problem>>
ProblemGenerationForBalancing::updateProblems(
  const std::map<Antares::Solver::WeeklyProblemId, PbOutput>& simuValues)
{
    if (simuValues.size())
    {
        for (const auto& [areaCluster, action]: findAreaClustersToModify(simuValues))
        {
            applyActionToCluster(areaCluster, action);
        }
    }

    else
    {
        for (auto& [area, areaInvestment]: areaInvestments)
        {
            areaInvestment.oldCriterionState = CriterionState::INVALID;
        }
    }
    return problems;
}

bool ProblemGenerationForBalancing::isBalanced() const
{
    for (const auto& [area, areaInvestment]: areaInvestments)
    {
        if (!areaInvestment.oldCriterionState
            || areaInvestment.oldCriterionState.value() != CriterionState::VALID)
        {
            return false;
        }
    }
    return true;
}
