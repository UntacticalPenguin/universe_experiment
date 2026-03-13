#pragma once

#include <string>

#include "simulation.hpp"

class JsonWriter {
public:
    static bool writeScene(const std::string& path, const SimulationResult& result);
};