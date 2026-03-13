#include <iostream>
#include <vector>

#include "catalog.hpp"
#include "simulation.hpp"
#include "json_writer.hpp"

int main() {
    Catalog catalog;
    if (!catalog.loadCsv("stars_small.csv")) {
        return 1;
    }

    Simulation simulation;
    simulation.setBodies(std::vector<Body>(
        catalog.bodies().begin(),
        catalog.bodies().end()
    ));

    if (!JsonWriter::write("web/stars.json", simulation.bodies())) {
        std::cerr << "Failed to write web/stars.json\n";
        return 1;
    }

    std::cout << "Wrote web/stars.json\n";
    return 0;
}