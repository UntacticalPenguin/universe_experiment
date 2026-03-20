#pragma once

#include <functional>
#include <vector>

#include "catalog.hpp"

struct SimulationConfig {
    int frameCount = 96;
    int substepsPerFrame = 4;
    double yearsPerFrame = 1000.0;
};

struct SimulationResult {
    std::vector<Body> bodies;
    std::vector<std::vector<Vec3>> frames;
    double yearsPerFrame = 0.0;
};

class Simulation {
public:
    void setBodies(std::vector<Body> bodies);
    SimulationResult precompute(
        const SimulationConfig& config,
        const std::function<void(int completedFrames, int totalFrames)>& onProgress = {}) const;

private:
    std::vector<Body> bodies_;
};