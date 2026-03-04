
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

/// @brief Fill the DispatchableProduction variable indices and marginal cost for a given area
/// @param areaName The name of the area to process
/// @param clusterName The name of the cluster to process
/// @param varToIndex A map from variable names to their indices
/// @param objCoeffs The objective coefficients for each variable
void ProblemGenerationForBalancing::fillDispProdVarIndicesAndMarginalCostsForArea(
  const std::string& areaName,
  const std::string& clusterName,
  const std::unordered_map<std::string_view, size_t>& varToIndex,
  const std::vector<double>& objCoeffs)
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
}

static std::unordered_map<std::string_view, size_t> buildVarToIndex(
  const std::vector<std::string>& vars)
{
    std::unordered_map<std::string_view, size_t> index;
    index.reserve(vars.size());
    for (size_t i = 0; i < vars.size(); ++i)
    {
        index.emplace(vars[i], i);
    }
    return index;
}

/// @brief Update the problems for the balancing calculation
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

    const auto varToIndex = buildVarToIndex(vars);

    for (const auto& [areaName, areaInvestment]: areaInvestments)
    {
        for (const auto& clusterName: areaInvestment.investmentCandidates | std::views::keys)
        {
            fillDispProdVarIndicesAndMarginalCostsForArea(areaName,
                                                          clusterName,
                                                          varToIndex,
                                                          objCoeffs);
        }
        for (const auto& clusterName: areaInvestment.decommissioningCandidates | std::views::keys)
        {
            fillDispProdVarIndicesAndMarginalCostsForArea(areaName,
                                                          clusterName,
                                                          varToIndex,
                                                          objCoeffs);
        }
    }
}

void ProblemGenerationForBalancing::logCriterionAndAreaInvestments(
  const std::map<std::string, CriterionState>& areaCriteriaState)
{
    // For each area, log the criterion state and the DispatchableProduction variable values for the
    // clusters of the area
    for (const auto& [areaName, criterionState]: areaCriteriaState)
    {
        std::stringstream ss;
        ss << "Criterion state for area " << areaName << ": " << to_string(criterionState) << "\n";

        const auto& areaInvestment = areaInvestments.at(areaName);
        for (const auto& [clusterName, candidate]: areaInvestment.investmentCandidates)
        {
            ss << "  Investment candidate cluster " << clusterName << ", current : +"
               << candidate.currentDispatchableProductionValue << "\n";
        }
        for (const auto& [clusterName, candidate]: areaInvestment.decommissioningCandidates)
        {
            ss << "  Decommissioning candidate cluster " << clusterName << ", current : -"
               << candidate.currentDispatchableProductionValue << "\n";
        }

        logger->display_message(ss.str(),
                                LogUtils::LOGLEVEL::DEBUG,
                                PROBLEM_GENERATION_LOGGER_CONTEXT);
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
    logCriterionAndAreaInvestments(areaCritState);

    for (const auto& [areaName, areaInvestment]: areaInvestments)
    {
        const CriterionState current = areaCritState.at(areaName);
        const CriterionState previous = areaInvestment.oldCriterionState;

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
            action = areaInvestment.isDisinvestmentPossible() ? CapacityAction::DISINVESTMENT
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

static double extraCost(const Candidate<Investment>& candidate, CapacityAction action)
{
    return action == CapacityAction::INVESTMENT ? candidate.candidateParams->investmentCost : 0.0;
}

static double extraCost(const Candidate<Decommissioning>&, CapacityAction)
{
    return 0.0;
}

template<typename CandidateType>
std::map<std::string, double> ProblemGenerationForBalancing::computeRentabilityForCandidates(
  const std::string& areaName,
  const std::map<std::string, Candidate<CandidateType>>& candidates,
  const std::map<Antares::Solver::WeeklyProblemId, PbOutput>& simuValues,
  CapacityAction action) const
{
    std::map<std::string, double> rentability;
    for (const auto& [clusterName, candidate]: candidates)
    {
        double value = 0.0;
        for (const auto& [pbId, pbOutput]: simuValues)
        {
            value += std::accumulate(pbOutput.areaPrices.at(areaName).begin(),
                                     pbOutput.areaPrices.at(areaName).end(),
                                     0.0)
                     - balancingData.at({areaName, clusterName}).marginalCost;
        }
        value -= candidate.candidateParams->fixedOmCosts + extraCost(candidate, action);
        rentability[clusterName] = value;
    }
    return rentability;
}

static bool shouldSelectMaxRentability(CapacityAction action)
{
    return action == CapacityAction::INVESTMENT || action == CapacityAction::RECOMMISSIONING;
}

static std::string selectBestClusterFromRentability(
  const std::map<std::string, double>& rentability,
  CapacityAction action)
{
    const auto valueOf = [](const auto& entry) { return entry.second; };
    const auto best = shouldSelectMaxRentability(action)
                        ? std::ranges::max_element(rentability, {}, valueOf)
                        : std::ranges::min_element(rentability, {}, valueOf);
    return best->first;
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
    const bool isInvestmentAction = action == CapacityAction::INVESTMENT
                                    || action == CapacityAction::DISINVESTMENT;

    std::map<std::string, double> rentability;
    if (isInvestmentAction)
    {
        rentability = computeRentabilityForCandidates(areaName,
                                                      areaInvestment.investmentCandidates,
                                                      simuValues,
                                                      action);
    }
    else
    {
        rentability = computeRentabilityForCandidates(areaName,
                                                      areaInvestment.decommissioningCandidates,
                                                      simuValues,
                                                      action);
    }

    return selectBestClusterFromRentability(rentability, action);
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

/// @brief Compute the new bound for a variable and update the candidate's current value
/// @param problem The problem to get the current bound from
/// @param varIndex The index of the variable
/// @param action The action to apply
/// @param areaInvestment The area investment data to update
/// @param clusterName The name of the cluster to update
/// @return The new bound value
double ProblemGenerationForBalancing::computeNewBoundAndUpdateCandidate(
  const std::shared_ptr<Problem>& problem,
  size_t varIndex,
  CapacityAction action,
  AreaInvestment& areaInvestment,
  const std::string& clusterName)
{
    double newBound;
    switch (action)
    {
    case CapacityAction::INVESTMENT:
        problem->get_ub(&newBound, varIndex, varIndex);
        newBound += areaInvestment.currentInvestmentIncrement;
        areaInvestment.investmentCandidates.at(clusterName).currentDispatchableProductionValue
          = newBound;
        break;
    case CapacityAction::DISINVESTMENT:
        problem->get_lb(&newBound, varIndex, varIndex);
        newBound -= areaInvestment.currentInvestmentIncrement;
        areaInvestment.investmentCandidates.at(clusterName).currentDispatchableProductionValue
          = newBound;
        break;
    case CapacityAction::DECOMMISSIONING:
        problem->get_ub(&newBound, varIndex, varIndex);
        newBound -= areaInvestment.currentDecommissioningIncrement;
        areaInvestment.decommissioningCandidates.at(clusterName).currentDispatchableProductionValue
          = newBound;
        break;
    case CapacityAction::RECOMMISSIONING:
        problem->get_lb(&newBound, varIndex, varIndex);
        newBound += areaInvestment.currentDecommissioningIncrement;
        areaInvestment.decommissioningCandidates.at(clusterName).currentDispatchableProductionValue
          = newBound;
        break;
    }
    return newBound;
}

static char boundTypeForAction(CapacityAction action)
{
    switch (action)
    {
    case CapacityAction::INVESTMENT:
    case CapacityAction::DISINVESTMENT:
        return 'U';
    case CapacityAction::DECOMMISSIONING:
    case CapacityAction::RECOMMISSIONING:
        return 'L';
    }
}

/// @brief Apply the action for each area cluster to the problems
/// @param areaCluster The area cluster to apply the action to
/// @param action The action to apply
void ProblemGenerationForBalancing::applyActionToCluster(const AreaCluster& areaCluster,
                                                         CapacityAction action)
{
    const auto& varIndices = balancingData.at(areaCluster).dispProdVarIndices;
    auto& areaInvestment = areaInvestments.at(areaCluster.first);
    const char boundType = boundTypeForAction(action);

    std::vector<int> vecIndices(varIndices.begin(), varIndices.end());
    std::vector<char> boundTypes(NUMBER_OF_HOURS_PER_WEEK, boundType);

    tbb::parallel_for_each(problems | std::views::values,
                           [&](const std::shared_ptr<Problem>& problem)
                           {
                               std::vector<double> localVarValues(NUMBER_OF_HOURS_PER_WEEK);
                               for (size_t hour = 0; hour < NUMBER_OF_HOURS_PER_WEEK; ++hour)
                               {
                                   localVarValues[hour] = computeNewBoundAndUpdateCandidate(
                                     problem,
                                     varIndices[hour],
                                     action,
                                     areaInvestment,
                                     areaCluster.second);
                               }
                               problem->chg_bounds(vecIndices, boundTypes, localVarValues);
                           });
}

/// @brief Update the problems using the balancing algorithm
/// @param simuValues The simulation values to use for the problems modification
/// @return The updated problems
std::map<Antares::Solver::WeeklyProblemId, std::shared_ptr<Problem>>
ProblemGenerationForBalancing::updateProblems(
  const std::map<Antares::Solver::WeeklyProblemId, PbOutput>& simuValues)
{
    // For the first iteration, simuValues is empty and no modification should be applied
    if (simuValues.empty())
    {
        return problems;
    }

    for (const auto& [areaCluster, action]: findAreaClustersToModify(simuValues))
    {
        applyActionToCluster(areaCluster, action);
    }
    return problems;
}

bool ProblemGenerationForBalancing::isBalanced() const
{
    for (const auto& [area, areaInvestment]: areaInvestments)
    {
        if (areaInvestment.oldCriterionState != CriterionState::VALID)
        {
            return false;
        }
    }
    return true;
}
