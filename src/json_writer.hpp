#pragma once

#include <string>
#include <vector>

#include "gas_catalog.hpp"
#include "simulation.hpp"

class JsonWriter {
public:
    static bool writeScene(const std::string& path,
                           const SimulationResult& result,
                           const std::vector<GasPoint>& gasPoints = {},
                           double gasDensityMax = 0.0);
};