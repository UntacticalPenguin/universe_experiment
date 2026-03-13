#include <iostream>
#include <utility>
#include <vector>

#include "catalog.hpp"
#include "simulation.hpp"
#include "json_writer.hpp"

int main() {
    Catalog catalog;
    if (!catalog.loadCsv("stars_small.csv")) {
        std::cerr << "Failed to load CSV\n";
        return 1;
    }

    std::vector<Body> bodies = catalog.bodies();

    Body sun;
    sun.id = 0;
    sun.name = "Sun";
    sun.pos = {0.0, 0.0, 0.0};
    sun.vel = {0.0, 0.0, 0.0};
    sun.mag = -26.74f;
    sun.colorIndex = 0.656f;
    sun.isSun = true;
    sun.hasMotion = false;
    bodies.push_back(sun);

    Simulation simulation;
    simulation.setBodies(std::move(bodies));

    SimulationConfig config;
    config.frameCount = 96;
    config.substepsPerFrame = 4;
    config.yearsPerFrame = 1000.0;

    SimulationResult result = simulation.precompute(config);

    if (!JsonWriter::writeScene("web/scene.json", result)) {
        std::cerr << "Failed to write web/scene.json\n";
        return 1;
    }

    std::cout << "Wrote web/scene.json\n";
    return 0;
}