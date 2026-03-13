#include "simulation.hpp"

void Simulation::setBodies(std::vector<Body> bodies) {
    bodies_ = std::move(bodies);
}

void Simulation::step(double dtYears) {
    for (auto& b : bodies_) {
        if (!b.hasMotion) {
            continue;
        }

        b.pos.x += b.vel.x * dtYears;
        b.pos.y += b.vel.y * dtYears;
        b.pos.z += b.vel.z * dtYears;
    }
}