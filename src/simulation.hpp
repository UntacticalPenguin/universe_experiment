#pragma once

#include <vector>
#include "body.hpp"

// Simulation owns the current star state and can advance it over time.
class Simulation {
public:
    void setBodies(std::vector<Body> bodies);

    // Move the simulation forward by dtYears years.
    void step(double dtYears);

    const std::vector<Body>& bodies() const {
        return bodies_;
    }

private:
    std::vector<Body> bodies_;
};