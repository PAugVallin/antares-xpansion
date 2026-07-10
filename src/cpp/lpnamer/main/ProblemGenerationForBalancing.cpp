
#include "antares-xpansion/lpnamer/main/ProblemGenerationForBalancing.h"

#include <iostream>
#include <numeric>
#include <tbb/parallel_for_each.h>
#include <utility>

#include <antares/api/solver.h>
#include <antares/solver/lps/LpsFromAntares.h>

/// @brief Launch the simulation and save the problems
/// @param directories The directories to use for the problems generation
/// @param areaSettings The area investments to use for the problems modification
/// @param logger The logger to use
/// @param solverName The name of the solver to use
ProblemGenerationForBalancing::ProblemGenerationForBalancing(
  ConfigurationManager::ConfigDirectories directories,
  std::map<std::string, AreaSettings>& areasSettings,
  Logger logger,
  std::shared_ptr<ProblemManager> problemManager,
  std::filesystem::path iterationsLogFileName):
    ProblemGenerationOptimSimu(directories, logger, problemManager),
    areasSettings(areasSettings),
    iterationsLogFileName(iterationsLogFileName)
{
    fillDispProdVarIndicesAndMarginalCosts();
    getInitialCapacitiesForCandidates();
    initializeOscillationRecords();
    initializeIterativeLogCSV();
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

void ProblemGenerationForBalancing::getInitialCapacitiesForCandidates()
{
    auto updateCapacity =
      [&](const std::string& areaName, const std::string& clusterName, auto& candidate)
    {
        const AreaCluster key{areaName, clusterName};
        const auto& dispProdVarIndices = balancingData[key].dispProdVarIndices;
        problemManager->getProblems().begin()->second->get_ub(&candidate.currentCapacity,
                                                              dispProdVarIndices[0],
                                                              dispProdVarIndices[0]);
        candidate.initialCapacity = candidate.currentCapacity;
        candidate.previousCapacity = candidate.initialCapacity;
    };

    for (auto& [areaName, areaSetting]: areasSettings)
    {
        for (auto& [clusterName, candidate]: areaSetting.investmentCandidates)
        {
            updateCapacity(areaName, clusterName, candidate);
        }

        for (auto& [clusterName, candidate]: areaSetting.decommissioningCandidates)
        {
            updateCapacity(areaName, clusterName, candidate);
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
    const auto& firstProblem = problemManager->getProblems().begin()->second;
    auto vars = firstProblem->get_col_names();
    for (auto& s: vars)
    {
        s.erase(s.find_last_not_of(" \t\n\r\f\v") + 1); // remove whitespaces
    }

    size_t nbVars = vars.size();
    std::vector<double> objCoeffs(nbVars);
    firstProblem->get_obj(objCoeffs.data(), 0, nbVars - 1);

    const auto varToIndex = buildVarToIndex(vars);

    for (const auto& [areaName, areaSettings]: areasSettings)
    {
        for (const auto& clusterName: areaSettings.investmentCandidates | std::views::keys)
        {
            fillDispProdVarIndicesAndMarginalCostsForArea(areaName,
                                                          clusterName,
                                                          varToIndex,
                                                          objCoeffs);
        }
        for (const auto& clusterName: areaSettings.decommissioningCandidates | std::views::keys)
        {
            fillDispProdVarIndicesAndMarginalCostsForArea(areaName,
                                                          clusterName,
                                                          varToIndex,
                                                          objCoeffs);
        }
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

void ProblemGenerationForBalancing::logCriterionAndAreaSettings(
  const std::map<Antares::Solver::WeeklyProblemId, PbOutput>& simuValues) const
{
    std::map<std::string, CriterionState> areaCritState;
    // the average criteria values are needed for logging purposes
    const auto avgAreaCriteria = computeAverageAreaCriteriaValues(simuValues);
    for (const auto& [area, value]: avgAreaCriteria)
    {
        areaCritState[area] = criterionState(areasSettings[area], value);
    }

    // For each area, log the criterion state and the DispatchableProduction variable values for the
    // clusters of the area
    for (const auto& [areaName, criterionState]: areaCritState)
    {
        std::stringstream ss;
        ss << "\n  Criterion state for area " << areaName << ": " << to_string(criterionState)
           << " | max oscillation reached : " << std::boolalpha << maxOscillationReached(areaName)
           << "\n";
        ss << "  Average criteria value: " << avgAreaCriteria.at(areaName);
        const auto& areaSettings = areasSettings.at(areaName);
        if (criterionState != CriterionState::VALID)
        {
            double threshold;
            if (criterionState == CriterionState::HIGHER)
            {
                ss << " (which is above the threshold of " << higherThreshold(areaSettings) << ")";
            }
            else
            {
                ss << " (which is below the threshold of " << lowerThreshold(areaSettings) << ")";
            }
        }
        ss << "\n";

        for (const auto& [clusterName, investmentCandidate]: areaSettings.investmentCandidates)
        {
            ss << "  Invested capacity for candidate cluster " << clusterName << ": "
               << investmentCandidate.currentCapacity
               << " | oscillation : " << oscillationRecords.at({areaName, clusterName}).first
               << "\n";
        }
        for (const auto& [clusterName, decommissioningCandidate]:
             areaSettings.decommissioningCandidates)
        {
            ss << "  Decommissioned capacity for candidate cluster " << clusterName << ": "
               << decommissioningCandidate.currentCapacity
               << " | oscillation : " << oscillationRecords.at({areaName, clusterName}).first
               << "\n";
        }

        logger->display_message(ss.str(),
                                LogUtils::LOGLEVEL::INFO,
                                PROBLEM_GENERATION_LOGGER_CONTEXT);
    }
}

void ProblemGenerationForBalancing::initializeIterativeLogCSV() const
{
    std::ofstream file(iterationsLogFileName);
    if (!file.is_open())
    {
        throw std::runtime_error("Failed to open iterative log file for writing: "
                                 + iterationsLogFileName.string());
    }
    // header
    file << "iteration,zone,criteria,action,cluster candidate,capacity change\n";
}

void ProblemGenerationForBalancing::saveCriterionAndAreaSettingsToIterativeLogCSV(
  int iteration,
  const std::map<Antares::Solver::WeeklyProblemId, PbOutput>& simuValues) const
{
    std::ofstream file(iterationsLogFileName, std::ios_base::app);
    if (!file.is_open())
    {
        throw std::runtime_error("Failed to open iterative log file for writing: "
                                 + iterationsLogFileName.string());
    }
    const auto areaCritState = areaCriteriaState(simuValues);
    std::string action;
    auto writeLineToFile = [&]<typename T>(const std::string& areaName,
                                           const std::string& clusterName,
                                           const CriterionState& criterionState,
                                           const Candidate<T>& candidate)
    {
        // only writing the line if capacity has been modified, i.e. an action has been
        // performed
        if (candidate.currentCapacity != candidate.previousCapacity)
        {
            action = (lastActionForArea.find(areaName) != lastActionForArea.end())
                       ? to_string(lastActionForArea.at(areaName))
                       : "NO ACTION";
            file << iteration << "," << areaName << "," << to_string(criterionState) << ","
                 << action << "," << clusterName << ","
                 << candidate.currentCapacity - candidate.previousCapacity << "\n";
        }
    };

    for (const auto& [areaName, criterionState]: areaCritState)
    {
        const auto& areaSettings = areasSettings.at(areaName);
        for (const auto& [clusterName, investmentCandidate]: areaSettings.investmentCandidates)
        {
            writeLineToFile(areaName, clusterName, criterionState, investmentCandidate);
        }
        for (const auto& [clusterName, decommissioningCandidate]:
             areaSettings.decommissioningCandidates)
        {
            writeLineToFile(areaName, clusterName, criterionState, decommissioningCandidate);
        }
    }
}

void ProblemGenerationForBalancing::saveCriterionAndAreaSettingsToCSV(
  const std::map<Antares::Solver::WeeklyProblemId, PbOutput>& simuValues,
  const std::filesystem::path& outputPath) const
{
    std::ofstream file(outputPath);
    if (!file.is_open())
    {
        throw std::runtime_error("Failed to open file for writing: " + outputPath.string());
    }

    file << "areaName,candidateName,capacity\n";

    const auto areaCritState = areaCriteriaState(simuValues);
    for (const auto& [areaName, criterionState]: areaCritState)
    {
        const auto& areaSettings = areasSettings.at(areaName);
        for (const auto& [clusterName, investmentCandidate]: areaSettings.investmentCandidates)
        {
            file << areaName << "," << clusterName << "," << investmentCandidate.currentCapacity
                 << "\n";
        }
        for (const auto& [clusterName, decommissioningCandidate]:
             areaSettings.decommissioningCandidates)
        {
            file << areaName << "," << clusterName << ","
                 << decommissioningCandidate.currentCapacity << "\n";
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
    updateAreaSettingsIncrement(areaCritState);

    for (const auto& [areaName, areaSettings]: areasSettings)
    {
        const CriterionState current = areaCritState.at(areaName);
        const CriterionState previous = areaSettings.oldCriterionState;

        if (current == CriterionState::VALID)
        {
            continue;
        }

        std::optional<CapacityAction> action = determineCapacityAction(areaName,
                                                                       current,
                                                                       areaSettings);
        // if no action possible, no cluster will be modified
        if (action.has_value())
        {
            const std::string clusterName = getBestCluster(simuValues,
                                                           areaName,
                                                           areaSettings,
                                                           action.value());
            areaClusterToModify[{areaName, clusterName}] = action.value();
        }
    }

    updateOldCriterionState(areaCritState);
    return areaClusterToModify;
}

/// @brief Find the action to apply from the criterion states and area investment parameters
/// @param areaName The name of the area
/// @param currentState The current criterion state
/// @param areaSettings The area investment parameters
/// @return The action to apply
std::optional<CapacityAction> ProblemGenerationForBalancing::determineCapacityAction(
  const std::string& areaName,
  CriterionState currentState,
  const AreaSettings& areaSettings) const
{
    std::optional<CapacityAction> previousAction;
    if (lastActionForArea.find(areaName) != lastActionForArea.end())
    {
        previousAction = lastActionForArea.at(areaName);
    }

    const bool isHigher = currentState == CriterionState::HIGHER;
    // Investment cycle if the previous action was investment or disinvestment, or if it's the first
    // iteration and the criterion is higher than the target
    const bool isInvestmentCycle = previousAction == CapacityAction::INVESTMENT
                                   || previousAction == CapacityAction::DISINVESTMENT
                                   || (!previousAction.has_value() && isHigher);

    if (maxOscillationReached(areaName))
    {
        // if no action is possible: logging a warning and carrying on
        std::ostringstream oss;
        oss << "No action in area " << areaName << " because max oscillation has been reached";
        logger->display_message(oss.str(),
                                LogUtils::LOGLEVEL::INFO,
                                PROBLEM_GENERATION_LOGGER_CONTEXT);
        return std::nullopt;
    }

    if (isInvestmentCycle && isHigher)
    {
        if (areaSettings.isInvestmentPossible())
        {
            return CapacityAction::INVESTMENT;
        }
        if (areaSettings.isRecommissioningPossible())
        {
            return CapacityAction::RECOMMISSIONING;
        }
    }
    else if (isInvestmentCycle && !isHigher)
    {
        if (areaSettings.isDisinvestmentPossible())
        {
            return CapacityAction::DISINVESTMENT;
        }
        if (areaSettings.isDecommissioningPossible())
        {
            return CapacityAction::DECOMMISSIONING;
        }
    }
    else if (isHigher)
    {
        if (areaSettings.isRecommissioningPossible())
        {
            return CapacityAction::RECOMMISSIONING;
        }
        if (areaSettings.isInvestmentPossible())
        {
            return CapacityAction::INVESTMENT;
        }
    }
    else
    {
        if (areaSettings.isDecommissioningPossible())
        {
            return CapacityAction::DECOMMISSIONING;
        }
        if (areaSettings.isDisinvestmentPossible())
        {
            return CapacityAction::DISINVESTMENT;
        }
    }

    // if no action is possible: logging a warning and carrying on
    std::ostringstream oss;
    oss << "Area " << areaName << " is not balanced but no modification is possible\n"
        << " Current criterion state: " << to_string(currentState) << "\n"
        << " Previous action: "
        << (previousAction.has_value() ? to_string(previousAction.value()) : "None") << "\n";
    logger->display_message(oss.str(),
                            LogUtils::LOGLEVEL::WARNING,
                            PROBLEM_GENERATION_LOGGER_CONTEXT);
    return std::nullopt;
}

template<typename CandidateType>
static double extraCost(const Candidate<CandidateType>& candidate)
{
    if constexpr (std::is_same_v<CandidateType, Investment>)
    {
        return candidate.currentCapacity
               * (candidate.params->investmentCost + candidate.params->fixedOmCosts);
    }
    else
    {
        return candidate.currentCapacity * candidate.params->fixedOmCosts;
    }
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
        if constexpr (std::is_same_v<CandidateType, Investment>)
        {
            if (action == CapacityAction::INVESTMENT
                && candidate.currentCapacity == candidate.params->expansionPotential)
            {
                rentability[clusterName] = std::numeric_limits<double>::min();
                continue;
            }
            else if (action == CapacityAction::DISINVESTMENT
                     && candidate.currentCapacity == candidate.initialCapacity)
            {
                rentability[clusterName] = std::numeric_limits<double>::max();
                continue;
            }
        }
        else
        {
            if (action == CapacityAction::DECOMMISSIONING
                && candidate.currentCapacity == candidate.params->decommissioningPotential)
            {
                rentability[clusterName] = std::numeric_limits<double>::max();
                continue;
            }
            else if (action == CapacityAction::RECOMMISSIONING
                     && candidate.currentCapacity == candidate.initialCapacity)
            {
                rentability[clusterName] = std::numeric_limits<double>::min();
                continue;
            }
        }
        for (const auto& [pbId, pbOutput]: simuValues)
        {
            value += std::accumulate(pbOutput.areaPrices.at(areaName).begin(),
                                     pbOutput.areaPrices.at(areaName).end(),
                                     0.0)
                       * candidate.currentCapacity
                     - balancingData.at({areaName, clusterName}).marginalCost;
        }
        value -= extraCost(candidate);
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
/// @param areaSettings The area investment parameters
/// @param action The action to apply for which the best cluster is looked for
/// @return The name of the best cluster for the given area and action
std::string ProblemGenerationForBalancing::getBestCluster(
  const std::map<Antares::Solver::WeeklyProblemId, PbOutput>& simuValues,
  const std::string& areaName,
  const AreaSettings& areaSettings,
  CapacityAction action) const
{
    const bool isInvestmentAction = action == CapacityAction::INVESTMENT
                                    || action == CapacityAction::DISINVESTMENT;

    std::map<std::string, double> rentability;
    if (isInvestmentAction)
    {
        rentability = computeRentabilityForCandidates(areaName,
                                                      areaSettings.investmentCandidates,
                                                      simuValues,
                                                      action);
    }
    else
    {
        rentability = computeRentabilityForCandidates(areaName,
                                                      areaSettings.decommissioningCandidates,
                                                      simuValues,
                                                      action);
    }

    return selectBestClusterFromRentability(rentability, action);
}

/// @brief Update the the area investment increments based on the criterion states
/// @param areaCritState The criterion states to use for the update
void ProblemGenerationForBalancing::updateAreaSettingsIncrement(
  const std::map<std::string, CriterionState>& areaCritState)
{
    for (auto& [area, areaSettings]: areasSettings)
    {
        if (areaSettings.oldCriterionState != areaCritState.at(area)
            && areaSettings.oldCriterionState != CriterionState::UNINITIALIZED
            && areaCritState.at(area) != CriterionState::VALID)
        {
            areaSettings.currentInvestmentIncrement = std::max(
              areaSettings.investmentIncrement * 0.1,
              areaSettings.currentInvestmentIncrement - 0.1 * areaSettings.investmentIncrement);
            areaSettings.currentDecommissioningIncrement = std::max(
              areaSettings.decommissioningIncrement * 0.1,
              areaSettings.currentDecommissioningIncrement
                - 0.1 * areaSettings.decommissioningIncrement);
        }
    }
}

/// @brief Update the old criterion states with the current ones
/// @param areaCritState The current criterion states to set as old criterion states
void ProblemGenerationForBalancing::updateOldCriterionState(
  const std::map<std::string, CriterionState>& areaCritState)
{
    for (auto& [area, areaSettings]: areasSettings)
    {
        areaSettings.oldCriterionState = areaCritState.at(area);
    }
}

/// @brief Compute the criterion state from the area investment parameters and the criterion
/// value
/// @param areaSettings The area investment parameters to use for the computation
/// @param value The criterion value to use for the computation
/// @return The criterion state computed
CriterionState ProblemGenerationForBalancing::criterionState(const AreaSettings& areaSettings,
                                                             double value) const
{
    if (value < lowerThreshold(areaSettings))
    {
        return CriterionState::LOWER;
    }
    else if (value > higherThreshold(areaSettings))
    {
        return CriterionState::HIGHER;
    }
    else
    {
        return CriterionState::VALID;
    }
}

/// @brief Compute the criterion states for each area
/// @param simuValues The simulation values to use for the computation
/// @return The criterion state for each area
std::map<std::string, CriterionState> ProblemGenerationForBalancing::areaCriteriaState(
  const std::map<Antares::Solver::WeeklyProblemId, PbOutput>& simuValues) const
{
    std::map<std::string, CriterionState> areaCriteriaState;
    const auto avgAreaCriteria = computeAverageAreaCriteriaValues(simuValues);
    for (const auto& [area, value]: avgAreaCriteria)
    {
        areaCriteriaState[area] = criterionState(areasSettings[area], value);
    }
    return areaCriteriaState;
}

/// @brief Compute the new bound for a variable and update the candidate's current value
/// @param problem The problem to get the current bound from
/// @param varIndex The index of the variable
/// @param action The action to apply
/// @param areaSettings The area investment data to update
/// @param clusterName The name of the cluster to update
/// @return The new bound value
double ProblemGenerationForBalancing::computeNewBoundAndUpdateCandidate(
  const std::shared_ptr<Problem>& problem,
  size_t varIndex,
  CapacityAction action,
  AreaSettings& areaSettings,
  const std::string& clusterName) const
{
    double newBound;
    switch (action)
    {
    case CapacityAction::INVESTMENT:
        problem->get_ub(&newBound, varIndex, varIndex);
        newBound = std::min(
          newBound + areaSettings.currentInvestmentIncrement,
          areaSettings.investmentCandidates.at(clusterName).params->expansionPotential);
        areaSettings.investmentCandidates.at(clusterName).currentCapacity = newBound;
        break;
    case CapacityAction::DISINVESTMENT:
        problem->get_ub(&newBound, varIndex, varIndex);
        newBound = std::max(newBound - areaSettings.currentInvestmentIncrement,
                            areaSettings.investmentCandidates.at(clusterName).initialCapacity);
        areaSettings.investmentCandidates.at(clusterName).currentCapacity = newBound;
        break;
    case CapacityAction::DECOMMISSIONING:
        problem->get_ub(&newBound, varIndex, varIndex);
        newBound = std::max(
          newBound - areaSettings.currentDecommissioningIncrement,
          areaSettings.decommissioningCandidates.at(clusterName).params->decommissioningPotential);
        areaSettings.decommissioningCandidates.at(clusterName).currentCapacity = newBound;
        break;
    case CapacityAction::RECOMMISSIONING:
        problem->get_ub(&newBound, varIndex, varIndex);
        newBound = std::min(newBound + areaSettings.currentDecommissioningIncrement,
                            areaSettings.decommissioningCandidates.at(clusterName).initialCapacity);
        areaSettings.decommissioningCandidates.at(clusterName).currentCapacity = newBound;
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
    case CapacityAction::DECOMMISSIONING:
    case CapacityAction::RECOMMISSIONING:
        return 'U';
    }
}

/// @brief Apply the action for each area cluster to the problems
/// @param areaCluster The area cluster to apply the action to
/// @param action The action to apply
void ProblemGenerationForBalancing::applyActionToCluster(const AreaCluster& areaCluster,
                                                         CapacityAction action)
{
    lastActionForArea[areaCluster.first] = action;
    const auto& varIndices = balancingData.at(areaCluster).dispProdVarIndices;
    auto& areaSettings = areasSettings.at(areaCluster.first);
    const char boundType = boundTypeForAction(action);

    std::vector<int> vecIndices(varIndices.begin(), varIndices.end());
    std::vector<char> boundTypes(NUMBER_OF_HOURS_PER_WEEK, boundType);

    tbb::parallel_for_each(problemManager->getProblems() | std::views::values,
                           [&](const std::shared_ptr<Problem>& problem)
                           {
                               std::vector<double> localVarValues(NUMBER_OF_HOURS_PER_WEEK);
                               for (size_t hour = 0; hour < NUMBER_OF_HOURS_PER_WEEK; ++hour)
                               {
                                   localVarValues[hour] = computeNewBoundAndUpdateCandidate(
                                     problem,
                                     varIndices[hour],
                                     action,
                                     areaSettings,
                                     areaCluster.second);
                               }
                               problem->chg_bounds(vecIndices, boundTypes, localVarValues);
                           });
}

/// @brief Update the problems using the balancing algorithm
/// @param simuValues The simulation values to use for the problems modification
/// @return The updated problems
std::shared_ptr<ProblemManager> ProblemGenerationForBalancing::updateProblems(
  const std::map<Antares::Solver::WeeklyProblemId, PbOutput>& simuValues)
{
    // For the first iteration, simuValues is empty and no modification should be applied
    if (simuValues.empty())
    {
        return problemManager;
    }

    // updating previous capacity
    for (auto& [areaName, areaSettings]: areasSettings)
    {
        for (auto& candidate: areaSettings.investmentCandidates | std::views::values)
        {
            candidate.previousCapacity = candidate.currentCapacity;
        }
        for (auto& candidate: areaSettings.decommissioningCandidates | std::views::values)
        {
            candidate.previousCapacity = candidate.currentCapacity;
        }
    }

    const auto& areaClusterToModify = findAreaClustersToModify(simuValues);
    // If no action available on all areas then the system is blocked
    if (areaClusterToModify.empty())
    {
        blocked = true;
    }
    for (const auto& [areaCluster, action]: areaClusterToModify)
    {
        const double* candidateCapacity;
        switch (action)
        {
        case CapacityAction::INVESTMENT:
        case CapacityAction::DISINVESTMENT:
            candidateCapacity = &areasSettings.at(areaCluster.first)
                                   .investmentCandidates.at(areaCluster.second)
                                   .currentCapacity;
            break;
        case CapacityAction::DECOMMISSIONING:
        case CapacityAction::RECOMMISSIONING:
            candidateCapacity = &areasSettings.at(areaCluster.first)
                                   .decommissioningCandidates.at(areaCluster.second)
                                   .currentCapacity;
            break;
        }
        double previousCandidateCapacity = *candidateCapacity;
        applyActionToCluster(areaCluster, action);
        updateRecords(areaCluster, action);
        logger->display_message((std::stringstream()
                                 << " action: " << to_string(action) << " area: "
                                 << areaCluster.first << " cluster: " << areaCluster.second
                                 << " new capacity: " << *candidateCapacity
                                 << " delta: " << (*candidateCapacity - previousCandidateCapacity))
                                  .str(),
                                LogUtils::LOGLEVEL::INFO,
                                PROBLEM_GENERATION_LOGGER_CONTEXT);
    }
    return problemManager;
}

/// @brief Check if the system is balanced from the simulation values
/// @param simuValues The simulation values to use for the computation
/// @return true if the system is balanced, false otherwise
bool ProblemGenerationForBalancing::isBalanced(
  const std::map<Antares::Solver::WeeklyProblemId, PbOutput>& simuValues) const
{
    const auto areaCritState = areaCriteriaState(simuValues);
    return !areaCritState.empty()
           && std::ranges::all_of(areaCritState | std::views::values,
                                  [](const auto& critState)
                                  { return critState == CriterionState::VALID; });
}

/// @brief Check if the system can perform any action
/// @return true if the system is blocked, false otherwise
bool ProblemGenerationForBalancing::isBlocked() const
{
    return blocked;
}

/// @brief Intialize oscillation records
/// @param areaSettings Data of the areas
void ProblemGenerationForBalancing::initializeOscillationRecords()
{
    for (const auto& [areaName, areaSetting]: areasSettings)
    {
        for (const auto& [clusterName, investmentCandidate]: areaSetting.investmentCandidates)
        {
            oscillationRecords[{areaName, clusterName}] = {0, std::nullopt};
        }
        for (const auto& [clusterName, decommissioningCandidate]:
             areaSetting.decommissioningCandidates)
        {
            oscillationRecords[{areaName, clusterName}] = {0, std::nullopt};
        }
    }
}

/// @brief Update records for an area cluster
/// @param areaCluster Area cluster to update
/// @param areaCluster Action apply to the area cluster
void ProblemGenerationForBalancing::updateRecords(const AreaCluster& areaCluster,
                                                  CapacityAction action)
{
    auto& oscillationStatus = oscillationRecords[areaCluster];
    if (oscillationStatus.second.has_value() && action != oscillationStatus.second)
    {
        oscillationStatus.first += 1;
    }
    oscillationStatus.second = action;
}

/// @brief Check if an area reachs max oscillation through one of their candidate
/// @param areaName The name of area to check
/// @return true if the area has reached mas oscillation, false otherwise
bool ProblemGenerationForBalancing::maxOscillationReached(const std::string& areaName) const
{
    bool maxOscillationReached = false;
    for (const auto& [areaCluster, oscillationStatus]: oscillationRecords)
    {
        if (areaCluster.first == areaName
            && oscillationStatus.first >= areasSettings[areaName].maxOscillation)
        {
            maxOscillationReached = true;
            continue;
        }
    }
    return maxOscillationReached;
}
