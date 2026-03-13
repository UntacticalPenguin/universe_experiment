#include <iostream>
#include <vector>

#include "catalog.hpp"
#include "projection.hpp"
#include "simulation.hpp"
#include "svg_writer.hpp"

int main() {
    // 1. Load the small CSV subset.
    Catalog catalog;
    if (!catalog.loadCsv("../data/stars_small.csv")) {
        return 1;
    }

    // 2. Put the stars into the simulation.
    Simulation simulation;
    simulation.setBodies(std::vector<Body>(
        catalog.bodies().begin(),
        catalog.bodies().end()
    ));

    // 3. Create a simple projection.
    Projection projection(8.0, 6.0);

    // 4. Save the initial view.
    if (!SvgWriter::write("stars_t0.svg", simulation.bodies(), projection, 1400, 900)) {
        std::cerr << "Failed to write stars_t0.svg\n";
        return 1;
    }

    // 5. Advance time by 1000 years.
    simulation.step(1000.0);

    // 6. Save the later view.
    if (!SvgWriter::write("stars_t1000.svg", simulation.bodies(), projection, 1400, 900)) {
        std::cerr << "Failed to write stars_t1000.svg\n";
        return 1;
    }

    std::cout << "Done.\n";
    std::cout << "Created:\n";
    std::cout << "  stars_t0.svg\n";
    std::cout << "  stars_t1000.svg\n";

    return 0;
}