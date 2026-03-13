#include "catalog.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

// Split one CSV line by commas.
// This simple version is enough for our generated stars_small.csv.
static std::vector<std::string> splitCsvLine(const std::string& line) {
    std::vector<std::string> parts;
    std::stringstream ss(line);
    std::string item;

    while (std::getline(ss, item, ',')) {
        parts.push_back(item);
    }

    return parts;
}

// Convert text to double.
// If it fails, use the fallback value.
static double toDouble(const std::string& text, double fallback = 0.0) {
    try {
        return std::stod(text);
    } catch (...) {
        return fallback;
    }
}

bool Catalog::loadCsv(const std::string& path) {
    std::ifstream file(path);

    if (!file.is_open()) {
        std::cerr << "Failed to open CSV file: " << path << "\n";
        return false;
    }

    bodies_.clear();

    std::string line;
    bool isHeader = true;

    while (std::getline(file, line)) {
        if (line.empty()) {
            continue;
        }

        if (isHeader) {
            isHeader = false;
            continue;
        }

        auto parts = splitCsvLine(line);

        // We expect at least:
        // id,name,x,y,z,vx,vy,vz,mag,ci
        if (parts.size() < 10) {
            continue;
        }

        Body b;

        b.id = static_cast<std::uint32_t>(toDouble(parts[0], 0.0));
        b.name = parts[1];

        b.pos.x = toDouble(parts[2], 0.0);
        b.pos.y = toDouble(parts[3], 0.0);
        b.pos.z = toDouble(parts[4], 0.0);

        b.vel.x = toDouble(parts[5], 0.0);
        b.vel.y = toDouble(parts[6], 0.0);
        b.vel.z = toDouble(parts[7], 0.0);

        b.mag = static_cast<float>(toDouble(parts[8], 10.0));
        b.colorIndex = static_cast<float>(toDouble(parts[9], 0.0));

        // For step 1, allow all stars to move.
        b.hasMotion = true;

        bodies_.push_back(std::move(b));
    }

    std::cout << "Loaded " << bodies_.size() << " stars from " << path << "\n";
    return true;
}