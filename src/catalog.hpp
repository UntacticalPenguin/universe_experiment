#pragma once

#include <string>
#include <vector>
#include "body.hpp"

// Catalog loads stars from a CSV file into memory.
class Catalog {
public:
    bool loadCsv(const std::string& path);

    const std::vector<Body>& bodies() const {
        return bodies_;
    }

private:
    std::vector<Body> bodies_;
};